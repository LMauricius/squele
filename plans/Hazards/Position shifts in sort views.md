# [Hazard] Position shifts in sort views
`ArraySortView` keeps its items at dense positions. Inserting or removing an item shifts every later item by one position, and every shifted position costs work even though its item did not change: an entry move, a position update in `m_rank` (a scattered write), and on the slot path a swap in the view's own delta, which every listener replays.

Useful:
- [Note] Ordered views
- [Note] Deltas and Diffs

## Severity

- A flush costs O(n - p), where `p` is the first changed position, regardless of how few items changed. Measured on 1M items (`benchmarks/sort.cpp`, Release): a full sort takes 45 to 89 ms; 64 modified items take 19 to 24 ms with a filter listening, and one item created at the front takes 10 to 20 ms even with no listener.
- Each filter over the sort replays up to n swaps per flush, and forwards those involving a passing item; see [Hazard] Uncoalesced swaps.
- No correctness risk.

## Possible efforts

- A delta form for shifts (a rotation of a range of positions) would let a listener move its per-position state a word at a time instead of one swap per position. It is a new form, worth adding only when a benchmark shows the swaps dominate.
- A position map that tolerates gaps (positions with spare room, renumbered rarely) would make most insertions O(1) at the cost of density, which `OrderedViewable` requires.
