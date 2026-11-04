# [Feature] Usable Index based storage view and filter
Implement the concepts and classes needed for the most basic demo
## concept `Viewable`
- Has a defined `Delta` class, whose instance is sent to listeners on `flush()` to describe changes since the last flush
- Has a typedef `Query`
## concept `Queryable`
- Comparable with any Queryable type, hashable
- Has a typedef `View`
- method `result(...)` that returns a shared_ptr to a `View`. Arguments are passed to the constructor, if the view is created
- Can be added to a Ledger, to cache views (never creates a new view then, just keyed by queries)
## concept `SlotViewable`
- Extension of `View` whose elements can be accessed by indexes
- `Delta`s contain the following info:
	- Created indices
  - Deleted indices
  - Replaced  (delete+create at same index counts as replace)
  - Modified indices (valid references kept)
  - Moved indices (valid references kept)
- The items can be accessed by an index
- Deleting items can make indices sparse. Check if an index is used by `isTaken() const`
- If possible, freed indices are reused
- It is iterable, with iterator skipping freed indices
## concept `Original`
- Also a `Viewable`
- ...but has no dependencies, and it's instead modifiable.
- Not a Listener
- It is its own query (that returns itself, as a reference)

## class `SlotList`
- It's also a `SlotViewable` and class `Origin<>`
- Items manually added

## class `Filter`, `FilterView`  and `SlotFilterView`
- `Filter`<> is a query, constructed from another query and a filter std::function<bool(Item)>
- If the source is  SlotViewable, the `View` of this query is SlotFilterView
- Re-checks only those source items that have changed on update

## Tasks

- [Task] Viewable and Queryable concepts
- [Task] Concept-based Listenable
- [Task] From query
- [Task] Ledger cache rework
- [Task] SlotDelta, SlotDiff and SlotViewable concept
- [Task] IndexTable slot allocator
- [Task] Slot item container
- [Task] SlotList
- [Task] Filter query and SlotFilterView
- [Task] Demo example
