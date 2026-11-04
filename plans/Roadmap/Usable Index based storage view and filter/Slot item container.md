# [Task] Slot item container
Add `Slot<TItemT>` in `bits/slot.hpp`: raw, aligned storage for a single item that is constructed and destroyed explicitly. It replaces `std::optional` to save space: the flag (padded to the item's alignment) is dropped.

- `construct(args...)` uses `std::construct_at`; `destroy()` uses `std::destroy_at`; `get()` returns the item through `std::launder`.
- It holds no "taken" flag. The owner already tracks that (the `std::vector<bool>` in `IndexTable`), so a flag here would duplicate it.
- Its destructor does nothing, because the slot does not know whether it holds an item; the owner destroys taken items first. Constructing, destroying or reading in the wrong state is UB (documented, not checked).
- It is neither copyable nor movable: copying raw bytes would break items that are not trivially relocatable.

Blocks:
- [Task] SlotList
