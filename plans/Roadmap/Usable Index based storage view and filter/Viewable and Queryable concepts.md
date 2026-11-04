# [Task] Viewable and Queryable concepts
Replace the virtual component bases with concepts in `core/concepts.hpp`, and replace the `View` and `Query` concepts with the Feature's ones. Each concept requires only the members its users call, so no view has to implement a no-op.

- `Flushable<T>`: `t.flush()`. `Syncable<T>`: `t.sync()`. They replace the virtual bases in `components/flushable.hpp` and `components/syncable.hpp`, which are deleted. `components/pullable.hpp` is deleted too: nothing calls `pull()`, and `Ledger::get` already syncs.
- `Viewable<T>`: `Flushable<T>`; declares `T::Delta` and `T::Query`. It does not require `sync()`.
- `Queryable<T>`: declares `T::View` satisfying `Viewable`; `std::copyable<T>` (queries are cache keys, so they are always copied); `std::equality_comparable<T>`; `std::hash<T>` is callable and returns `std::size_t`.
- The `result(Ledger &, Args &&...)` member is variadic, so the concept cannot check it; it is documented instead and checked by `Ledger` at the call site.
- Comparison across different query types is implicit: the `Ledger` buckets caches by query type, so queries of different types never meet.
- `ListenerOf<L, S>`: `l.onDelta(const S::Delta &)`, `s.addListener(&l)` and `s.removeListener(&l)`.
- `SelectionOf<T, S>`: `Viewable<T>`, `Syncable<T>` and `ListenerOf<T, S>`; a view derived from a source.
- `Original<T>`: `Viewable<T>`, not `Syncable<T>`, and `T::Query` is `From<T>` (forward-declared here). An original has no sources; a query to it is made with `From{p_view}`.

Blocks:
- [Task] Ledger cache rework
- [Task] SlotDelta, SlotDiff and SlotViewable concept
- [Task] From query

Fixes:
- [Bug] Query concept takes incomplete Ledger by value
- [Bug] Query concept misuses std::less
- [Bug] ListenerOf passes listener by value
