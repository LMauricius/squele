# [Hazard] Uncoalesced swaps
`SlotDiff` keeps every swap recorded between two flushes as its own entry, and every listener replays the list in order. Swapping the same pair back and forth, or reordering many items through swaps (a sort), grows the list with each call.

`ArraySortView` records at most one swap per moved position for each source delta it applies, but its own diff keeps the swaps of every delta applied since its last flush, and a shift moves every later position (see [Hazard] Position shifts in sort views).

Useful:
- [Note] Deltas and Diffs
- [Note] Ordered views

## Severity

- Memory between flushes is O(swaps), not O(touched slots).
- Each flush costs O(swaps) per listening filter, and again at every filter level, since each filter forwards the swaps of its source that involve a passing item.
- No correctness risk: the list is exact.

## Possible efforts

- A permutation coalescer (a map from index to index) would bound the list by the touched indices, at the cost of a hash lookup per swap.
