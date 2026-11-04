# [Task] SlotList
Add `SlotList<TItemT, TIndexT = std::uint32_t>` in `views/slotList.hpp`: an `Original` and a `SlotViewable` whose items are added by hand. It derives only from `Listenable<Delta>` and has no `sync()`; its query is `From<SlotList>`.

- Storage: `std::deque<Slot<ItemT>>` indexed by slot, with `IndexTable` choosing slots and tracking which are taken.
- `emplace(args...) -> IndexT`: when `IndexTable` returns a new index, `emplace_back()` a slot first. If the item's constructor throws, release the index again and record nothing.
- `erase(i)` destroys the item; the destructor walks the taken flags and destroys the remaining items.
- `modify(i) -> ItemT &` (records a modification), `operator[](i) const`, `isTaken(i) const`, `activeCount()`, `slotCount()`.
- The iterator walks the storage and skips free slots.
- Every mutation is recorded in a `SlotDiff`. `flush()` notifies listeners with its `delta()`, then clears it.
- Not copyable or movable.

Blocks:
- [Task] Filter query and SlotFilterView

Useful:
- [Note] Slot storage
- [Note] Deltas and Diffs
- [Note] View lifetime and listeners
