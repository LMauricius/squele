# [Task] Filter query and SlotFilterView
Add `Filter<TSourceQ>`, the declared `FilterView` and the implemented `SlotFilterView` in `views/filter.hpp`.

`Filter` holds a copy of the source query (a `From` for an `Original` source) and a predicate `bool (*)(const Item &)`. A function pointer is used instead of the Feature's `std::function`, because a query must be comparable and hashable; stateless lambdas convert to it. Equality and hash combine the source query and the pointer. `View` is `SlotFilterView` when the source view is `SlotViewable`, otherwise `FilterView`.

`FilterView` is declared only; instantiating it fails with a `static_assert` until a non-slot source exists.

`SlotFilterView` satisfies `SelectionOf` its source and requires `ListenerOf<SlotFilterView, SourceView>`; nothing else of the source is used besides the `SlotViewable` members. It reuses the source's indices: index `i` is taken when the source has it taken and the predicate passed. It stores one pass flag per index and reads items from the source, so it owns no item copies.

- Construction: gets the source with `ledger.get_unsynced(source)`, keeps the `shared_ptr`, registers as a listener and checks every source item.
- `onDelta`: adds the source delta to a pending `SlotDiff` (the delta dies after the call).
- `sync()`: re-checks only the pending indices, clears the pending diff, and records the transitions in its own `SlotDiff` (fail to pass is created, pass to fail is deleted, pass to pass forwards replaced or modified).
- `flush()`: emits its own diff's `delta()`, as `SlotList` does.
- The destructor removes the listener from the source.

Blocks:
- [Task] Demo example
