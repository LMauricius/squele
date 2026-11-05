# [Hazard] Deque blocks for large items
---
done: true
---
`SlotStore` keeps its cells in a `std::deque`, whose block size is fixed by the standard library (512 bytes in libstdc++, at least one element). Items larger than that get one heap block each, and every item read goes through the deque's two-level indexing with a division by the element count per block.

Useful:
- [Note] Slot storage

## Severity

- For items of a few hundred bytes or more, one allocation per item: heap fragmentation and no spatial locality between neighbouring slots, so iteration and bulk filter scans miss the cache per item.
- For small items the cost is a few instructions per access; dense iteration measured about 1.2 ns per item, so this is minor there.
- No correctness risk; item references stay stable as documented.

## Possible efforts

- A chunked cell array in `bits/` (a vector of `unique_ptr<SlotCell<T>[]>` chunks of a power-of-two element count, for example 64 KB per chunk) keeps references stable, indexes with a shift and a mask, and allocates once per chunk whatever the item size.
- Chunk size could follow `sizeof(Item)`, so a chunk always holds a round number of cache lines.
- Done (see [Optimization] Chunked cell storage). Measured: dense iteration of 1M `int` items 1.1 to 0.8 ms, filter construction 1.8 to 1.2 ms, bulk deltas about 20% faster; 64K items of 1 KB: iteration 180 to 125 us, filter construction 175 to 150 us. Refilling a store of large items right after destroying one is slower in the benchmark (7.5 to 16 ms), because glibc returns the freed 64 KB chunks to the system while it keeps the freed 1 KB deque blocks in its bins; with trimming disabled, chunks fill faster (8.1 to 6.7 ms).
