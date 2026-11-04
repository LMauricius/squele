# [Task] From query
---
done: true
---
Add `From<TViewT>` in `core/from.hpp`: a query that takes a view's `shared_ptr` and gives the same pointer back, standing in for an `Original` view as its query.

- Holds a non-null `std::shared_ptr<TViewT>` (asserted on construction); `View` is `TViewT`.
- `result(Ledger &)` returns the held pointer; it never constructs a view.
- Equality and `std::hash` compare the pointer (identity).

Blocks:
- [Task] SlotStore
- [Task] Ledger cache rework

Useful:
- [Note] Query keys
