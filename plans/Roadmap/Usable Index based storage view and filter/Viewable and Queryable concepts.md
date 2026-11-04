# [Task] Viewable and Queryable concepts
Replace the virtual component bases with concepts in `core/concepts.hpp`, and replace the `View` and `Query` concepts with the Feature's ones.

- `Flushable<T>`: `t.flush()`. `Syncable<T>`: `t.sync()` and `{ t.dirty() } -> bool` (true when `sync()` would change the contents). They replace the virtual bases in `components/flushable.hpp` and `components/syncable.hpp`, which are deleted.
- `components/pullable.hpp` becomes the free function `pull(v)` in `bits/pull.hpp`: it calls `v.sync()` if `v` is `Syncable` and `v.dirty()`, then `v.flush()`.
- `Viewable<T>`: `Flushable<T>`; declares `T::Delta` and `T::Query`. It does not require `sync()`.
- `Queryable<T>`: declares `T::View` satisfying `Viewable`; `std::copyable<T>`; `std::equality_comparable<T>`; `std::hash<T>` is callable and returns `std::size_t`.
- `{ q.result(l) } -> std::same_as<std::shared_ptr<typename T::View>>` with `Ledger &l`.
- Comparison across different query types is implicit: the `Ledger` buckets caches by query type, so queries of different types never meet.
- `Listening<L, S>`: `l.onDelta(const S::Delta &)`, `l.onDirty()`, `s.addListener(&l)` and `s.removeListener(&l)`.
- `Dependent<V, S>`: `Viewable<V>`, `Syncable<V>` and `Listening<V, S>`; a view derived from source `S`.
- `Original<T>`: `Viewable<T>`, not `Syncable<T>`, and `T::Query` is `From<T>` (forward-declared).

Blocks:
- [Task] Ledger cache rework
- [Task] SlotDelta, SlotDiff and SlotViewable concept
- [Task] From query

Fixes:
- [Bug] Query concept takes incomplete Ledger by value
- [Bug] Query concept misuses std::less
- [Bug] ListenerOf passes listener by value

Useful:
- [Note] Naming conventions
- [Note] Concepts over virtual bases
- [Note] Header layout
- [Note] Pull data flow
- [Note] C++20 pitfalls
