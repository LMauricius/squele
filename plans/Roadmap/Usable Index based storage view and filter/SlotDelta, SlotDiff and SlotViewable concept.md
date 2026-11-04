# [Task] SlotDelta, SlotDiff and SlotViewable concept
Replace `deltas/indexable.hpp` with `deltas/slot.hpp` (holding `SlotDelta` and `SlotDiff`), and add the `SlotViewable` concept.

`SlotDelta<TIndexT>` holds `activeCount`, `slotCount` and one sorted `std::span<const IndexT>` per change kind: `created`, `deleted`, `replaced` and `modified`. `moved` is a span of `{from, to}` pairs, sorted by `from`. `slotCount` replaces `maxIndex`.

`SlotDiff<TIndexT>` coalesces changes per index:

| Earlier | Later | Result |
|---|---|---|
| (none) | any | the later change |
| create | modify | create |
| create | delete | nothing |
| delete | create | replace |
| replace | modify | replace |
| replace | delete | delete |
| modify | modify | modify |
| modify | delete | delete |

Any other pair (create after create, modify after delete, and so on) is impossible for a valid source; it is asserted.

- State: one change-kind byte per slot (grown on demand) plus a list of touched indices, so recording and coalescing are O(1).
- "Nothing" needs its own kind, distinct from "untouched": the index is still in the touched list, so a later change must not push it a second time. `delta()` skips such entries.
- `create(i)`, `erase(i)`, `modify(i)`: record one change.
- `operator+=(const SlotDelta &)`: merges a whole delta in O(its size).
- `delta(activeCount, slotCount) -> SlotDelta`: sorts the touched indices into per-kind vectors and returns spans over them.
- `clear()`, `empty()`. `clear()` resets only the touched bytes, not the whole byte array.

Moves are not recorded yet: no source moves items, so `moved` stays empty until one does. Once they exist, a move renames indices recorded before it; that is handled inside `SlotDiff` alone.

`SlotViewable<T>`: `Viewable<T>`; `T::Index` is integral; `T::Item`; `T::Delta` is `SlotDelta<T::Index>`; `isTaken(i) const`, `operator[](i) const`, `activeCount() const`, `slotCount() const`; `begin()`/`end()` iterate only taken indices. The iterator yields items, and `it.index()` returns the current index.

Blocks:
- [Task] SlotStore

Useful:
- [Note] Deltas and Diffs
