# [Task] SlotIndexPool
Rename `bits/indexTable.hpp` to `bits/slotIndexPool.hpp` and finish it as the index allocator for slot storage, with LIFO reuse of freed indices.

- State: `std::vector<bool>` of taken flags plus a `std::vector<IndexT>` stack of freed indices.
- `acquire()`: pops the free stack, or appends a new index when it is empty. O(1) amortized. When it appends, the returned index equals the old `slotCount()`, so the owner knows to grow its storage.
- `release(i)`: clears the flag and pushes `i`. Releasing a free index is UB (asserted).
- `isTaken(i)`, `activeCount()`, `slotCount()` (replacing `maxIndex()`).
- Running out of `IndexT` values is asserted in `acquire()`.
- Add the missing `<concepts>` include.

Blocks:
- [Task] SlotStore

Fixes:
- [Bug] IndexTable getUnused has no return

Useful:
- [Note] Slot storage
