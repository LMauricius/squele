# [Task] SlotCell
---
done: true
---
Add `SlotCell<TItemT>` in `bits/slotCell.hpp`: raw, aligned storage for a single item that is constructed and destroyed explicitly.

- `construct(args...)` uses `std::construct_at`; `destroy()` uses `std::destroy_at`; `get()` returns the item through `std::launder`.
- No flag, an empty destructor, and deleted copy and move operations (see the Note).
- Constructing, destroying or reading in the wrong state is UB (documented, not checked).

Blocks:
- [Task] SlotStore

Useful:
- [Note] Slot storage
- [Note] C++20 pitfalls
