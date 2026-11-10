# [Feature] Map
A `Map<Q, Fn>` query whose view holds the result of `Fn` called on each item of its source. The results are owned values, computed once per change of their source item, so later views (a filter, a sort) read them by pointer like the items of a store.

Decisions:
- `Fn` follows the key rules of `KeyCallable` and is invocable with `const Item &`; the view's `Item` is the decayed result type.
- Results live in stable storage (`SlotChunks`), so a result keeps its address until its source item is created, replaced or erased; the view never sets `relocated`. A relocated source needs no recomputation, since the results are copies, not pointers.
- Change detection: by default, every recomputed result is reported modified, since a result may hold pointers or plain data whose pointees change unseen by `==`. `Fn` may instead be `ComparedResult{eq, fn}`, which pairs the function with a `KeyCallable` equality on results (both part of the query key); the view then skips results that compare equal to the old ones.
- Slot path: a `SlotViewable` source keeps its index space. A result sits at its source item's index, the view's taken bits are the source's, and created, modified and replaced items are recomputed while erased ones are destroyed. A swap swaps two results (no call), so a dense source gives a dense view. The view's delta is its source's delta with the same indices, minus the results found unchanged.
- General path: any other `ListViewable` source recomputes every result on each flush, reporting changes by position like the general path of `ArraySortView`.
- `Map` and `Extend` stay separate views. The parts they share (result storage, recomputation and the equality check) live in small `bits/` components that both reuse.
- One view class serves every source kind; the parts of the slot and dense paths are constrained with `requires` on the source's concepts, so the query never picks between view classes.
- Like `FilterView`, the view is eager while read every cycle, and otherwise folds deltas until `sync()`.

Useful:
- [Note] Query keys
- [Note] Pull data flow
- [Note] Deltas and Diffs
- [Note] Slot storage

Blocks:
- [Feature] Extend

- [Task] ComparedResult and result tools
  ---
  done: true
  ---
  `bits/result.hpp`: `ComparedResult{eq, fn}` (a `KeyCallable` pair with `callableEqual` and `combineCallableHash` support), and the shared pieces of `Map` and `Extend`: the result type of a plain or compared function, `ResultFunction` and `ResultOf` (the constraint and the result type of a plain or compared function), `resultFn` (the function that constructs a result), and `recomputeResult`, which recomputes a result in place and returns whether it changed (always true without `eq`). Views store results directly in `SlotChunks<SlotCell<R>>`; no wrapper. `ViewIndex` (the index type of a derived view, formerly `ArraySortView`'s `SortIndex`) moves to `core/concepts.hpp`. Tested alone (`checkResultTools`).

  Blocks:
  - [Task] Map query and MapView
  - [Task] Extend query and ExtendView
- [Task] Map query and MapView
  ---
  done: true
  ---
  `Map<Q, Fn>` in `queries/map.hpp` (equality, `std::hash`, CTAD) and `MapView` in `views/mapView.hpp`: one class for every source, whose slot path members and methods are constrained with `requires` (`SlotViewable` source), not a separate slot class. Slot path: the source's index space and taken bits, recomputation of created, modified and replaced items, destruction of erased ones, swaps of results, and the forwarded delta minus unchanged results. General path: full recomputation per flush, output by position. Eager while read every cycle, otherwise a pending diff until `sync()`.
- [Task] Map fuzzing
  ---
  done: true
  ---
  `Map` over a store, over a filter (with `ComparedResult`) and over a `SwapSource`, a filter and a sort over `Map`, and `Map` over the test-only general source (`checkMapSlot`, `checkMapGeneral`). After each round, every result must equal `fn` of its source item, and the model listener must replay the delta. A counting function checks that swaps alone call nothing and that unchanged items are never recomputed; the address rule holds for every result.
- [Task] Map docs
  ---
  done: true
  ---
  A Note on derived views (owned results, change detection and `ComparedResult`, index space), the header layout Note and `CLAUDE.md` updated with the new headers.

  Useful:
  - [Note] Derived views

# [Feature] Extend
An `Extend<Item, Args...>` query that combines one or more sources and function results into one view. Each argument is either a source query or a function; the n-th item of the view holds a pointer to the n-th item of each source and the result of each function over those items, in argument order. `count()` is the smallest source count. Without functions it zips its sources, so no separate `Zip` query is needed.

Decisions:
- Matching ignores indices: the n-th item of a source is the n-th one its iteration yields, as if all sources were iterated side by side. Positions are these ranks.
- Arguments: a `Queryable` argument is a source; any other argument is a function (`KeyCallable`). A function is called with the items of all sources as separate arguments, in source order (`fn(const A &, const B &)`). Sources and functions may come in any order. At least one source and at least two members (sources plus functions) are required; fewer do not compile.
- `Item` is the first template argument. It defaults to `std::pair` for two members and `std::tuple` for more (sources as `const T *`, results as values); a deduction guide picks the default, so `Extend{qa, qb, fn}` deduces it. A user type with named members may replace it: any movable aggregate initialized as `Item{members...}` in argument order, such as `struct Scored { const Player *player; int score; };` in `Extend<Scored, Q, Fn>{q, fn}`.
- Items are rebuilt whole, so a change at a position calls every function there again. `ComparedResult` is not accepted: an item points at its source items, so a source modification must be reported whatever the results compare to.
- Sources are told apart by listener tags: `addListener(p_l, tag)` stores a tag that `onDelta(d, tag)` receives back, so sources of one type, or one source given twice, stay distinct.
- The view always defers: a function reads every source, so nothing is rebuilt until `sync()` has pulled all of them.
- Index path (one `SlotViewable` source, or only `OrderedViewable` ones): rank `i` is index `i` in every source, so only touched positions are rebuilt. One source keeps its index space and follows its swaps by moving items; with several, a swap is reported as two replaced positions. A relocation rereads the pointers and rebuilds only the items whose pointers changed; the view never sets `relocated`.
- General path: any other mix of `ListViewable` sources rebuilds every position on each sync with a change (a source item at an unchanged address may still have changed), reporting an unchanged pointer tuple as modified and a different one as replaced.

Useful:
- [Note] Query keys
- [Note] Pull data flow
- [Note] Deltas and Diffs
- [Note] Ordered views
- [Note] Derived views

- [Task] Listener tags
  ---
  done: true
  ---
  `ListenerTag` in `core/concepts.hpp`; `Broadcaster::addListener(p_l, tag = 0)` stores it per entry and passes it to `onDelta(d, tag)` when the listener takes it, else calls `onDelta(d)`; `removeListener(p_l, tag = 0)` matches it. `Listenable` and `Listening` accept both forms. An entry grows from 24 to 32 bytes.
- [Task] Extend query and ExtendView
  ---
  done: true
  ---
  `Extend<Item, Args...>` in `queries/extend.hpp`: arguments split into sources (`Queryable`, or `Viewable` for the view's parts) and functions, the `ExtendArguments` checks, the deduction guide picking `std::pair` or `std::tuple`, aggregate construction of a custom `Item`, equality and `std::hash`. `ExtendView` in `views/extendView.hpp`: tagged listening, the deferred apply, whole-item rebuilds into `SlotChunks<SlotCell<Item>>` with a parallel pointer array, and the general path.
- [Task] Incremental Extend paths
  ---
  done: true
  ---
  The index path: per-source pending diffs folded by tag, swaps first (moved items for one source, replaced positions for several), then only the touched words (a word list, so a sync costs O(touched words)); a relocation checks every pointer. Buffers keep their capacity, so a steady state does not allocate.
- [Task] Extend fuzzing
  ---
  done: true
  ---
  `checkExtend`: two sorts zipped, a named `Row` with a function between them, sorts of different counts (index path, dense); a store, a sort and a `SwapSource` each with a counted function (index path, one source; swaps and relocations included); a store zipped with itself (two tags on one source), a filter with a store, and two `VecSource`s with a function (general path). Every item is checked against the sources walked side by side, and the model listener (now generic over the item type) replays every delta. A counting function checks that a round calls it at most once per store change.
- [Task] Extend benchmarks
  ---
  done: true
  ---
  `benchmarks/extend.cpp`. Results (1M items, Release, best of 7, pinned, one run each, not A/B): construction 9.4 ms against 3.5 ms for `Map` (items of a pointer and an int, plus the pointer array); 64 modified 3.6 us against 0.9 us; 104857 modified 1.8 ms against 0.6 ms; a read pass 1.5 ms against 1.1 ms for `Map` plus source reads by index. A zip of two sorts adds about 10 ms to the sorts' own 55 ms for 64 modifications (each shifted position is replaced and rebuilt), and reads 2.9 ms against 3.3 ms for reading both sorts by position.
- [Task] Extend docs and example
  ---
  done: true
  ---
  The Note on derived views extended with `Extend`, the Note on view lifetime and listeners (tags), the header layout Note, `CLAUDE.md`, and `examples/example3.cpp` (a map, and an extend of a sorted store with a function into a named struct).
