#pragma once
#ifndef H_INCLUDED_SQUELE_VIEWS_SORT
#define H_INCLUDED_SQUELE_VIEWS_SORT

// API DRAFT: bodies are comments describing the flow, for review

#include "squele/bits/callable.hpp"
#include "squele/bits/order.hpp"
#include "squele/bits/pull.hpp"
#include "squele/bits/slotBits.hpp"
#include "squele/bits/slotIterator.hpp"
#include "squele/components/broadcaster.hpp"
#include "squele/core/concepts.hpp"
#include "squele/core/ledger.hpp"
#include "squele/deltas/slot.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace squele {

/**
 * A `KeyCallable` that is a strict weak order on `TItemT`
 */
template <class C, class TItemT>
concept SortComparator =
    KeyCallable<C> &&
    std::strict_weak_order<const C &, const TItemT &, const TItemT &>;

namespace detail {

/**
 * The index type of a sort view over `TSourceT`: the source's for a slot
 * source (positions never outnumber its indices), else a 32-bit default as
 * in `SlotStore`
 */
template <class TSourceT> struct SortIndex {
    using type = std::uint32_t;
};
template <SlotViewable TSourceT> struct SortIndex<TSourceT> {
    using type = typename TSourceT::Index;
};

/**
 * Takes no space in an entry of the general path, which has no source index
 */
struct NoIndex {};

} // namespace detail

template <ListViewable TSourceT, class TCmpT = std::ranges::less>
class ArraySortView;

/**
 * A query for the items of a source, sorted by a comparator.
 * Its view is `ArraySortView`.
 * @note The comparator is a `SortComparator`, keyed like the `Filter`
 * predicate. The order of equal items is unspecified (the sort is not stable).
 */
template <Queryable TSourceQ, class TCmpT = std::ranges::less>
    requires ListViewable<typename TSourceQ::View> &&
             SortComparator<TCmpT, typename TSourceQ::View::Item>
class Sort {
  public:
    using SourceQ = TSourceQ;
    using Source = typename SourceQ::View;
    using Item = typename Source::Item;
    using Compare = TCmpT;
    using View = ArraySortView<Source, Compare>;

    /**
     * @param source the source's query (`From{p_view}` for an original)
     * @param cmp the comparator; a function pointer must not be null
     * (asserted). It must not throw
     */
    explicit Sort(TSourceQ source, Compare cmp = {});

    std::shared_ptr<View> result(Ledger &ledger) const;

    const SourceQ &source() const { return m_source; }
    const Compare &compare() const { return m_cmp; }

    bool operator==(const Sort &o) const;

  private:
    SourceQ m_source;
    [[no_unique_address]] Compare m_cmp;
};

} // namespace squele

template <class TSourceQ, class TCmpT>
struct std::hash<squele::Sort<TSourceQ, TCmpT>> {
    std::size_t operator()(const squele::Sort<TSourceQ, TCmpT> &q) const;
};

namespace squele {

/**
 * The view of `Sort`: the source's items in sorted order, at the dense
 * positions `0..count-1`. It owns no item copies: each position holds a
 * pointer to its source item. It is `OrderedViewable` (so `SlotViewable`),
 * and reports its changes as `SlotDelta`s.
 *
 * Two paths, picked at compile time:
 * - General (any `ListViewable` source): its delta is not understood, so
 *   every source flush re-sorts everything. A position whose item address
 *   is unchanged is reported modified, one with a new address replaced, so
 *   `relocated` is never set.
 * - Slot (a `SlotViewable` source): each position also holds its source
 *   index. Source swaps only remap indices; changed items are taken out,
 *   sorted alone and merged back; the reordering is reported as swaps, so a
 *   filter over this view follows it without calling its predicate.
 *
 * Like `SlotFilterView`, it applies deltas eagerly while it is read every
 * cycle, and otherwise defers them to its next `sync()`.
 */
template <ListViewable TSourceT, class TCmpT>
class ArraySortView
    : public Broadcaster<
          SlotDelta<typename detail::SortIndex<TSourceT>::type>> {
  public:
    using SourceT = TSourceT;
    using Query = Sort<typename SourceT::Query, TCmpT>;
    using Item = typename SourceT::Item;
    using Index = typename detail::SortIndex<SourceT>::type;
    using Delta = SlotDelta<Index>;
    using Compare = TCmpT;
    using Iterator = SlotIterator<ArraySortView>;

    /**
     * Gets the source from `ledger`, sorts all its items and starts
     * listening. The view starts clean.
     */
    ArraySortView(Ledger &ledger, const Query &query) {
        // static_assert(Dependent<...> && OrderedViewable<ArraySortView>)
        // pull(*m_source), so nothing unflushed is missed
        // sortAll() without recording (no listeners yet)
        // slot path: m_source->addInterests(sourceInterests)
        // m_source->addListener(this)
    }

    ~ArraySortView() {
        // removeListener; slot path: removeInterests(sourceInterests)
    }

    /**
     * Applies the source's changes now if this view was read since the last
     * delta, else defers them to the next `sync()`.
     * @note noexcept: a failure here would leave this view inconsistent with
     * its source.
     */
    void onDelta(const typename SourceT::Delta &d) noexcept {
        // eager when m_readSinceDelta and nothing is deferred:
        //   slot path: apply(d); general path: sortAll()
        // else defer:
        //   slot path: m_pending.fold(d), m_pendingSlotCount = d.slotCount
        //   general path: m_resortDue = true
        // m_readSinceDelta = false
    }

    /**
     * Marks this view dirty, forwarding only the first call per cycle
     */
    void onDirty() noexcept {}

    bool dirty() const { return m_dirty; }

    /**
     * Pulls the source if dirty, then applies the deferred changes
     */
    void sync() {
        // as SlotFilterView::sync(); the deferred work is
        //   slot path: apply(m_pending.delta(0, m_pendingSlotCount,
        //              sourceInterests)), m_pending.clear()
        //   general path: sortAll() if m_resortDue
    }

    /**
     * Sends the changes since the last flush to listeners; does nothing when
     * there are none
     */
    void flush() {
        // m_diff.delta(count(), slotCount(), interests()), clear
    }

    /**
     * @returns the item at position `i`
     * @warning Reading a position past `count()` is UB (not checked).
     */
    const Item &operator[](Index i) const {
        return *m_order[static_cast<std::size_t>(i)].p_item;
    }

    bool isTaken(Index i) const { return i < count(); }
    BitSpan takenBits() const { return m_taken; }
    Index count() const { return static_cast<Index>(m_order.size()); }
    Index slotCount() const { return count(); }
    const Compare &compare() const { return m_cmp; }

    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, slotCount()}; }

  private:
    static constexpr bool slotPath = SlotViewable<SourceT>;
    using SourceIndex = std::conditional_t<slotPath, Index, detail::NoIndex>;

    // The rank of a source index that holds no item of this view
    static constexpr Index npos = std::numeric_limits<Index>::max();

    /**
     * One position: its item, and on the slot path its source index
     */
    struct Entry {
        const Item *p_item;
        [[no_unique_address]] SourceIndex index;
    };

    // Slot path: the members every step below reads
    static constexpr SlotInterest::Mask sourceInterests =
        SlotInterest::CREATED | SlotInterest::DELETED | SlotInterest::REPLACED |
        SlotInterest::MODIFIED | SlotInterest::SWAPPED | SlotInterest::WORDS |
        SlotInterest::TOUCHED_WORDS;

    // Slot path: a delta touching more than count / bulkDivisor items
    // re-sorts fully (placeholder until measured)
    static constexpr std::size_t bulkDivisor = 4;

    /**
     * Entries in sorted order, compared through their pointers
     */
    bool less(const Entry &a, const Entry &b) const {
        return m_cmp(*a.p_item, *b.p_item);
    }

    /**
     * Re-sorts everything from the source. The construction step of both
     * paths, and the only step of the general path.
     */
    void sortAll() {
        // Collect the entries by iterating the source (slot path: with
        // it.index()), std::sort them by less()
        // Slot path: fill m_rank (npos for free indices)
        // Record (with listeners), word level: below both counts, a position
        // with the same address is modified, one with a new address replaced
        // (erase + create), built as two masks per word without branches; the
        // tail is created or erased. The old pointers are compared as
        // integers (std::bit_cast to std::uintptr_t) before being
        // overwritten, never dereferenced
        // setTaken(new count)
    }

    /**
     * Slot path: applies a delta of the source, which must be consistent
     * with it
     */
    void apply(const typename SourceT::Delta &d) noexcept {
        // 1. Grow m_rank to d.slotCount (npos); forward d.relocated
        // 2. remapSwaps(d.swapped)
        // 3. takeOut(d): deleted and replaced leave; their old positions are
        //    recorded erased, their ranks become npos
        // 4. refreshPointers(d.relocated): every live entry when relocated,
        //    else only the stale ones from step 2
        // 5. Modified: kept in place (recorded modified) when still ordered
        //    against untouched neighbours (keptInPlace), else recorded
        //    modified at its old position and taken out, keeping its rank
        // 6. Batch: the taken-out modified entries plus created and replaced
        //    ones ({&src[i], i}); the new ones have rank npos
        // 7. compact() from the first taken-out position p0
        // 8. Bulk (batch above count / bulkDivisor): append the batch
        //    and std::sort everything, p = 0. Else std::sort the batch and
        //    mergeSortedBack(), p = min(p0, its result)
        // 9. recordReorder(p, oldCount) with listeners
        // 10. m_rank for positions [p, newCount); setTaken(newCount)
    }

    /**
     * Slot path: follows source swaps. Each swaps the ranks of its two
     * indices and fixes the entries' indices; the items keep their positions,
     * so nothing is compared or recorded. The two entries' pointers are
     * stale until refreshPointers()
     */
    void remapSwaps(std::span<const typename Delta::Swap> s) {
        // for {a, b}: std::swap(m_rank[a], m_rank[b]); for each side whose
        // rank is not npos: entry.index = that side, push its position to
        // m_stale
    }

    /**
     * Slot path: takes the deleted and replaced items out (null pointer as
     * the mark), records their old positions erased, and sets their ranks
     * to npos
     * @returns the first position taken out (count() if none)
     */
    Index takeOut(const typename SourceT::Delta &d) { return 0; }

    /**
     * Slot path: rereads pointers from source indices. All live entries when
     * `all` (a relocation), else the entries in m_stale; taken-out entries
     * are skipped, since their source slots may be free
     */
    void refreshPointers(bool all) {}

    /**
     * Slot path: whether the modified entry at `pos` is still ordered
     * against both neighbours, each untouched in the source delta (its
     * source index has no touched bit in `d.words`; missing words are
     * untouched). Equal neighbours count as ordered
     */
    bool keptInPlace(const typename SourceT::Delta &d, Index pos) const {
        return false;
    }

    /**
     * Slot path: removes the taken-out entries from `p0` on, keeping the
     * order of the rest. O(count - p0)
     */
    void compact(Index p0) {}

    /**
     * Slot path: records the move from the old order to the new one in
     * positions [p, max(oldCount, newCount)), after the erases and
     * modifications recorded at old positions:
     * - m_to[o - p] = q - p for each kept entry, from old position o (its
     *   rank, not yet updated) to new position q
     * - the remaining old positions (deleted, replaced, free tail) are paired
     *   in increasing order with the remaining new ones (new entries, free
     *   tail), so a replaced position that stays put needs no swap
     * - permuteBySwaps(m_to) into m_diff.swap(); SlotDiff makes the earlier
     *   records follow their items
     * - m_diff.create(q) for each new entry; on an erased slot it coalesces
     *   to replaced
     */
    void recordReorder(Index p, Index oldCount) {}

    /**
     * Sets the first `n` bits of `m_taken` and clears the rest, keeping the
     * zero padding, in O(changed words)
     */
    void setTaken(Index n) {}

    std::shared_ptr<SourceT> m_source; // owned, so it outlives this listener
    [[no_unique_address]] Compare m_cmp;
    std::vector<Entry> m_order; // per position
    BitVector m_taken;          // the first count() bits
    SlotDiff<Index> m_diff;
    bool m_dirty = false;
    bool m_readSinceDelta = true;

    // Slot path only (empty on the general path): the position of each
    // source index (npos when free), the deferred source changes, and
    // scratch buffers that keep their capacity
    std::vector<Index> m_rank;
    SlotDiff<Index> m_pending;
    Index m_pendingSlotCount = 0;
    std::vector<Index> m_stale; // positions with stale pointers
    std::vector<Entry> m_batch; // entries to merge back
    std::vector<Index> m_to;    // the reorder permutation

    // General path only: a re-sort is deferred to the next sync()
    bool m_resortDue = false;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_VIEWS_SORT
