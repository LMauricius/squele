# [Optimization] Bitmask spans in SlotDelta
Store slot flags in a word-addressable bit storage, and give `SlotDelta` bit members next to its index spans, so listeners can process changes a whole word (64 slots) at a time. The index spans stay; listeners use the bit members only where they are faster.

`SlotDelta` gains three members:
- `touched`: one bit per slot, set for every index that appears in any span.
- `touchedWords`: the indices of the `touched` words that have a set bit (unordered, each once). A listener visits only these words, so a sparse delta costs O(changed words), not O(slotCount / 64).
- `changes`: the net change kind per slot, read only at touched indices. It is the per-slot array `SlotDiff` already keeps, so it costs nothing to expose.

A touched mask speeds up any view that keeps per-slot derived bits. A filter recomputes its pass word as `(pass & ~touched) | passed(touched & sourceTaken)`, and gets its transitions as `created = new & ~old`, `deleted = old & ~new`, `activeCount += popcount(new) - popcount(old)`. Planned set views (union or intersection of views over one source) would recompute `a & b` or `a | b` per touched word with no per-index work at all. Walking a touched word with `std::countr_zero` also yields its indices in ascending order, which ordered consumers can use without sorting.

The bit storage keeps its words 64-byte aligned (one cache line) and padded to a multiple of 8 words, with every padding bit zero. Bulk loops then need no scalar tail, and `popcount` over whole words stays exact.

Per-slot item work (predicate calls, `SlotDiff` recording) stays per index; only the bookkeeping around it becomes word-level.

Addresses:
- [Hazard] Iteration over individual bits

Useful:
- [Note] Slot storage
- [Note] Deltas and Diffs

## [Task] Slot benchmark
A benchmark executable `benchmarks/slot.cpp` (plain `std::chrono`, built by CMake, not run by `ctest`). It measures full iteration, `SlotFilterView` construction, and `onDelta` with small and bulk deltas, on dense stores and on sparse ones (a store grown to N items and shrunk to a few). Run it before the other Tasks to record a baseline.

Blocks:
- [Task] Bit storage in bits/

## [Task] Bit storage in bits/
`bits/slotBits.hpp` with two classes:
- `BitSpan`: a read-only view (word pointer plus bit count) with `test(i)`, `words()` and `nextSet(i)` (the first set bit at or after `i`, through `std::countr_zero` on the word masked below `i`).
- `BitVector`: the owning storage, with 64-byte aligned words, word count rounded up to a multiple of 8, and padding bits always zero. It has `set`, `reset`, `resize`, `push_back` and converts to `BitSpan`. `resize` gives the strong exception guarantee.

Blocks:
- [Task] Bit storage in slot views
- [Task] Touched mask in SlotDiff and SlotDelta

## [Task] Bit storage in slot views
`SlotIndexPool::m_taken` and `SlotFilterView::m_pass` switch to `BitVector`. `SlotViewable` gains `takenBits()`, returning a `BitSpan` of the taken flags; `SlotStore` forwards its pool's. The fuzz test checks `takenBits()` against `isTaken()` and that padding bits stay zero.

Blocks:
- [Task] Word-skipping SlotIterator
- [Task] Word-level SlotFilterView::onDelta

## [Task] Word-skipping SlotIterator
`SlotIterator::skipFree` uses `takenBits().nextSet(i)`, so it skips 64 free slots per step. `SlotFilterView`'s constructor scan benefits through its iterator without changes.

Blocks:
- [Task] Bitmask results and docs

## [Task] Touched mask in SlotDiff and SlotDelta
`SlotDiff` keeps a touched `BitVector` and a list of touched word indices (a word index is pushed when its word goes from zero to non-zero). These replace `m_touched`: `delta()` builds the index spans by walking the touched words, and `clear()` resets only those words and the kinds of their set bits. `Kind` becomes the public `SlotChange`, and `SlotDelta` gains `touched`, `touchedWords` and `changes`. Recording keeps the strong exception guarantee: every allocating step runs before the state the delta reads changes. The fuzz test checks the new members against the spans.

Blocks:
- [Task] Word-level SlotFilterView::onDelta

## [Task] Word-level SlotFilterView::onDelta
`onDelta` visits `touchedWords`. For each word it calls the predicate only for `touched & sourceTaken`, builds the new pass word, and derives created and deleted bits and the `m_activeCount` change with bit operations. A kept index (passed before and after) is recorded as replaced when `changes` says so, else as modified.

Blocks:
- [Task] Bitmask results and docs

## [Task] Bitmask results and docs
Rerun the benchmark against the baseline and keep only the changes that are faster. Update the Notes "Slot storage" and "Deltas and Diffs" with the bit storage and the touched mask, and mark the Hazard done if it is resolved.
