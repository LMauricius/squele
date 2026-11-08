# [Note] Derived views
A derived view owns values computed from its source items, instead of pointing at them. `MapView` (the view of `Map`) and `ExtendView` (the view of `Extend`) are the two so far.

- Results live in stable cells (`SlotChunks<SlotCell<R>>`), so a result keeps its address until its source item is created, replaced or erased. A derived view never sets `relocated`: a relocated source moves its items, not the copies computed from them.
- A result is recomputed only when its source item is created, modified or replaced. A source swap swaps two results (or moves one into a free cell) without a call, so results must be movable.
- Change detection: a plain function reports every recomputed result as modified, since `==` may miss changes behind pointers or other data a result refers to. `ComparedResult{eq, fn}` adds an equality on results; a recomputed result equal to the old one is then not reported. Both callables are part of the query key.
- Index space: over a `SlotViewable` source, the view keeps the source's indices (a result sits at its item's index), so a dense source gives a dense view and a filter over the view lines up with the source. Over any other `ListViewable` source, results sit at dense positions in iteration order, and every source flush recomputes all of them.
- The view keeps its own taken bits: while it defers, its source's bits already describe changes it has not applied.

`bits/result.hpp` holds the pieces shared by derived views: `ComparedResult`, `ResultFunction` and `ResultOf` (the constraint and the result type), `resultFn` (the function that constructs a result) and `recomputeResult` (recompute in place, report whether it changed).

## Extend

`ExtendView` combines several sources and function results by position. Its items hold one pointer per source and one result per function, in argument order, as `std::pair`, `std::tuple` or a named aggregate.

- Matching ignores indices: the n-th item of each source is the n-th one its iteration yields. `count()` is the smallest source count; positions are a prefix, except with one sparse source, and the slot count never shrinks.
- An item is built whole (`Item{members...}`), since C++20 offers no member access by position for a named aggregate. So a change at a position calls every function again; `ComparedResult` is not accepted, because a source modification always changes what the item points at and must be reported anyway.
- It always defers: each source's delta is folded into that source's pending diff (sources are told apart by their listener tag), and `sync()` pulls every source before any function runs, since a function reads all of them.
- Index path (one `SlotViewable` source, or only `OrderedViewable` ones): position `i` is index `i` in every source, so only the touched words are visited. One source keeps its index space and follows swaps by moving items (no call when the pointer did not change); with several, a swap replaces both positions. A relocation rereads every pointer and rebuilds only the items whose pointers changed.
- General path (any other mix): every sync with a change walks all sources side by side and rebuilds every position; a changed pointer tuple is reported replaced, an unchanged one modified.
