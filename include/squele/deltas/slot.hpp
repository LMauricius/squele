#pragma once
#ifndef H_INCLUDED_SQUELE_DELTAS_SLOT
#define H_INCLUDED_SQUELE_DELTAS_SLOT

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace squele {

/**
 * Changes of a slot-based view since its last flush.
 * A delta is temporary: its spans view storage owned by the flushing view, so
 * it is valid only during the listener call.
 * @note Every index appears in at most one span. Spans are *unordered* (touch
 * order); a consumer that needs index order sorts its own copy.
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
    void create(IndexT i) { record(i, Kind::created); }

    /**
     * Records that the taken index `i` became free
     */
    void erase(IndexT i) { record(i, Kind::deleted); }

    /**
     * Records that the item at the taken index `i` changed in place
     */
    void modify(IndexT i) { record(i, Kind::modified); }

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
        for (IndexT i : m_touched) {
            switch (m_kinds[static_cast<std::size_t>(i)]) {
            case Kind::created:
                m_created.push_back(i);
                break;
            case Kind::deleted:
                m_deleted.push_back(i);
                break;
            case Kind::replaced:
                m_replaced.push_back(i);
                break;
            case Kind::modified:
                m_modified.push_back(i);
                break;
            default: // Kind::nothing; untouched is never in m_touched
                break;
            }
        }
        return {activeCount, slotCount,  m_created, m_deleted,
                m_replaced,  m_modified, {}};
    }

    /**
     * Forgets all recorded changes, in O(k)
     */
    void clear() {
        for (IndexT i : m_touched)
            m_kinds[static_cast<std::size_t>(i)] = Kind::untouched;
        m_touched.clear();
    }

    /**
     * @returns true when nothing was recorded since the last clear()
     * @note A create followed by an erase of the same index still counts as
     * recorded; its delta is just empty
     */
    bool empty() const { return m_touched.empty(); }

  private:
    /**
     * The net change of one index. `nothing` (e.g. create then erase) differs
     * from `untouched`: the index is already in m_touched, so it must not be
     * pushed again.
     */
    enum class Kind : std::uint8_t {
        untouched = 0, // value-initialized by resize()
        nothing,
        created,
        deleted,
        replaced,
        modified
    };

    /**
     * @returns the net change of `earlier` followed by `later`
     */
    static Kind coalesce(Kind earlier, Kind later) {
        switch (earlier) {
        case Kind::nothing:
            return later;
        case Kind::created:
            if (later == Kind::modified)
                return Kind::created;
            if (later == Kind::deleted)
                return Kind::nothing;
            break;
        case Kind::deleted:
            if (later == Kind::created)
                return Kind::replaced;
            break;
        case Kind::replaced:
        case Kind::modified:
            if (later == Kind::modified)
                return earlier;
            if (later == Kind::deleted)
                return Kind::deleted;
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
    void record(IndexT i, Kind later) {
        const auto at = static_cast<std::size_t>(i);
        if (at >= m_kinds.size())
            m_kinds.resize(at + 1);
        Kind &kind = m_kinds[at];
        if (kind == Kind::untouched) {
            m_touched.push_back(i);
            kind = later;
        } else {
            kind = coalesce(kind, later);
        }
    }

    // One kind per slot, grown on demand; only touched entries are non-zero
    std::vector<Kind> m_kinds;
    // Touched indices in touch order; each appears once
    std::vector<IndexT> m_touched;
    // Output buffers that delta() spans over
    std::vector<IndexT> m_created, m_deleted, m_replaced, m_modified;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_DELTAS_SLOT
