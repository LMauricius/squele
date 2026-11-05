# [Note] Slot storage
Slot-based views store items in `SlotCell<T>`: raw aligned storage without a "taken" flag. The flag lives once, as a bit in `SlotIndexPool`'s `BitVector`, which saves the padded flag that `std::optional<T>` would add to every slot.

- A `SlotCell` does not know whether it holds an item, so its destructor does nothing; the owner destroys taken items. It is neither copyable nor movable, because copying raw bytes breaks items that are not trivially relocatable.
- Slots are kept in `SlotChunks` (`bits/slotChunks.hpp`): fixed chunks of a power-of-two number of cells within 64 KB (at least one cell), which never move, so item references stay valid. Indexing is a shift and a mask, and large items share allocations, unlike `std::deque`, whose libstdc++ blocks hold a single item above 512 bytes. Chunks are never released before the store dies, and their cells are default-initialized raw storage.
- Freed indices are reused last freed first, in O(1).
- If an item's constructor throws, the acquired index is released again and no change is recorded.
- Slot flags (taken, passed) are stored in `BitVector` (`bits/slotBits.hpp`), not `std::vector<bool>`, because word-level code needs the words. Words are 64-byte aligned and padded to whole cache lines, and every bit past `size()` is zero; code that writes words directly must keep that invariant. Change bookkeeping (`SlotDiff`) uses its own blocks of four words per 64 slots (see [Note] Deltas and Diffs), half a byte per slot.
- `SlotViewable::takenBits()` exposes the taken flags, so `SlotIterator` skips 64 free slots per step and costs O(slotCount / 64 + activeCount).
