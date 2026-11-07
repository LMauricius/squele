# [Note] Header layout
Where code lives under `include/squele/`:

- `core/`: concepts, the `Ledger` and the basic queries (`From`).
- `components/`: mixin base classes for views (such as `Broadcaster`).
- `bits/`: tiny reusable components and functions (such as `SlotIndexPool`, `SlotCell`, `SlotIterator`, `combineHash` and `pull`).
- `deltas/`: change descriptions (`*Delta`) and their owning accumulators (`*Diff`).
- `queries/`: queries that create views (such as `Filter` and `Sort`), one per header.
- `views/`: concrete views (such as `SlotStore`, `FilterView` and `ArraySortView`).

A query header forward-declares its views and includes their headers at its end, so including the query alone is enough to call `result()`. A view header includes its query header first. Either header may be included first: the include guards break the cycle, and the query is always complete before its views.
