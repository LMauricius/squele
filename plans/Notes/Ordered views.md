# [Note] Ordered views
An `OrderedViewable` is a slot view whose indices are dense positions and whose index order is sorted order. `ArraySortView` (the view of `Sort`) is the first one.

- Dense: `slotCount() == count()`, and every index below it is taken. A filter over an ordered view keeps its order (it reuses the positions) but is not dense, so it is not ordered itself.
- Sorted by `compare()`, a strict weak order. The order of equal items is unspecified: the sort is not stable, so an item never moves past its equals and modifying an item among equals often moves nothing. A fixed order among equals belongs in the comparator, as a tie-break key.
- A sort view owns no item copies. Each position holds a pointer to its source item and compares through it; on the slot path it also holds the source index and its position at the last delta, and `m_rank` maps a source index to its position.

`ArraySortView` has two paths, picked at compile time:

- General: any `ListViewable` source. Its delta is not understood, so every source flush re-sorts everything. Positions are compared by item address: the same address is reported modified, a new one replaced, and `relocated` is never set.
- Slot: a `SlotViewable` source. Source swaps only remap indices (no comparison and no output, unless the source swapped item storage, which relocates). Changed items are taken out, sorted alone and merged back (`mergeSortedBack`: a binary search per item for a small batch, a linear merge for a large one). A modified item still ordered against untouched neighbours stays in place.

The slot path reports its reordering as swaps (`permuteBySwaps`, at most one per moved position), so listeners that keep per-position state follow it without recomputing: a filter swaps its pass flags, and a sort over the sort remaps its indices. Erases and modifications are recorded at old positions before the swaps and creates at new positions after them; `SlotDiff` moves the earlier records along with their items. A view shrinks only after its delta: the delta's `slotCount` is the largest count since the last flush, so it covers the erased last positions.

Positions shift on every insertion and removal, so one change near the front swaps every later position; see [Hazard] Position shifts in sort views.
