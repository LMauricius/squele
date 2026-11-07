# [Feature] Map
A `Map<Q, Fn, Eq>` query whose view holds the result of `Fn` called on each item of its source. The results are owned values, computed once per change of their source item, so later views (a filter, a sort) read them by pointer like the items of a store.

Decisions:
- `Fn` follows the key rules of `KeyCallable` and is invocable with `const Item &`; the view's `Item` is the decayed result type.
- Results live in stable storage (`SlotChunks`), so a result keeps its address until its source item is created, replaced or erased; the view never sets `relocated`. A relocated source needs no recomputation, since the results are copies, not pointers.
- Change detection: by default, every recomputed result is reported modified, since a result may hold pointers or plain data whose pointees change unseen by `==`. The optional `Eq` (a `KeyCallable` equality on results, part of the query key) lets the view skip results that compare equal to the old ones.
- Slot path: a `SlotViewable` source keeps its index space. A result sits at its source item's index, the view's taken bits are the source's, and created, modified and replaced items are recomputed while erased ones are destroyed. A swap swaps two results (no call), so a dense source gives a dense view. The view's delta is its source's delta with the same indices, minus the results `Eq` found unchanged.
- General path: any other `ListViewable` source recomputes every result on each flush, reporting changes by position like the general path of `ArraySortView`.
- `Map` and `Extend` stay separate views. The parts they share (result storage, recomputation and the equality check) live in small `bits/` components that both reuse.
- Like `SlotFilterView`, the view is eager while read every cycle, and otherwise folds deltas until `sync()`.

Useful:
- [Note] Query keys
- [Note] Pull data flow
- [Note] Deltas and Diffs
- [Note] Slot storage

Blocks:
- [Feature] Extend

# [Feature] Extend
An `Extend<Item, Args...>` query that combines one or more sources and function results into one view. Each argument is either a source query or a function; the n-th item of the view holds a pointer to the n-th item of each source and the result of each function over those items, in argument order. `count()` is the smallest source count. Without functions it zips its sources, so no separate `Zip` query is needed.

Decisions:
- Matching ignores indices: the n-th item of a source is the n-th one its iteration yields, as if all sources were iterated side by side. The view is dense; its positions are these ranks.
- Arguments: a `Queryable` argument is a source; any other argument is a function (`KeyCallable`). A function is called with the items of all sources as separate arguments, in source order (`fn(const A &, const B &)`). Sources and functions may come in any order. At least one source and at least two members (sources plus functions) are required; fewer do not compile.
- `Item` is the first template argument. It defaults to `std::pair` for two members and `std::tuple` for more (sources as `const T *`, results as values); a deduction guide picks the default, so `Extend{qa, qb, fn}` deduces it. A user type with named members may replace it: any aggregate initialized as `Item{members...}` in argument order, such as `struct Scored { const Player *player; int score; };` in `Extend<Scored, Q, Fn>{q, fn}`.
- One view instead of a zip plus a map per function: there is one listener hop and one delta, and the results sit next to the source pointers in the same entry, so reading an item touches one place.
- Results follow the rules of `Map`: owned values, recomputed when any of their source items is created, modified or replaced. Each function may carry its own optional equality through the wrapper argument `ComparedResult{eq, fn}`; without one, every recomputed result is reported modified.
- Dense path: when every source is `OrderedViewable` (dense), rank `i` is index `i` in every source. A change at `i` in any source recomputes and reports position `i`; the tail beyond the new smallest count is created or erased. A swap in one source is not a swap of combined items, so it is reported as two replaced positions. Since entries hold source pointers, a `relocated` source makes the view reread them (no call) and forward the flag.
- One source: there is nothing to match, so the view keeps its source's index space and follows swaps by swapping entries, like `Map`.
- General path: any other mix of `ListViewable` sources recombines on each flush, recomputing only the positions whose pointer tuple changed (the same tuple is reported modified, a different one replaced).
- The view is dense (with several sources), so a filter over it keeps its positions, and a sort over it can order by any member.

Useful:
- [Note] Query keys
- [Note] Pull data flow
- [Note] Deltas and Diffs
- [Note] Ordered views
