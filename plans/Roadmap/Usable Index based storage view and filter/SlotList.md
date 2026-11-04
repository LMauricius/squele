# [Task] SlotList
Add `SlotList<TItemT, TIndexT = std::uint32_t>` in `views/slotList.hpp`: an `Original` and a `SlotViewable` whose items are added by hand. It derives only from `Listenable<Delta>` and has no `sync()`; its query is `From<SlotList>`.

- Storage: `std::deque<Slot<ItemT>>` indexed by slot, with `IndexTable` choosing slots and tracking which are taken. A deque never relocates its elements when it grows, so `Slot` needs no move support and item references stay valid.
- The destructor and `erase(i)` destroy items explicitly; the destructor walks the taken flags.
- `emplace(args...) -> IndexT`, `erase(i)`, `modify(i) -> ItemT &` (records a modification), `operator[](i) const`, `isTaken(i) const`.
- The iterator walks the storage and skips free slots.
- Every mutation is recorded in a `SlotDiff`. `flush()` notifies listeners with its `delta()`, then clears it; with no changes it notifies nobody.

Blocks:
- [Task] Filter query and SlotFilterView
