# [Task] Viewable and Queryable concepts
Replace the virtual component bases with concepts in `core/concepts.hpp`, and replace the `View` and `Query` concepts with the Feature's ones.

- `Flushable<T>`: `t.flush()`. `Syncable<T>`: `t.sync()`. They replace the virtual bases in `components/flushable.hpp` and `components/syncable.hpp`, which are deleted.
- `components/pullable.hpp` becomes the free function `pull(v)` in `bits/pull.hpp`: it calls `v.sync()` if `v` is `Syncable`, then `v.flush()`.
- `Viewable<T>`: `Flushable<T>`; declares `T::Delta` and `T::Query`. It does not require `sync()`.
- `Queryable<T>`: declares `T::View` satisfying `Viewable`; `std::copyable<T>`; `std::equality_comparable<T>`; `std::hash<T>` is callable and returns `std::size_t`.
- The `result(Ledger &, Args &&...)` member is variadic, so the concept cannot check it; it is documented instead and checked by `Ledger` at the call site.
- Comparison across different query types is implicit: the `Ledger` buckets caches by query type, so queries of different types never meet.
- `Listening<L, S>`: `l.onDelta(const S::Delta &)`, `s.addListener(&l)` and `s.removeListener(&l)`.
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
