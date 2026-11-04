# [Task] Filter query and SlotFilterView
Add `Filter<TSourceQ>`, the declared `FilterView` and the implemented `SlotFilterView` in `views/filter.hpp`.

`Filter` holds a copy of the source query (a `From` for an `Original` source) and a predicate `bool (*)(const Item &)`. A function pointer is used instead of the Feature's `std::function`, because a query must be comparable and hashable; stateless lambdas convert to it (with a unary `+` when the type is deduced). Equality and hash combine the source query and the pointer. `View` is `SlotFilterView` when the source view is `SlotViewable`, otherwise `FilterView`. Filters nest: a `SlotFilterView` is itself `SlotViewable`.

`FilterView` is declared only; instantiating it fails with a `static_assert` until a non-slot source exists. In C++20 the condition must depend on the template parameter, or it fires even when the class is never used.

`SlotFilterView` satisfies `SelectionOf` its source and requires `ListenerOf<SlotFilterView, SourceView>`; nothing else of the source is used besides the `SlotViewable` members. It reuses the source's indices: index `i` is taken when the source has it taken and the predicate passed. It stores a `std::vector<bool>` of pass flags and a pass counter (its `activeCount`), and reads items from the source, so it owns no item copies.

- Construction: gets the source with `ledger.get_unsynced(source)`, keeps the `shared_ptr`, registers as a listener and checks every source item.
- `onDelta`: adds the source delta to a pending `SlotDiff` (the delta dies after the call).
- `sync()`: first `pull(*source)`, so the source is synced and its changes reach the pending diff. Without this, the source could hold unflushed erases, and reading an erased slot is UB. Then it re-checks only the pending indices, clears the pending diff, and records the transitions in its own `SlotDiff`.
- Transitions per pending index: fail to pass is created; pass to fail (including a source delete) is deleted; pass to pass forwards the source's kind (replaced or modified); fail to fail records nothing.
- `flush()`: emits its own diff's `delta()`, as `SlotList` does.
- The destructor removes the listener from the source, which is still alive because the view owns it.
- `sync()` must stay cheap when nothing changed: views sharing a source each pull it, and the extra pulls flush an empty diff.

Blocks:
- [Task] Demo example
