# [Task] SlotStore
---
done: true
---
Add `SlotStore<TItemT, TIndexT = std::uint32_t>` in `views/slotStore.hpp`: an `Original` and a `SlotViewable` whose items are added by hand. It derives only from `Broadcaster<Delta>` and has no `sync()`; its query is `From<SlotStore>`.

- Storage: `std::deque<SlotCell<ItemT>>` indexed by slot, with `SlotIndexPool` choosing slots and tracking which are taken.
- `emplace(args...) -> IndexT`: when `SlotIndexPool` returns a new index, `emplace_back()` a slot first. If the item's constructor throws, release the index again and record nothing.
- `erase(i)` destroys the item; the destructor walks the taken flags and destroys the remaining items.
- `modify(i) -> ItemT &` (records a modification), `operator[](i) const`, `isTaken(i) const`, `activeCount()`, `slotCount()`.
- The iterator walks the storage and skips free slots. It is `SlotIterator<View>` in `bits/slotIterator.hpp`, shared with `SlotFilterView`: it needs only `isTaken()`, `operator[]` and `slotCount()`.
- Every mutation is recorded in a `SlotDiff`; when the diff was empty before the mutation, `broadcastDirty()` first (see the Pull data flow Note). `flush()` does nothing when the diff is empty; otherwise it notifies listeners with its `delta()`, then clears it.
- Not copyable or movable.

Blocks:
- [Task] Filter query and SlotFilterView

Useful:
- [Note] Slot storage
- [Note] Deltas and Diffs
- [Note] View lifetime and listeners
- [Note] Pull data flow
