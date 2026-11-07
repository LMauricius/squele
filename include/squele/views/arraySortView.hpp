#pragma once
#ifndef H_INCLUDED_SQUELE_VIEWS_ARRAYSORTVIEW
#define H_INCLUDED_SQUELE_VIEWS_ARRAYSORTVIEW

#include "squele/bits/order.hpp"
#include "squele/bits/pull.hpp"
#include "squele/bits/slotBits.hpp"
#include "squele/bits/slotIterator.hpp"
#include "squele/components/broadcaster.hpp"
#include "squele/core/concepts.hpp"
#include "squele/core/ledger.hpp"
#include "squele/deltas/slot.hpp"
#include "squele/queries/sort.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace squele {

namespace detail {

/**
 * Take no space in an entry of the general path, which has no source index
 * and no position to remember; two types, so both members can overlap
 */
struct NoIndex {};
struct NoPos {};

/**
 * Take no space in a sort view for the state of the path it does not use;
 * two types, so both members can overlap
 */
struct NoSlotState {};
struct NoGeneralState {};

/**
 * The bits of the positions `[lo, hi)` within the word that starts at
 * `base`; zero when the ranges do not meet. Sort views need it to record and
 * mark ranges of dense positions a word at a time.
 */
constexpr BitWord rangeBits(std::size_t base, std::size_t lo, std::size_t hi) {
    // Clamped to the word, so whole-word shifts never happen
    auto low = [base](std::size_t x) {
        const std::size_t n = x <= base ? 0 : std::min(x - base, bitWordBits);
        return n == bitWordBits ? ~BitWord{0} : (BitWord{1} << n) - 1;
    };
    return hi <= lo ? 0 : low(hi) & ~low(lo);
}

} // namespace detail

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
 * Like `FilterView`, it applies deltas eagerly while it is read every
 * cycle, and otherwise defers them to its next `sync()`.
 * @note Positions shift on insertion and removal: one change near the front
 * reports a swap for every later position.
 */
template <ListViewable TSourceT, class TCmpT>
class ArraySortView : public Broadcaster<SlotDelta<ViewIndex<TSourceT>>> {
  public:
    using SourceT = TSourceT;
    using Query = Sort<typename SourceT::Query, TCmpT>;
    using Item = typename SourceT::Item;
    using Index = ViewIndex<SourceT>;
    using Delta = SlotDelta<Index>;
    using Compare = TCmpT;
    using Iterator = SlotIterator<ArraySortView>;

    /**
     * Gets the source from `ledger`, sorts all its items and starts
     * listening. The view starts clean.
     */
    ArraySortView(Ledger &ledger, const Query &query)
        : m_source(ledger.getUnsynced(query.source())), m_cmp(query.compare()) {
        static_assert(Dependent<ArraySortView, SourceT> &&
                      OrderedViewable<ArraySortView>);
        // Make the source consistent and leave nothing unflushed, so no
        // change is missed between this sort and addListener(). Nothing is
        // recorded: there are no listeners yet
        pull(*m_source);
        sortAll();
        if constexpr (slotPath)
            m_source->addInterests(sourceInterests);
        m_source->addListener(this);
    }

    ~ArraySortView() {
        m_source->removeListener(this);
        if constexpr (slotPath)
            m_source->removeInterests(sourceInterests);
    }

    /**
     * Applies the source's changes now if this view was read since the last
     * delta, else defers them to the next `sync()`.
     * @note noexcept: a failure here would leave this view inconsistent with
     * its source.
     */
    void onDelta([[maybe_unused]] const typename SourceT::Delta &d) noexcept {
        if constexpr (slotPath) {
            if (m_readSinceDelta && m_slot.pending.empty()) {
                apply(d);
            } else {
                m_slot.pending.fold(d);
                m_slot.pendingSlotCount = d.slotCount;
            }
        } else {
            if (m_readSinceDelta && !m_general.resortDue)
                sortAll();
            else
                m_general.resortDue = true;
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
     * then the deferred changes (if any) are applied
     */
    void sync() {
        if (m_dirty) {
            pull(*m_source);
            // The source was just pulled, so it is consistent with the
            // deferred changes
            if constexpr (slotPath) {
                if (!m_slot.pending.empty()) {
                    apply(m_slot.pending.delta(0, m_slot.pendingSlotCount,
                                               sourceInterests));
                    m_slot.pending.clear();
                }
            } else if (m_general.resortDue) {
                sortAll();
                m_general.resortDue = false;
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
     * @note The shrink applies after the delta: its `slotCount` is the
     * largest count since the last flush, so it covers the erased positions.
     */
    void flush() {
        if (m_diff.empty())
            return;
        this->broadcast(m_diff.delta(count(), std::max(m_peakCount, count()),
                                     this->interests()));
        m_diff.clear();
        m_peakCount = 0;
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
    using Position = std::conditional_t<slotPath, Index, detail::NoPos>;

    // The rank of a source index that holds no item of this view
    static constexpr Index npos = std::numeric_limits<Index>::max();

    /**
     * One position: its item, and on the slot path its source index and its
     * position after the last delta (`npos` while new). The position fills
     * the padding after a 32-bit index, and spares the reorder step a lookup
     * in `m_slot.rank` (a cache miss) per entry
     */
    struct Entry {
        const Item *p_item;
        [[no_unique_address]] SourceIndex index;
        [[no_unique_address]] Position pos;
    };

    /**
     * Slot path state: the position of each source index (npos when free),
     * the deferred source changes, and scratch buffers that keep their
     * capacity
     */
    struct SlotState {
        std::vector<Index> rank;
        SlotDiff<Index> pending;
        Index pendingSlotCount = 0;
        std::vector<Index> stale; // positions with stale pointers
        std::vector<Index> to;    // the reorder permutation
    };

    /**
     * General path state
     */
    struct GeneralState {
        bool resortDue = false; // a re-sort is deferred to the next sync()
    };

    // Slot path: the spans for the per-index steps, the swaps, and the words
    // for the neighbour check and for folding deferred deltas
    static constexpr SlotInterest::Mask sourceInterests =
        SlotInterest::CREATED | SlotInterest::DELETED | SlotInterest::REPLACED |
        SlotInterest::MODIFIED | SlotInterest::SWAPPED | SlotInterest::WORDS |
        SlotInterest::TOUCHED_WORDS;

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
        notePeak();
        // Collect into the scratch buffer, so the old order stays readable
        // for recording
        m_batch.clear();
        m_batch.reserve(static_cast<std::size_t>(m_source->count()));
        for (auto it = m_source->begin(); it != m_source->end(); ++it) {
            if constexpr (slotPath)
                m_batch.push_back({&*it, it.index(), npos});
            else
                m_batch.push_back({&*it, {}, {}});
        }
        std::sort(
            m_batch.begin(), m_batch.end(),
            [this](const Entry &a, const Entry &b) { return less(a, b); });

        // Only the general path re-sorts with listeners (the slot path calls
        // this before it has any)
        if constexpr (!slotPath)
            if (this->hasListeners())
                recordResort();
        m_order.swap(m_batch);

        if constexpr (slotPath) {
            m_slot.rank.assign(static_cast<std::size_t>(m_source->slotCount()),
                               npos);
            setPositions(0);
        }
        setTaken(m_order.size());
    }

    /**
     * General path: records the change from the old order (`m_order`) to the
     * new one (`m_batch`), a word at a time. Below both counts, a position
     * whose item address is unchanged is modified, one with a new address
     * replaced (erase + create); the tail is created or erased.
     */
    void recordResort()
        requires(!slotPath)
    {
        const std::size_t n0 = m_order.size(), n1 = m_batch.size();
        const std::size_t lo = std::min(n0, n1), hi = std::max(n0, n1);
        for (std::size_t base = 0; base < hi; base += bitWordBits) {
            // The old pointers may dangle: they are compared as integers,
            // never read through. Branch-free, as in the filter's word loop
            BitWord same = 0;
            const std::size_t end = std::min(lo, base + bitWordBits);
            for (std::size_t q = base; q < end; ++q)
                same |=
                    BitWord{std::bit_cast<std::uintptr_t>(m_order[q].p_item) ==
                            std::bit_cast<std::uintptr_t>(m_batch[q].p_item)}
                    << (q - base);
            const BitWord kept = detail::rangeBits(base, 0, lo);
            const BitWord replaced = kept & ~same;
            const std::size_t w = base / bitWordBits;
            // Erase before create, so a replaced position coalesces
            if (const BitWord m = replaced | detail::rangeBits(base, n1, n0))
                m_diff.eraseWord(w, m);
            if (const BitWord m = replaced | detail::rangeBits(base, n0, n1))
                m_diff.createWord(w, m);
            if (const BitWord m = kept & same)
                m_diff.modifyWord(w, m);
        }
    }

    /**
     * Slot path: applies a delta of the source, which must be consistent
     * with it
     */
    void apply(const typename SourceT::Delta &d) noexcept
        requires slotPath
    {
        notePeak();
        const bool record = this->hasListeners();
        const std::size_t oldCount = m_order.size();
        if (static_cast<std::size_t>(d.slotCount) > m_slot.rank.size())
            m_slot.rank.resize(static_cast<std::size_t>(d.slotCount), npos);
        // The items are the source's, so their addresses move with it
        if (d.relocated && record) [[unlikely]]
            m_diff.relocate();

        // Swaps first: the other changes start from the swapped state
        remapSwaps(d.swapped);

        // Deleted and replaced items leave; replaced ones come back new
        m_batch.clear();
        std::size_t p0 = oldCount; // the first position taken out
        for (const auto &indices : {d.deleted, d.replaced}) {
            for (Index i : indices) {
                const std::size_t pos = takeOut(i, record);
                p0 = std::min(p0, pos);
            }
        }
        // Before any comparison: kept entries need valid pointers, and
        // taken-out ones are skipped, since their slots may be free. A swap
        // that moved an item in memory (rare: a source that swaps item
        // storage, unlike a view of a view) leaves a position with a new
        // address that no change of this view lists, so it relocates
        if (refreshPointers(d.relocated) && record && !d.relocated) [[unlikely]]
            m_diff.relocate();

        // Modified items stay where still ordered, else leave and come back
        // as kept entries (their rank still holds the old position)
        for (Index i : d.modified) {
            const auto pos = static_cast<std::size_t>(
                m_slot.rank[static_cast<std::size_t>(i)]);
            assert(pos < oldCount);
            if (record)
                m_diff.modify(static_cast<Index>(pos));
            if (keptInPlace(d, pos))
                continue;
            m_batch.push_back(m_order[pos]);
            m_order[pos].p_item = nullptr;
            p0 = std::min(p0, pos);
        }
        for (Index i : d.replaced)
            m_batch.push_back({&(*m_source)[i], i, npos});
        for (Index i : d.created) {
            assert(m_slot.rank[static_cast<std::size_t>(i)] == npos);
            m_batch.push_back({&(*m_source)[i], i, npos});
        }

        // Merge the sorted batch into the sorted rest
        compact(p0);
        std::sort(
            m_batch.begin(), m_batch.end(),
            [this](const Entry &a, const Entry &b) { return less(a, b); });
        const std::size_t kept = m_order.size();
        m_order.resize(kept + m_batch.size());
        const std::size_t p = std::min(
            p0, mergeSortedBack(std::span<Entry>(m_order), kept,
                                std::span<const Entry>(m_batch),
                                [this](const Entry &a, const Entry &b) {
                                    return less(a, b);
                                }));

        if (record)
            recordReorder(p, oldCount);
        setPositions(p);
        setTaken(m_order.size());
    }

    /**
     * Slot path: follows source swaps. Each swaps the ranks of its two
     * indices and fixes the entries' indices; the items keep their positions,
     * so nothing is compared or recorded. The entries' pointers are stale
     * until `refreshPointers()`
     */
    void remapSwaps(std::span<const typename Delta::Swap> s)
        requires slotPath
    {
        m_slot.stale.clear();
        for (const auto &[a, b] : s) {
            Index &ra = m_slot.rank[static_cast<std::size_t>(a)];
            Index &rb = m_slot.rank[static_cast<std::size_t>(b)];
            std::swap(ra, rb);
            if (ra != npos) {
                m_order[static_cast<std::size_t>(ra)].index = a;
                m_slot.stale.push_back(ra);
            }
            if (rb != npos) {
                m_order[static_cast<std::size_t>(rb)].index = b;
                m_slot.stale.push_back(rb);
            }
        }
    }

    /**
     * Slot path: takes the item of source index `i` out (a null pointer
     * marks it), records its position erased and sets its rank to `npos`
     * @returns its position
     */
    std::size_t takeOut(Index i, bool record)
        requires slotPath
    {
        Index &rank = m_slot.rank[static_cast<std::size_t>(i)];
        const auto pos = static_cast<std::size_t>(rank);
        assert(pos < m_order.size());
        m_order[pos].p_item = nullptr;
        rank = npos;
        if (record)
            m_diff.erase(static_cast<Index>(pos));
        return pos;
    }

    /**
     * Slot path: rereads pointers from source indices: all live entries when
     * `all` (a relocation), else the stale ones. Taken-out entries are
     * skipped, since their source slots may be free
     * @returns whether a stale pointer changed (always false when `all`)
     */
    bool refreshPointers(bool all)
        requires slotPath
    {
        if (all) [[unlikely]] {
            for (Entry &e : m_order)
                if (e.p_item)
                    e.p_item = &(*m_source)[e.index];
            return false;
        }
        bool changed = false;
        for (Index pos : m_slot.stale) {
            Entry &e = m_order[static_cast<std::size_t>(pos)];
            if (!e.p_item)
                continue;
            const Item *const p_item = &(*m_source)[e.index];
            changed |= p_item != e.p_item;
            e.p_item = p_item;
        }
        return changed;
    }

    /**
     * Slot path: whether the modified entry at `pos` is still ordered
     * against both neighbours, each untouched in the source delta (its
     * source index has no touched bit in `d.words`; missing words are
     * untouched). Equal neighbours count as ordered. A touched neighbour
     * may be taken out or moved, so it is never compared
     */
    bool keptInPlace(const typename SourceT::Delta &d, std::size_t pos) const
        requires slotPath
    {
        auto untouched = [&](std::size_t q) {
            const auto i = static_cast<std::size_t>(m_order[q].index);
            const std::size_t w = i / bitWordBits;
            return w >= d.words.size() ||
                   !((d.words[w].touched >> (i % bitWordBits)) & 1);
        };
        const Entry &e = m_order[pos];
        if (pos > 0 && (!untouched(pos - 1) || less(e, m_order[pos - 1])))
            return false;
        if (pos + 1 < m_order.size() &&
            (!untouched(pos + 1) || less(m_order[pos + 1], e)))
            return false;
        return true;
    }

    /**
     * Slot path: removes the taken-out entries from `p0` on, keeping the
     * order of the rest. O(count - p0)
     */
    void compact(std::size_t p0)
        requires slotPath
    {
        const auto first = m_order.begin() + static_cast<std::ptrdiff_t>(p0);
        m_order.erase(std::remove_if(first, m_order.end(),
                                     [](const Entry &e) { return !e.p_item; }),
                      m_order.end());
    }

    /**
     * Slot path: records the move from the old order to the new one in the
     * positions `[p, max(oldCount, count()))`, after the erases and
     * modifications recorded at old positions. Positions are not updated
     * yet, so a kept entry's `pos` is its old position, and a new entry's is
     * `npos`.
     */
    void recordReorder(std::size_t p, std::size_t oldCount)
        requires slotPath
    {
        const std::size_t newCount = m_order.size();
        const std::size_t size = std::max(oldCount, newCount);
        if (p >= size)
            return;
        // Old offset -> new offset of each kept entry. Positions before p
        // did not change, so every kept entry from p on came from p on
        m_slot.to.assign(size - p, npos);
        auto oldPos = [this](std::size_t q) { return m_order[q].pos; };
        for (std::size_t q = p; q < newCount; ++q)
            if (const Index o = oldPos(q); o != npos)
                m_slot.to[static_cast<std::size_t>(o) - p] =
                    static_cast<Index>(q - p);

        // The rest of the old offsets (erased, or free past oldCount) go to
        // the rest of the new ones (new entries, or free past newCount), both
        // in increasing order, so a replaced position that stays put is a
        // fixed point and needs no swap
        std::size_t o = 0;
        auto pair = [&](std::size_t t) {
            while (m_slot.to[o] != npos)
                ++o;
            m_slot.to[o] = static_cast<Index>(t);
        };
        for (std::size_t q = p; q < newCount; ++q)
            if (oldPos(q) == npos)
                pair(q - p);
        for (std::size_t q = std::max(p, newCount); q < size; ++q)
            pair(q - p);

        // SlotDiff moves the earlier records along with their items: an
        // erase lands on a new entry's position (a create there makes it
        // replaced) or past newCount
        permuteBySwaps(std::span<Index>(m_slot.to), static_cast<Index>(p),
                       [this](Index a, Index b) { m_diff.swap(a, b); });
        for (std::size_t q = p; q < newCount; ++q)
            if (oldPos(q) == npos)
                m_diff.create(static_cast<Index>(q));
    }

    /**
     * Slot path: stores the positions from `p` on, in the entries and in
     * `m_slot.rank`
     */
    void setPositions(std::size_t p)
        requires slotPath
    {
        for (std::size_t q = p; q < m_order.size(); ++q) {
            m_order[q].pos = static_cast<Index>(q);
            m_slot.rank[static_cast<std::size_t>(m_order[q].index)] =
                static_cast<Index>(q);
        }
    }

    /**
     * Keeps the largest count since the last flush; called before the count
     * changes
     */
    void notePeak() { m_peakCount = std::max(m_peakCount, count()); }

    /**
     * Sets the first `n` bits of `m_taken` and clears the rest, keeping the
     * zero padding, in O(changed words)
     */
    void setTaken(std::size_t n) {
        const std::size_t old = m_taken.size();
        // Shrinking clears the dropped bits; growing adds clear ones
        m_taken.resize(n);
        const std::span<BitWord> words = m_taken.words();
        for (std::size_t w = old / bitWordBits; w * bitWordBits < n; ++w)
            words[w] |= detail::rangeBits(w * bitWordBits, old, n);
    }

    std::shared_ptr<SourceT> m_source; // owned, so it outlives this listener
    [[no_unique_address]] Compare m_cmp;
    std::vector<Entry> m_order; // per position
    BitVector m_taken;          // the first count() bits
    SlotDiff<Index> m_diff;
    Index m_peakCount = 0; // the largest count since the last flush
    bool m_dirty = false;
    // Whether sync() ran since the last delta; a new view is about to be read
    bool m_readSinceDelta = true;
    // Scratch entries that keep their capacity: the new order on a full
    // sort, the entries to merge back on the slot path
    std::vector<Entry> m_batch;

    [[no_unique_address]] std::conditional_t<slotPath, SlotState,
                                             detail::NoSlotState> m_slot;
    [[no_unique_address]] std::conditional_t<slotPath, detail::NoGeneralState,
                                             GeneralState> m_general;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_VIEWS_ARRAYSORTVIEW
