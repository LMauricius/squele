# [Optimization] Chunked cell storage
---
done: true
---
Replace the `std::deque` of `SlotCell`s in `SlotStore` with a small chunked array in `bits/`: a vector of chunks, each a power-of-two number of cells, so that

- item references stay stable (a chunk never moves);
- indexing is a shift and a mask, instead of the deque's division by a block size that depends on the item size;
- chunks are large (64 KB by default) whatever the item size, so items of several hundred bytes no longer get one allocation each and neighbouring slots share cache lines.

Decisions:
- Chunk size is a byte budget (`TChunkBytesT = 65536`), rounded to a power of two of cells, at least one cell; a `SlotStore` passes nothing.
- The chunk array never shrinks, like the deque today. Cells are destroyed by the owner only (see `SlotCell`).
- No iterator of its own: `SlotIterator` already walks the taken bits and indexes.

Addresses:
- [Hazard] Deque blocks for large items

Useful:
- [Note] Slot storage

## [Task] SlotChunks in bits/
---
done: true
---
`bits/slotChunks.hpp`: `SlotChunks<TCellT, TChunkBytesT>` with `size()`, `operator[]`, and `emplace_back()` that allocates a new chunk when the last one is full (strong exception guarantee: the chunk is allocated before the size changes). Cells are default-initialized raw storage (`std::make_unique_for_overwrite`), so a new chunk costs one allocation and no per-cell work. A fuzz check compares references taken before and after growth, with a 1 KB item type as well as `int`.

Blocks:
- [Task] SlotStore on SlotChunks

Useful:
- [Note] Slot storage

## [Task] SlotStore on SlotChunks
---
done: true
---
`SlotStore::m_cells` becomes `SlotChunks<SlotCell<Item>>`; `emplace()` keeps its growth check against `size()`. A/B benchmark of `iterate store`, `construct filter` and the bulk deltas with `int` items, and the same cases with a 1 KB item, dense and sparse.

Results (A/B, best of 7, pinned): 1M `int` items: iteration 1.1 to 0.8 ms, filter construction 1.8 to 1.2 ms, bulk deltas 3.2 to 2.6 ms (modified) and 4.4 to 3.2 ms (replaced); sparse cases within noise or faster. 64K items of 1 KB: iteration 180 to 125 us, filter construction 175 to 150 us. Refilling right after destroying a store is slower (7.5 to 16 ms) because glibc trims freed 64 KB chunks; with trimming off it is faster (8.1 to 6.7 ms).

Blocks:
- [Task] Chunk docs

## [Task] Chunk docs
---
done: true
---
Update the Note "Slot storage" (chunks instead of a deque, the stability argument, the chunk size rule) and the Hazard.
