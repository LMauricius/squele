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
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace squele {

// Views take their source view, not its query, so their names stay short
// (`SlotFilterView<Store>`); the query is recovered as `Source::Query`
template <Viewable TSourceT> class FilterView;
template <Viewable TSourceT> class SlotFilterView;

/**
 * A query for the items of a source that pass a predicate.
 * Its view is `SlotFilterView` for a `SlotViewable` source, else `FilterView`.
 * @note The predicate is a function pointer (not `std::function`) so the
 * query is comparable and hashable. The same lambda written twice gives two
 * pointers, and so two views; name the function to share one view.
 */
template <Queryable TSourceQ> class Filter {
  public:
    using SourceQ = TSourceQ;
    using Source = typename SourceQ::View;
    using Item = typename Source::Item;
    using Predicate = bool (*)(const Item &);
    using View = std::conditional_t<SlotViewable<Source>,
                                    SlotFilterView<Source>, FilterView<Source>>;

    /**
     * @param source the source's query (`From{p_view}` for an original)
     * @param p_pred the predicate; must not be null (asserted) and must not
     * throw
     */
    Filter(TSourceQ source, Predicate p_pred)
        : m_source(std::move(source)), m_pred(p_pred) {
        assert(m_pred);
    }

    std::shared_ptr<View> result(Ledger &ledger) const {
        return std::make_shared<View>(ledger, *this);
    }

    const SourceQ &source() const { return m_source; }
    Predicate predicate() const { return m_pred; }

    bool operator==(const Filter &) const = default;

  private:
    SourceQ m_source;
    Predicate m_pred;
};

} // namespace squele

// Defined before the views, so Queryable<Filter<...>> never sees it missing
template <class TSourceQ> struct std::hash<squele::Filter<TSourceQ>> {
    std::size_t operator()(const squele::Filter<TSourceQ> &q) const {
        using Predicate = typename squele::Filter<TSourceQ>::Predicate;
        return squele::combineHash(std::hash<TSourceQ>{}(q.source()),
                                   std::hash<Predicate>{}(q.predicate()));
    }
};

namespace squele {

/**
 * The view of `Filter` over a source that is not `SlotViewable`.
 * @note Declared only: no such source exists yet.
 */
template <Viewable TSourceT> class FilterView {
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
 * It updates eagerly: `onDelta` re-checks only the changed indices while the
 * source is consistent, so the predicate runs once per source change. Its
 * pass flags are updated a word (64 slots) at a time, from the delta's
 * touched bits.
 */
template <Viewable TSourceT>
class SlotFilterView : public Broadcaster<typename TSourceT::Delta> {
  public:
    using SourceT = TSourceT;
    using Query = Filter<typename SourceT::Query>;
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
        m_pass.resize(static_cast<std::size_t>(m_source->slotCount()));
        for (auto it = m_source->begin(); it != m_source->end(); ++it) {
            if (m_pred(*it)) {
                m_pass.set(static_cast<std::size_t>(it.index()));
                ++m_activeCount;
            }
        }
        m_source->addInterests(sourceInterests);
        m_source->addListener(this);
    }

    ~SlotFilterView() {
        m_source->removeListener(this);
        m_source->removeInterests(sourceInterests);
    }

    /**
     * Re-checks the changed source indices and records the transitions.
     * @note noexcept: a failure here would leave this view inconsistent with
     * its source.
     */
    void onDelta(const Delta &d) noexcept {
        if (static_cast<std::size_t>(d.slotCount) > m_pass.size())
            m_pass.resize(static_cast<std::size_t>(d.slotCount));
        // Per index: word-level updates win only from about 2 changes per
        // touched word (measured with benchmarks/slot.cpp)
        const std::size_t changed = d.created.size() + d.deleted.size() +
                                    d.replaced.size() + d.modified.size();
        if (changed < 2 * d.touchedWords.size()) {
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
        const std::span<const BitWord> touched = d.touched.words();
        const std::span<const BitWord> taken = m_source->takenBits().words();
        const bool record = this->hasListeners();
        for (Index w : d.touchedWords) {
            const auto at = static_cast<std::size_t>(w);
            const std::size_t base = at * bitWordBits;
            const BitWord t = touched[at];
            const BitWord old = pass[at];

            // New pass bits: untouched bits keep theirs; touched ones pass
            // when the source holds them and the predicate accepts them
            BitWord next = old & ~t;
            for (BitWord alive = t & taken[at]; alive; alive &= alive - 1) {
                const int b = std::countr_zero(alive);
                if (check(static_cast<Index>(base + b)))
                    next |= BitWord{1} << b;
            }
            pass[at] = next;
            // Subtract first: old bits are counted, so this never wraps
            m_activeCount -= static_cast<Index>(std::popcount(old));
            m_activeCount += static_cast<Index>(std::popcount(next));

            // Without listeners nothing is recorded, so flush() stays a no-op
            if (record)
                recordWord(d, base, t & (old | next), old, next);
        }
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
     * Pulls the source if dirty; its delta updates this view in `onDelta`
     */
    void sync() {
        if (!m_dirty)
            return;
        pull(*m_source);
        m_dirty = false;
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
    // Read from the source's deltas: the spans by the per-index path, the
    // bits by the word path (`changes` only while recording). Fixed, since
    // `m_pass` needs every change whatever this view's listeners want
    static constexpr typename Delta::Interests sourceInterests =
        SlotInterest::ALL & ~SlotInterest::MOVED;

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
     * Word-level `update()`: records the transitions of the indices in `bits`
     * (touched, and passing before or after) of the word starting at index
     * `base`, given the word's old and new pass bits.
     */
    void recordWord(const Delta &d, std::size_t base, BitWord bits, BitWord old,
                    BitWord next) {
        for (; bits; bits &= bits - 1) {
            const int b = std::countr_zero(bits);
            const auto i = static_cast<Index>(base + b);
            const bool passed = (old >> b) & 1;
            const bool passes = (next >> b) & 1;
            if (passed && passes) {
                if (d.changes[base + b] == SlotChange::replaced) {
                    m_diff.erase(i);
                    m_diff.create(i);
                } else {
                    m_diff.modify(i);
                }
            } else if (passed) {
                m_diff.erase(i);
            } else {
                m_diff.create(i);
            }
        }
    }

    std::shared_ptr<SourceT> m_source; // owned, so it outlives this listener
    typename Query::Predicate m_pred;
    BitVector m_pass; // per source index: taken and passed
    Index m_activeCount = 0;
    SlotDiff<Index> m_diff;
    bool m_dirty = false;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_VIEWS_FILTER
