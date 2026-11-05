#pragma once
#ifndef H_INCLUDED_SQUELE_DELTAS_SLOT
#define H_INCLUDED_SQUELE_DELTAS_SLOT

#include "squele/bits/slotBits.hpp"

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace squele {

/**
 * The net change of one slot since the last flush. `nothing` (e.g. create then
 * erase) differs from `untouched`: the slot was recorded, but its changes
 * cancel out.
 */
enum class SlotChange : std::uint8_t {
    untouched = 0, // value-initialized by resize()
    nothing,
    created,
    deleted,
    replaced,
    modified
};

/**
 * Changes of a slot-based view since its last flush.
 * A delta is temporary: its spans view storage owned by the flushing view, so
 * it is valid only during the listener call.
 * The changes come in two forms; a listener reads the cheaper one for its work:
 * - index spans (`created`, ...), for per-index work on a few changes;
 * - bits (`touched`, `touchedWords`, `changes`), for word-level work such as
 *   updating per-slot flags 64 at a time.
 * @note Every index appears in at most one span. Spans are *unordered*; a
 * consumer that needs index order walks the touched words instead.
 */
template <std::integral TIndexT> struct SlotDelta {
    using IndexT = TIndexT;

    /**
     * An index that moved; references to its item stay valid
     */
    struct Move {
        IndexT from;
        IndexT to;
    };

    /**
     * The number of *taken* indices in the source
     */
    IndexT activeCount;

    /**
     * One past the largest index that may be taken
     */
    IndexT slotCount;

    /**
     * Indices that were free and are now taken
     */
    std::span<const IndexT> created;

    /**
     * Indices that were taken and are now free
     */
    std::span<const IndexT> deleted;

    /**
     * Indices whose item was deleted and a new one created in its place
     */
    std::span<const IndexT> replaced;

    /**
     * Indices whose item changed in place; references stay valid
     */
    std::span<const IndexT> modified;

    /**
     * Moved indices. Always empty until a source moves items
     */
    std::span<const Move> moved;

    /**
     * One bit per slot, set for each index recorded since the last flush:
     * every index in the spans above, plus those whose change is `nothing`
     */
    BitSpan touched;

    /**
     * Indices of the `touched` words with at least one set bit; unordered, each
     * once. Visiting only these keeps word-level work O(changed words).
     */
    std::span<const IndexT> touchedWords;

    /**
     * The net change per slot; meaningful only at touched indices
     */
    std::span<const SlotChange> changes;
};

/**
 * Self-sufficient accumulator of slot changes between flushes.
 * Coalesces repeated changes to the same index in O(1) and produces a
 * `SlotDelta` in O(k), for k touched indices.
 * @note Recording an impossible sequence for one index (e.g. a create after a
 * create) is UB; it is asserted.
 */
template <std::integral TIndexT> class SlotDiff {
  public:
    using IndexT = TIndexT;
    using DeltaT = SlotDelta<IndexT>;

    /**
     * Records that the free index `i` became taken
     */
    void create(IndexT i) { record(i, SlotChange::created); }

    /**
     * Records that the taken index `i` became free
     */
    void erase(IndexT i) { record(i, SlotChange::deleted); }

    /**
     * Records that the item at the taken index `i` changed in place
     */
    void modify(IndexT i) { record(i, SlotChange::modified); }

    /**
     * @returns the accumulated changes
     * @note The returned spans are valid until this diff changes
     */
    DeltaT delta(IndexT activeCount, IndexT slotCount) {
        // Fill the per-kind buffers; clear() keeps their capacity
        m_created.clear();
        m_deleted.clear();
        m_replaced.clear();
        m_modified.clear();
        // Walking the touched words yields each word's indices in ascending
        // order
        forEachTouched([&](std::size_t at) {
            const auto i = static_cast<IndexT>(at);
            switch (m_kinds[at]) {
            case SlotChange::created:
                m_created.push_back(i);
                break;
            case SlotChange::deleted:
                m_deleted.push_back(i);
                break;
            case SlotChange::replaced:
                m_replaced.push_back(i);
                break;
            case SlotChange::modified:
                m_modified.push_back(i);
                break;
            default: // SlotChange::nothing
                break;
            }
        });
        return {activeCount, slotCount, m_created, m_deleted,      m_replaced,
                m_modified,  {},        m_touched, m_touchedWords, m_kinds};
    }

    /**
     * Forgets all recorded changes, in O(k)
     */
    void clear() {
        forEachTouched(
            [&](std::size_t at) { m_kinds[at] = SlotChange::untouched; });
        const std::span<BitWord> words = m_touched.words();
        for (IndexT w : m_touchedWords)
            words[static_cast<std::size_t>(w)] = 0;
        m_touchedWords.clear();
    }

    /**
     * @returns true when nothing was recorded since the last clear()
     * @note A create followed by an erase of the same index still counts as
     * recorded; its delta is just empty
     */
    bool empty() const { return m_touchedWords.empty(); }

  private:
    /**
     * Calls `f(i)` for every touched index `i`, word by word
     */
    template <class F> void forEachTouched(F f) const {
        const std::span<const BitWord> words = BitSpan(m_touched).words();
        for (IndexT w : m_touchedWords) {
            const auto at = static_cast<std::size_t>(w);
            const std::size_t base = at * bitWordBits;
            for (BitWord word = words[at]; word; word &= word - 1)
                f(base + static_cast<std::size_t>(std::countr_zero(word)));
        }
    }

    /**
     * @returns the net change of `earlier` followed by `later`
     */
    static SlotChange coalesce(SlotChange earlier, SlotChange later) {
        switch (earlier) {
        case SlotChange::nothing:
            return later;
        case SlotChange::created:
            if (later == SlotChange::modified)
                return SlotChange::created;
            if (later == SlotChange::deleted)
                return SlotChange::nothing;
            break;
        case SlotChange::deleted:
            if (later == SlotChange::created)
                return SlotChange::replaced;
            break;
        case SlotChange::replaced:
        case SlotChange::modified:
            if (later == SlotChange::modified)
                return earlier;
            if (later == SlotChange::deleted)
                return SlotChange::deleted;
            break;
        default:
            break;
        }
        assert(!"impossible slot change sequence");
        return later;
    }

    /**
     * Records one change with the strong exception guarantee: the allocating
     * steps run before any state the delta reads is changed.
     */
    void record(IndexT i, SlotChange later) {
        const auto at = static_cast<std::size_t>(i);
        // Growing adds untouched slots only, so it changes nothing visible
        if (at >= m_kinds.size()) {
            m_kinds.resize(at + 1);
            m_touched.resize(at + 1);
        }
        SlotChange &kind = m_kinds[at];
        if (kind != SlotChange::untouched) {
            kind = coalesce(kind, later);
            return;
        }
        BitWord &word = m_touched.words()[at / bitWordBits];
        if (word == 0)
            m_touchedWords.push_back(static_cast<IndexT>(at / bitWordBits));
        word |= BitWord{1} << (at % bitWordBits);
        kind = later;
    }

    // One kind per slot, grown on demand; only touched entries are non-zero
    std::vector<SlotChange> m_kinds;
    // One bit per slot, set where m_kinds is not untouched
    BitVector m_touched;
    // Indices of the non-zero m_touched words; each appears once
    std::vector<IndexT> m_touchedWords;
    // Output buffers that delta() spans over
    std::vector<IndexT> m_created, m_deleted, m_replaced, m_modified;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_DELTAS_SLOT
