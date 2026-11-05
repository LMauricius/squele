# [Note] Slot storage
Slot-based views store items in `SlotCell<T>`: raw aligned storage without a "taken" flag. The flag lives once, as a bit in `SlotIndexPool`'s `BitVector`, which saves the padded flag that `std::optional<T>` would add to every slot.

- A `SlotCell` does not know whether it holds an item, so its destructor does nothing; the owner destroys taken items. It is neither copyable nor movable, because copying raw bytes breaks items that are not trivially relocatable.
- Slots are kept in a `std::deque`: it never relocates elements when it grows, so item references stay valid. Never call `shrink_to_fit` on it (that needs movable elements).
- Freed indices are reused last freed first, in O(1).
- If an item's constructor throws, the acquired index is released again and no change is recorded.
- Slot flags (taken, passed, touched) are stored in `BitVector` (`bits/slotBits.hpp`), not `std::vector<bool>`, because word-level code needs the words. Words are 64-byte aligned and padded to whole cache lines, and every bit past `size()` is zero; code that writes words directly must keep that invariant.
- `SlotViewable::takenBits()` exposes the taken flags, so `SlotIterator` skips 64 free slots per step and costs O(slotCount / 64 + activeCount).
