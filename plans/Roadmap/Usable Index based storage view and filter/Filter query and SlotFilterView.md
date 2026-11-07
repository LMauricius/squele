# [Task] Filter query and SlotFilterView
---
done: true
---
Add `Filter<TSourceQ>`, the declared `FilterView` and the implemented `SlotFilterView` in `views/filter.hpp`.

`Filter` holds a copy of the source query (a `From` for an `Original` source) and a predicate `bool (*)(const Item &)`. Equality and hash combine the source query and the pointer (hashes are mixed with `combineHash` from `bits/hash.hpp`, shared by future composite queries). `View` is `SlotFilterView` when the source view is `SlotViewable`, otherwise `FilterView`. Filters nest: a `SlotFilterView` is itself `SlotViewable`.

`FilterView` is declared only; instantiating it fails with a `static_assert` until a non-slot source exists.

`SlotFilterView` is `Dependent` on its source and uses nothing of the source besides `Listening` and the `SlotViewable` members. It reuses the source's indices: index `i` is taken when the source has it taken and the predicate passed. It stores a `std::vector<bool>` of pass flags and a pass counter (its `count`), and reads items from the source, so it owns no item copies.

It updates eagerly: the source is consistent while it flushes, so `onDelta` re-checks the delta's indices right away and records the transitions in the view's own `SlotDiff`. There is no pending diff and no copy of the source delta; the predicate runs once per source change, whoever pulls the source.

- Construction: gets the source with `ledger.getUnsynced(source)`, keeps the `shared_ptr`, calls `pull(*source)` (so the source is consistent and has nothing unflushed), checks every source item, then registers as a listener. The view starts clean.
- Dirty state: a `bool m_dirty`. `onDirty()`: if not already dirty, set it and `broadcastDirty()`. `dirty()` returns it.
- `onDelta`: for each index in `created`, `replaced` and `modified`, run the predicate; for each in `deleted`, treat it as failed. Record the transition and update the pass flag and counter.
- Transitions per index: fail to pass is created; pass to fail (including a source delete) is deleted; pass to pass forwards the source's kind (replaced or modified); fail to fail records nothing.
- `sync()`: if dirty, `pull(*source)` and clear the flag; nothing else.
- `flush()`: emits its own diff's `delta()`, as `SlotStore` does (nothing when empty).
- The destructor removes the listener from the source.

Blocks:
- [Task] Demo example

Useful:
- [Note] Pull data flow
- [Note] Query keys
- [Note] Deltas and Diffs
- [Note] View lifetime and listeners
- [Note] C++20 pitfalls
