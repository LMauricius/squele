#pragma once
#ifndef H_INCLUDED_SQUELE_BITS_SLOTCELL
#define H_INCLUDED_SQUELE_BITS_SLOTCELL

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace squele {

/**
 * Raw, aligned storage for one item, constructed and destroyed explicitly.
 * It has no "taken" flag (the owner tracks it), so its destructor does nothing:
 * the owner destroys taken items. It is neither copyable nor movable, since
 * copying raw bytes breaks items that are not trivially relocatable.
 * @warning Constructing a taken cell, or destroying or reading a free one, is
 * UB (not checked).
 */
template <class TItemT> class SlotCell {
  public:
    using ItemT = TItemT;

    SlotCell() = default;
    SlotCell(const SlotCell &) = delete;
    SlotCell &operator=(const SlotCell &) = delete;
    ~SlotCell() = default;

    /**
     * Constructs the item from `args`
     */
    template <class... TArgs> void construct(TArgs &&...args) {
        std::construct_at(reinterpret_cast<ItemT *>(m_storage),
                          std::forward<TArgs>(args)...);
    }

    /**
     * Destroys the item
     */
    void destroy() noexcept { std::destroy_at(&get()); }

    // Reads go through std::launder: the item was created by construct_at
    // in storage typed as bytes
    ItemT &get() { return *std::launder(reinterpret_cast<ItemT *>(m_storage)); }
    const ItemT &get() const {
        return *std::launder(reinterpret_cast<const ItemT *>(m_storage));
    }

  private:
    alignas(ItemT) std::byte m_storage[sizeof(ItemT)];
};

} // namespace squele

#endif // H_INCLUDED_SQUELE_BITS_SLOTCELL
