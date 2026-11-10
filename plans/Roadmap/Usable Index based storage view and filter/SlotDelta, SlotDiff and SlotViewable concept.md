# [Task] SlotDelta, SlotDiff and SlotViewable concept
---
done: true
---
Replace `deltas/indexable.hpp` with `deltas/index.hpp` (holding `SlotDelta` and `SlotDiff`), and add the `SlotViewable` concept.

`SlotDelta<TIndexT>` holds `count`, `slotCount` and one `std::span<const IndexT>` per change kind: `created`, `deleted`, `replaced` and `modified`. `moved` is a span of `{from, to}` pairs. `slotCount` replaces `maxIndex`. Spans are *unordered* (touch order): index order helps no planned consumer (a sort view orders by key, not by index), so no view pays O(k log k) per flush. A consumer that needs order sorts its own copy.

`SlotDiff<TIndexT>` coalesces changes per index:

| Earlier | Later | Result |
|---|---|---|
| (none) or nothing | any | the later change |
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
- `delta(count, slotCount) -> SlotDelta`: one O(k) pass over the touched list that appends each index to its per-kind vector, and returns spans over them. The per-kind vectors keep their capacity, so steady-state flushing does not allocate.
- `clear()`, `empty()`. `clear()` resets only the touched bytes, not the whole byte array.
- No `operator+=(const SlotDelta &)`: listeners consume a delta during `onDelta` (see the Note); add it only when a view must defer a delta.

Moves are not recorded yet: no source moves items, so `moved` stays empty until one does. Once they exist, a move renames indices recorded before it; that is handled inside `SlotDiff` alone.

`SlotViewable<T>`: `Viewable<T>`; `T::Index` is integral; `T::Item`; `T::Delta` is `SlotDelta<T::Index>`; `isTaken(i) const`, `operator[](i) const`, `count() const`, `slotCount() const`; `begin()`/`end()` iterate only taken indices. The iterator yields items, and `it.index()` returns the current index.

Blocks:
- [Task] SlotStore

Useful:
- [Note] Deltas and Diffs
