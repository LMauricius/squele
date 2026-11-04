# [Note] Header layout
Where code lives under `include/squele/`:

- `core/`: concepts, the `Ledger` and the basic queries (`From`).
- `components/`: mixin base classes for views (such as `Broadcaster`).
- `bits/`: tiny reusable components and functions (such as `SlotIndexPool`, `SlotCell`, `SlotIterator`, `combineHash` and `pull`).
- `deltas/`: change descriptions (`*Delta`) and their owning accumulators (`*Diff`).
- `views/`: concrete views and the queries that create them.
