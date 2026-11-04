# [Note] Naming conventions
Names say what a thing is and what it is used for, and follow one scheme everywhere.

- **Concepts are adjectives** describing what a type is or can do: `Flushable`, `Syncable`, `Viewable`, `Queryable`, `SlotViewable`, `Original` (has no sources and is modified by hand), `Dependent<V, S>` (derived from source `S`), `Listening<L, S>` (receives `S`'s deltas).
- **Classes are nouns** naming their role: `Ledger` (keeps views per query), `Broadcaster` (sends deltas to listeners), `SlotIndexPool` (hands out and reclaims slot indices), `SlotCell` (raw storage for one item), `SlotStore` (an original holding items in slots).
- **Queries and their views pair up**: the query `X` creates `XView`; when the source is slot-based, it creates `SlotXView` instead (`Filter`, `FilterView`, `SlotFilterView`). A query that wraps an existing view reads as a preposition: `From{p_view}`.
- **Change types**: a `*Delta` is a temporary description of changes, a `*Diff` owns accumulated changes (see the Deltas and Diffs Note).
- **The `Slot` prefix** marks everything built around sparse, reusable indices.
- Free functions are verbs: `pull(view)`.
