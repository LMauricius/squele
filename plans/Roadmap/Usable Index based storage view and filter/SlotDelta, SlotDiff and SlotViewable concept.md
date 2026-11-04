# [Task] SlotDelta, SlotDiff and SlotViewable concept
Replace `deltas/indexable.hpp` with `deltas/slot.hpp` (holding `SlotDelta` and `SlotDiff`), and add the `SlotViewable` concept.

`SlotDelta<TIndexT>` holds `activeCount`, `maxIndex` and one sorted `std::span<const IndexT>` per change kind: `created`, `deleted`, `replaced` and `modified`. `moved` is a span of `{from, to}` pairs, sorted by `from`.

A `SlotDelta` is temporary: it only views storage owned by someone else and is valid during the listener call.

`SlotDiff<TIndexT>` is self-sufficient: it owns the changes accumulated between flushes and coalesces them per index:

| Earlier | Later | Result |
|---|---|---|
| create | delete | nothing |
| create | modify | create |
| delete | create | replace |
| modify | delete | delete |
| replace | delete | delete |

- State: one change-kind byte per slot plus a list of touched indices, so recording and coalescing are O(1).
- `create(i)`, `erase(i)`, `modify(i)`: record one change.
- `operator+=(const SlotDelta &)`: merges a whole delta in O(its size).
- `delta(activeCount, maxIndex) -> SlotDelta`: sorts the touched indices into per-kind vectors and returns spans over them, valid until the diff changes.
- `clear()`, `empty()`. Vectors keep their capacity, so there is no allocation in steady state.

Moves are not recorded yet: no source moves items, so `moved` stays empty until one does. Once they exist, a move renames indices recorded before it; that is handled inside `SlotDiff` alone.

`SlotViewable<T>`: `Viewable<T>`; `T::Index` is integral; `T::Item`; `T::Delta` is `SlotDelta<T::Index>`; `isTaken(i) const`, `operator[](i) const`, `maxIndex() const`; `begin()`/`end()` iterate only taken indices.

Blocks:
- [Task] SlotList
