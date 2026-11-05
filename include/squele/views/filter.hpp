#pragma once
#ifndef H_INCLUDED_SQUELE_VIEWS_FILTER
#define H_INCLUDED_SQUELE_VIEWS_FILTER

#include "squele/bits/hash.hpp"
#include "squele/bits/pull.hpp"
#include "squele/bits/slotBits.hpp"
#include "squele/bits/slotIterator.hpp"
#include "squele/components/broadcaster.hpp"
#include "squele/core/concepts.hpp"
#include "squele/core/ledger.hpp"
#include "squele/deltas/slot.hpp"

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace squele {

/**
 * A callable that can be part of a query key: it is copyable, accepts a
 * `const TItemT &`, and is compared and hashed as one of
 * - a function pointer, by value;
 * - an empty type (a named functor, a captureless lambda), by type alone:
 *   the `Ledger` keeps one cache per query type, so all such keys are equal;
 * - any other type, by its `operator==` and `std::hash`.
 */
template <class P, class TItemT>
concept FilterPredicate =
    std::copyable<P> && std::predicate<const P &, const TItemT &> &&
    (std::is_pointer_v<P> || std::is_empty_v<P> ||
     (std::equality_comparable<P> && requires(const P &p) {
         { std::hash<P>{}(p) } -> std::convertible_to<std::size_t>;
     }));

namespace detail {

/**
 * The function pointer type that `Filter` uses by default, so existing code
 * that names `Filter<Q>` keeps its meaning
 */
template <class TSourceQ>
using PredicatePtr = bool (*)(const typename TSourceQ::View::Item &);

} // namespace detail

// Views take their source view, not its query, so their names stay short
// (`SlotFilterView<Store>`); the query is recovered from the source's query
// and the predicate type
template <Viewable TSourceT,
          class TPredT = detail::PredicatePtr<typename TSourceT::Query>>
class FilterView;
template <Viewable TSourceT,
          class TPredT = detail::PredicatePtr<typename TSourceT::Query>>
class SlotFilterView;

/**
 * A query for the items of a source that pass a predicate.
 * Its view is `SlotFilterView` for a `SlotViewable` source, else `FilterView`.
 * @note The predicate is a `FilterPredicate`, so the query is comparable and
 * hashable. A function pointer is called indirectly; an empty functor or a
 * captureless lambda (`Filter{q, [](const T &x) { ... }}`) is inlined into
 * the view. A lambda is a new type each time it is written, so two written
 * lambdas give two views (as two pointers do); name the predicate to share
 * one view.
 */
template <Queryable TSourceQ, class TPredT = detail::PredicatePtr<TSourceQ>>
    requires FilterPredicate<TPredT, typename TSourceQ::View::Item>
class Filter {
  public:
    using SourceQ = TSourceQ;
    using Source = typename SourceQ::View;
    using Item = typename Source::Item;
    using Predicate = TPredT;
    using View = std::conditional_t<SlotViewable<Source>,
                                    SlotFilterView<Source, Predicate>,
                                    FilterView<Source, Predicate>>;

    /**
     * @param source the source's query (`From{p_view}` for an original)
     * @param pred the predicate; a function pointer must not be null
     * (asserted). It must not throw
     */
    Filter(TSourceQ source, Predicate pred)
        : m_source(std::move(source)), m_pred(std::move(pred)) {
        if constexpr (std::is_pointer_v<Predicate>)
            assert(m_pred);
    }

    std::shared_ptr<View> result(Ledger &ledger) const {
        return std::make_shared<View>(ledger, *this);
    }

    const SourceQ &source() const { return m_source; }
    const Predicate &predicate() const { return m_pred; }

    bool operator==(const Filter &o) const {
        if constexpr (std::is_empty_v<Predicate>)
            return m_source == o.m_source;
        else
            return m_source == o.m_source && m_pred == o.m_pred;
    }

  private:
    SourceQ m_source;
    [[no_unique_address]] Predicate m_pred;
};

} // namespace squele

// Defined before the views, so Queryable<Filter<...>> never sees it missing
template <class TSourceQ, class TPredT>
struct std::hash<squele::Filter<TSourceQ, TPredT>> {
    std::size_t operator()(const squele::Filter<TSourceQ, TPredT> &q) const {
        const std::size_t h = std::hash<TSourceQ>{}(q.source());
        // An empty predicate is part of the type, which the Ledger already
        // keys on
        if constexpr (std::is_empty_v<TPredT>)
            return h;
        else
            return squele::combineHash(h, std::hash<TPredT>{}(q.predicate()));
    }
};

namespace squele {

/**
 * The view of `Filter` over a source that is not `SlotViewable`.
 * @note Declared only: no such source exists yet.
 */
template <Viewable TSourceT, class TPredT> class FilterView {
    // Depends on TSourceT, so it fires only when instantiated (see C++20
    // pitfalls)
    static_assert(sizeof(TSourceT) == 0,
                  "FilterView is not implemented: use a SlotViewable source");
};

/**
 * The view of `Filter` over a `SlotViewable` source. It reuses the source's
 * indices (index `i` is taken when the source's is and the item passed) and
 * reads items from the source, so it owns no item copies. Being
 * `SlotViewable` itself, it can be filtered again.
 *
 * It updates eagerly while it is read every cycle: `onDelta` re-checks only
 * the changed indices while the source is consistent, so the predicate runs
 * once per source change. A view that is not read folds the deltas into a
 * pending diff instead, and re-checks the net changes on its next `sync()`. Its
 * pass flags and its own diff are updated a word (64 slots) at a time, from
 * the delta's word blocks.
 */
template <Viewable TSourceT, class TPredT>
class SlotFilterView : public Broadcaster<typename TSourceT::Delta> {
  public:
    using SourceT = TSourceT;
    using Query = Filter<typename SourceT::Query, TPredT>;
    using Item = typename SourceT::Item;
    using Index = typename SourceT::Index;
    using Delta = typename SourceT::Delta;
    using Iterator = SlotIterator<SlotFilterView>;

    /**
     * Gets the source from `ledger`, checks every source item and starts
     * listening. The view starts clean.
     */
    SlotFilterView(Ledger &ledger, const Query &query)
        : m_source(ledger.getUnsynced(query.source())),
          m_pred(query.predicate()) {
        static_assert(Dependent<SlotFilterView, SourceT>);

        // Make the source consistent and leave nothing unflushed, so no
        // change is missed between this scan and addListener()
        pull(*m_source);
        // Word by word and branch-free, as in apply(). Both bit spans hold
        // slotCount() bits, so their padded word counts match. The pass bits
        // start clear, so free words are skipped without a store
        m_pass.resize(static_cast<std::size_t>(m_source->slotCount()));
        const std::span<const BitWord> taken = m_source->takenBits().words();
        const std::span<BitWord> pass = m_pass.words();
        for (std::size_t at = 0; at < taken.size(); ++at) {
            if (!taken[at])
                continue;
            const std::size_t base = at * bitWordBits;
            BitWord next = 0;
            for (BitWord alive = taken[at]; alive; alive &= alive - 1) {
                const int b = std::countr_zero(alive);
                next |= BitWord{check(static_cast<Index>(base + b))} << b;
            }
            pass[at] = next;
            if (next)
                m_activeCount += static_cast<Index>(std::popcount(next));
        }
        m_source->addInterests(sourceInterests);
        m_source->addListener(this);
    }

    ~SlotFilterView() {
        m_source->removeListener(this);
        m_source->removeInterests(sourceInterests);
    }

    /**
     * Applies the source's changes now if this view was read since the last
     * delta, else folds them into the pending diff for the next `sync()`.
     * A view read every cycle so stays eager; one that misses a cycle turns
     * lazy until it is read again, and pays only O(touched words) per source
     * flush meanwhile.
     * @note noexcept: a failure here would leave this view inconsistent with
     * its source.
     */
    void onDelta(const Delta &d) noexcept {
        if (m_readSinceDelta && m_pending.empty()) {
            apply(d);
        } else {
            m_pending.fold(d);
            m_pendingSlotCount = d.slotCount;
        }
        m_readSinceDelta = false;
    }

    /**
     * Marks this view dirty, forwarding only the first call per cycle
     */
    void onDirty() noexcept {
        if (m_dirty)
            return;
        m_dirty = true;
        this->broadcastDirty();
    }

    bool dirty() const { return m_dirty; }

    /**
     * Pulls the source if dirty; its delta updates this view in `onDelta`,
     * then the pending changes (if any) are applied
     */
    void sync() {
        if (m_dirty) {
            pull(*m_source);
            if (!m_pending.empty()) {
                // The source was just pulled, so it is consistent with the
                // pending changes; only slots that still hold an item are read
                namespace SI = SlotInterest;
                apply(m_pending.delta(0, m_pendingSlotCount,
                                      SI::SPARSE_SPANS | SI::WORDS |
                                          SI::TOUCHED_WORDS));
                m_pending.clear();
            }
            m_dirty = false;
        }
        // Set last: this read covers every delta so far, including those of
        // the pull above
        m_readSinceDelta = true;
    }

    /**
     * Sends the changes since the last flush to listeners, filling only the
     * delta members in `interests()`; does nothing when there are none.
     */
    void flush() {
        if (m_diff.empty())
            return;
        this->broadcast(
            m_diff.delta(m_activeCount, slotCount(), this->interests()));
        m_diff.clear();
    }

    /**
     * @returns the item at the taken index `i`
     * @warning Reading a free index is UB (not checked).
     */
    const Item &operator[](Index i) const { return (*m_source)[i]; }

    bool isTaken(Index i) const {
        const auto at = static_cast<std::size_t>(i);
        return at < m_pass.size() && m_pass.test(at);
    }

    BitSpan takenBits() const { return m_pass; }

    Index activeCount() const { return m_activeCount; }
    Index slotCount() const { return static_cast<Index>(m_pass.size()); }

    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, slotCount()}; }

  private:
    /**
     * Re-checks the changed source indices and records the transitions.
     * The source must be consistent with `d` (read inside the source's
     * flush, or after pulling it).
     */
    void apply(const Delta &d) noexcept {
        if (static_cast<std::size_t>(d.slotCount) > m_pass.size())
            m_pass.resize(static_cast<std::size_t>(d.slotCount));
        // Per index over the spans while sparse (the source fills them only
        // then, see SPARSE_SPANS); word level otherwise
        if (d.sparse()) {
            for (Index i : d.deleted)
                update(i, false, false);
            for (Index i : d.created)
                update(i, check(i), false);
            for (Index i : d.replaced)
                update(i, check(i), true);
            for (Index i : d.modified)
                update(i, check(i), false);
            return;
        }

        // Word level: 64 pass flags per step, predicate only where needed
        const std::span<BitWord> pass = m_pass.words();
        const bool record = this->hasListeners();
        for (Index w : d.touchedWords) {
            const auto at = static_cast<std::size_t>(w);
            const std::size_t base = at * bitWordBits;
            const SlotWordChange &c = d.words[at];
            const BitWord t = c.touched;
            const BitWord old = pass[at];

            // New pass bits: untouched bits keep theirs; touched ones pass
            // when the source holds them and the predicate accepts them
            BitWord next = old & ~t;
            for (BitWord alive = t & c.exists; alive; alive &= alive - 1) {
                const int b = std::countr_zero(alive);
                // Branch-free: the predicate's result is data-dependent, so a
                // branch on it mispredicts about half the time once inlined
                next |= BitWord{check(static_cast<Index>(base + b))} << b;
            }
            pass[at] = next;
            // Subtract first: old bits are counted, so this never wraps
            m_activeCount -= static_cast<Index>(std::popcount(old));
            m_activeCount += static_cast<Index>(std::popcount(next));

            // Without listeners nothing is recorded, so flush() stays a no-op
            if (record)
                recordWord(at, c, old, next);
        }
    }

    // Read from the source's deltas: the spans by the per-index path (sparse
    // deltas only), the word blocks by the word path. Fixed, since `m_pass`
    // needs every change whatever this view's listeners want
    static constexpr typename Delta::Interests sourceInterests =
        SlotInterest::SPARSE_SPANS | SlotInterest::WORDS |
        SlotInterest::TOUCHED_WORDS;

    bool check(Index i) const { return m_pred((*m_source)[i]); }

    /**
     * Records the transition of index `i`:
     * fail -> pass is created, pass -> fail is deleted, pass -> pass forwards
     * the source's kind (`replaced` becomes erase + create, which the diff
     * coalesces into a replace), fail -> fail records nothing.
     */
    void update(Index i, bool passes, bool replaced) {
        const auto at = static_cast<std::size_t>(i);
        const bool passed = m_pass.test(at);
        if (passed != passes) {
            if (passes) {
                m_pass.set(at);
                ++m_activeCount;
            } else {
                m_pass.reset(at);
                --m_activeCount;
            }
        }
        // Without listeners nothing is recorded, so flush() stays a no-op
        if (!this->hasListeners())
            return;
        if (passed && passes) {
            if (replaced) {
                m_diff.erase(i);
                m_diff.create(i);
            } else {
                m_diff.modify(i);
            }
        } else if (passed) {
            m_diff.erase(i);
        } else if (passes) {
            m_diff.create(i);
        }
    }

    /**
     * Word-level `update()`: records the transitions of word `at`, given the
     * source's change block `c` and the word's old and new pass bits. A kept
     * bit (passing before and after) is replaced where the source replaced
     * it, else modified.
     */
    void recordWord(std::size_t at, const SlotWordChange &c, BitWord old,
                    BitWord next) {
        const BitWord kept = old & next & c.touched;
        const BitWord keptReplaced = kept & ~c.same;
        // Erase before create, so a kept replaced bit coalesces to replaced
        if (const BitWord m = (old & ~next) | keptReplaced)
            m_diff.eraseWord(at, m);
        if (const BitWord m = (next & ~old) | keptReplaced)
            m_diff.createWord(at, m);
        if (const BitWord m = kept & c.same)
            m_diff.modifyWord(at, m);
    }

    std::shared_ptr<SourceT> m_source; // owned, so it outlives this listener
    [[no_unique_address]] typename Query::Predicate m_pred;
    BitVector m_pass; // per source index: taken and passed
    Index m_activeCount = 0;
    SlotDiff<Index> m_diff;
    // Source changes not applied yet, and the source's slot count at the
    // last of them
    SlotDiff<Index> m_pending;
    Index m_pendingSlotCount = 0;
    bool m_dirty = false;
    // Whether sync() ran since the last delta; a new view is about to be read
    bool m_readSinceDelta = true;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_VIEWS_FILTER
