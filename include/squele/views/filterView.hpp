#pragma once
#ifndef H_INCLUDED_SQUELE_VIEWS_FILTERVIEW
#define H_INCLUDED_SQUELE_VIEWS_FILTERVIEW

#include "squele/bits/pull.hpp"
#include "squele/bits/slotBits.hpp"
#include "squele/bits/slotIterator.hpp"
#include "squele/components/broadcaster.hpp"
#include "squele/core/concepts.hpp"
#include "squele/core/ledger.hpp"
#include "squele/deltas/slot.hpp"
#include "squele/queries/filter.hpp"

#include <bit>
#include <cstddef>
#include <memory>
#include <span>

namespace squele {

/**
 * The view of `Filter`. It reuses the source's
 * indices (index `i` is taken when the source's is and the item passed) and
 * reads items from the source, so it owns no item copies. Being
 * `SlotViewable` itself, it can be filtered again.
 *
 * It updates eagerly while it is read every cycle: `onDelta` re-checks only
 * the changed indices while the source is consistent, so the predicate runs
 * once per source change. A view that is not read folds the deltas into a
 * pending diff instead, and re-checks the net changes on its next `sync()`. Its
 * pass flags and its own diff are updated a word (64 slots) at a time, from
 * the delta's word blocks. A swap in the source swaps the pass flags without
 * re-checking, and is forwarded unless both slots fail. A relocation in the
 * source is forwarded as it is.
 * @note Only `SlotViewable` sources are supported yet; the general path
 * (constrained parts for other `ListViewable` sources) is not implemented.
 */
template <Viewable TSourceT, class TPredT>
    requires SlotViewable<TSourceT>
class FilterView : public Broadcaster<typename TSourceT::Delta> {
  public:
    using SourceT = TSourceT;
    using Query = Filter<typename SourceT::Query, TPredT>;
    using Item = typename SourceT::Item;
    using Index = typename SourceT::Index;
    using Delta = typename SourceT::Delta;
    using Iterator = SlotIterator<FilterView>;

    /**
     * Gets the source from `ledger`, checks every source item and starts
     * listening. The view starts clean.
     */
    FilterView(Ledger &ledger, const Query &query)
        : m_source(ledger.getUnsynced(query.source())),
          m_pred(query.predicate()) {
        static_assert(Dependent<FilterView, SourceT>);

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
                m_count += static_cast<Index>(std::popcount(next));
        }
        m_source->addInterests(sourceInterests);
        m_source->addListener(this);
    }

    ~FilterView() {
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
                apply(m_pending.delta(0, m_pendingSlotCount, sourceInterests));
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
        this->broadcast(m_diff.delta(m_count, slotCount(), this->interests()));
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

    Index count() const { return m_count; }
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
        // The items are the source's, so their addresses move with it
        if (d.relocated && this->hasListeners()) [[unlikely]]
            m_diff.relocate();
        // Swaps first: the other changes start from the swapped state
        if (!d.swapped.empty()) [[unlikely]]
            swapAll(d.swapped);
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
            m_count -= static_cast<Index>(std::popcount(old));
            m_count += static_cast<Index>(std::popcount(next));

            // Without listeners nothing is recorded, so flush() stays a no-op
            if (record)
                recordWord(at, c, old, next);
        }
    }

    // Read from the source's deltas: the swaps first, the spans by the
    // per-index path (sparse
    // deltas only), the word blocks by the word path. Fixed, since `m_pass`
    // needs every change whatever this view's listeners want
    static constexpr typename Delta::Interests sourceInterests =
        SlotInterest::SPARSE_SPANS | SlotInterest::WORDS |
        SlotInterest::TOUCHED_WORDS | SlotInterest::SWAPPED;

    bool check(Index i) const { return m_pred((*m_source)[i]); }

    /**
     * Applies the source's swaps in order. Kept out of line: inlined into
     * `apply()`, it slowed sparse deltas without swaps by about 5%
     */
    [[gnu::noinline]] void swapAll(std::span<const typename Delta::Swap> s) {
        for (const auto &[a, b] : s)
            swap(a, b);
    }

    /**
     * Follows a source swap of `a` and `b`: the pass flags trade places with
     * their items, so the predicate does not run
     */
    void swap(Index a, Index b) {
        const auto at = static_cast<std::size_t>(a);
        const auto bt = static_cast<std::size_t>(b);
        const bool pa = m_pass.test(at), pb = m_pass.test(bt);
        // Two failing slots are both free here, so the swap changes nothing
        // this view holds or reports (its diff stays valid as it is)
        if (!pa && !pb)
            return;
        if (pa != pb) {
            pa ? m_pass.reset(at) : m_pass.set(at);
            pb ? m_pass.reset(bt) : m_pass.set(bt);
        }
        if (this->hasListeners())
            m_diff.swap(a, b);
    }

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
                ++m_count;
            } else {
                m_pass.reset(at);
                --m_count;
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
    Index m_count = 0;
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

#endif // H_INCLUDED_SQUELE_VIEWS_FILTERVIEW
