# [Task] Filter query and SlotFilterView
Add `Filter<TSourceQ>`, the declared `FilterView` and the implemented `SlotFilterView` in `views/filter.hpp`.

`Filter` holds a copy of the source query (a `From` for an `Original` source) and a predicate `bool (*)(const Item &)`. Equality and hash combine the source query and the pointer. `View` is `SlotFilterView` when the source view is `SlotViewable`, otherwise `FilterView`. Filters nest: a `SlotFilterView` is itself `SlotViewable`.

`FilterView` is declared only; instantiating it fails with a `static_assert` until a non-slot source exists.

`SlotFilterView` satisfies `SelectionOf` its source and uses nothing of the source besides `ListenerOf` and the `SlotViewable` members. It reuses the source's indices: index `i` is taken when the source has it taken and the predicate passed. It stores a `std::vector<bool>` of pass flags and a pass counter (its `activeCount`), and reads items from the source, so it owns no item copies.

- Construction: gets the source with `ledger.get_unsynced(source)`, keeps the `shared_ptr`, registers as a listener and checks every source item.
- `onDelta`: adds the source delta to a pending `SlotDiff`.
- `sync()`: first `pull(*source)`, then re-checks only the pending indices, clears the pending diff, and records the transitions in its own `SlotDiff`.
- Transitions per pending index: fail to pass is created; pass to fail (including a source delete) is deleted; pass to pass forwards the source's kind (replaced or modified); fail to fail records nothing.
- `flush()`: emits its own diff's `delta()`, as `SlotList` does.
- The destructor removes the listener from the source.

Blocks:
- [Task] Demo example

Useful:
- [Note] Pull data flow
- [Note] Query keys
- [Note] Deltas and Diffs
- [Note] View lifetime and listeners
- [Note] C++20 pitfalls
