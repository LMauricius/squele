#pragma once
#ifndef H_INCLUDED_SQUELE_VIEWS_SLOTSTORE
#define H_INCLUDED_SQUELE_VIEWS_SLOTSTORE

#include "squele/bits/slotCell.hpp"
#include "squele/bits/slotIndexPool.hpp"
#include "squele/bits/slotIterator.hpp"
#include "squele/components/broadcaster.hpp"
#include "squele/core/from.hpp"
#include "squele/deltas/slot.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

namespace squele {

/**
 * An original holding items in slots: items are added by hand at sparse
 * indices, and freed indices are reused last freed first.
 * Its query is `From<SlotStore>`. It is neither copyable nor movable
 * (listeners point to it), so keep it in a `shared_ptr`.
 * @note Mutations are recorded and sent to listeners on `flush()`.
 */
template <class TItemT, std::integral TIndexT = std::uint32_t>
class SlotStore : public Broadcaster<SlotDelta<TIndexT>> {
  public:
    using Item = TItemT;
    using Index = TIndexT;
    using Delta = SlotDelta<Index>;
    using Query = From<SlotStore>;
    using Iterator = SlotIterator<SlotStore>;

    SlotStore() = default;

    ~SlotStore() {
        // Cells cannot tell whether they hold an item; the pool can
        for (std::size_t i = 0; i < m_cells.size(); ++i)
            if (m_pool.isTaken(static_cast<Index>(i)))
                m_cells[i].destroy();
    }

    /**
     * Constructs an item from `args` in a free slot
     * @returns the item's index
     * @note Strong exception guarantee: if anything throws (including the
     * item's constructor), the index is released and nothing is recorded.
     */
    template <class... TArgs> Index emplace(TArgs &&...args) {
        const Index i = m_pool.acquire();
        bool constructed = false;
        try {
            // A new index equals the cell count, so grow by one cell. The
            // check (not the pool) decides, since a failed emplace_back
            // leaves the pool one slot ahead of the cells
            if (static_cast<std::size_t>(i) == m_cells.size())
                m_cells.emplace_back();
            m_cells[i].construct(std::forward<TArgs>(args)...);
            constructed = true;
            markDirty();
            m_diff.create(i);
        } catch (...) {
            if (constructed)
                m_cells[i].destroy();
            m_pool.release(i);
            throw;
        }
        return i;
    }

    /**
     * Destroys the item at the taken index `i` and frees the index
     * @note Strong exception guarantee.
     * @warning Erasing a free index is UB (asserted).
     */
    void erase(Index i) {
        assert(isTaken(i));
        markDirty();
        m_diff.erase(i); // the only step that can throw
        m_cells[i].destroy();
        m_pool.release(i);
    }

    /**
     * Records a modification of the item at the taken index `i`
     * @returns the item, to be modified before the next flush
     * @warning Modifying a free index is UB (asserted).
     */
    Item &modify(Index i) {
        assert(isTaken(i));
        markDirty();
        m_diff.modify(i);
        return m_cells[i].get();
    }

    /**
     * Sends the changes since the last flush to listeners; does nothing when
     * there are none.
     */
    void flush() {
        if (m_diff.empty())
            return;
        this->broadcast(m_diff.delta(activeCount(), slotCount()));
        m_diff.clear();
    }

    /**
     * @returns the item at the taken index `i`
     * @warning Reading a free index is UB (not checked).
     */
    const Item &operator[](Index i) const { return m_cells[i].get(); }

    bool isTaken(Index i) const { return m_pool.isTaken(i); }
    Index activeCount() const { return m_pool.activeCount(); }
    Index slotCount() const { return m_pool.slotCount(); }

    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, slotCount()}; }

  private:
    /**
     * Keeps the invariant "a non-empty diff implies dirty listeners": the
     * first mutation after a flush tells the listeners.
     */
    void markDirty() {
        if (m_diff.empty())
            this->broadcastDirty();
    }

    // A deque never relocates its elements when growing, so item references
    // stay valid. Never shrink_to_fit it (that needs movable elements)
    std::deque<SlotCell<Item>> m_cells;
    SlotIndexPool<Index> m_pool;
    SlotDiff<Index> m_diff;
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_VIEWS_SLOTSTORE
