# [Hazard] Rebuilds on shifted positions in extends
An `ExtendView` over several dense sources (sorts) matches position `i` of each. One insertion near the front of a sort shifts every later position, and a swap in one source pairs different items at both positions, so the extend rebuilds every shifted item and calls every function there again, although no source item changed.

Useful:
- [Note] Derived views
- [Note] Ordered views

## Severity

- A flush costs O(n - p) rebuilds and function calls, where `p` is the first shifted position. Measured on 1M items (`benchmarks/extend.cpp`, Release): a zip of two sorts adds about 10 ms to the 55 ms the sorts spend on 64 modified items, without functions; each function adds one call per shifted position.
- Every shifted position is reported replaced, so listeners over the extend redo their work for all of them.
- No correctness risk. With one source the extend follows swaps by moving items, so it does not pay this.

## Possible efforts

- When every source reports the same swap (sources sorted the same way), move the combined items instead of rebuilding them.
- A shift delta form (see [Hazard] Position shifts in sort views) would let an extend shift its items a block at a time, when all sources shift alike.
