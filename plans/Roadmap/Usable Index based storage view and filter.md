# [Feature] Usable Index based storage view and filter
---
done: true
---
Implement the concepts and classes needed for the most basic demo. Concepts are preferred to virtual base classes, and each concept requires only the members its users call, so no class implements a no-op.
## concepts `Flushable` and `Syncable`
- `Flushable`: has `flush()`, which sends the changes since the last flush to listeners
- `Syncable`: has `sync()`, which brings the contents up to date with the sources
## concept `Viewable`
- `Flushable`, but not necessarily `Syncable`
- Has a defined `Delta` class, whose instance is sent to listeners on `flush()` to describe changes since the last flush. A delta is temporary: it is valid only during the listener call
- Has a typedef `Query`
## concept `Queryable`
- Copyable, equality comparable and hashable (queries are cache keys)
- Has a typedef `View`
- Method `result(Ledger &, ...)` that returns a `shared_ptr` to a `View`. The remaining arguments are passed to the constructor, if the view is created
- Can be passed to a `Ledger`, which caches views keyed by queries and creates a view only on a cache miss. `Ledger::collect()` releases views nobody else uses
## concepts `Listening` and `Dependent`
- `Listening<L, S>`: `L` has `onDelta(const S::Delta &)` and can be added to `S`; it inherits nothing
- `Dependent<V, S>`: `V` is a `Viewable` and `Syncable` view that is `Listening` to its source `S`
- The `Broadcaster<Delta>` mixin stores listeners and sends them deltas, without virtual calls
## concept `SlotViewable`
- Extension of `Viewable` whose elements can be accessed by indices
- `Delta`s (`SlotDelta`) contain the following info:
  - Created indices
  - Deleted indices
  - Replaced indices (delete+create at the same index counts as replace)
  - Modified indices (valid references kept)
  - Moved indices (valid references kept)
- Changes are accumulated between flushes in a self-sufficient `SlotDiff`
- Deleting items can make indices sparse. Check if an index is used by `isTaken() const`
- Freed indices are reused, last freed first
- It is iterable, with the iterator skipping freed indices
## concept `Original`
- Also a `Viewable`
- ...but has no dependencies (not `Syncable`), and it's instead modifiable
- Not a listener
- Its query is `From<T>`, a copyable query that holds the original's `shared_ptr` and returns it
## class `SlotStore`
- An `Original` and a `SlotViewable`
- Items manually added
- Items live in `SlotCell`s (raw storage without a flag, to save space); taken slots are tracked by a `std::vector<bool>`
## class `Filter`, `FilterView` and `SlotFilterView`
- `Filter<>` is a query, constructed from another query and a predicate function pointer `bool (*)(const Item &)` (a function pointer, unlike `std::function`, is comparable and hashable)
- If the source is `SlotViewable`, the `View` of this query is `SlotFilterView`, which reuses the source's indices
- `FilterView`, for other sources, is only declared for now
- Re-checks only those source items that have changed on update

## Tasks

- [Task] Viewable and Queryable concepts
- [Task] Broadcaster mixin
- [Task] From query
- [Task] Ledger cache rework
- [Task] SlotDelta, SlotDiff and SlotViewable concept
- [Task] SlotIndexPool
- [Task] SlotCell
- [Task] SlotStore
- [Task] Filter query and SlotFilterView
- [Task] Demo example

Useful:
- [Note] Naming conventions
- [Note] Concepts over virtual bases
