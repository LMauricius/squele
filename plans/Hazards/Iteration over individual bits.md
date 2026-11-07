# [Hazard] Iteration over individual bits
---
done: true
---
Slot views keep their "taken" flags in `std::vector<bool>` (`SlotIndexPool::m_taken`, `FilterView::m_pass`) and read them one bit at a time. `SlotIterator::skipFree` calls `isTaken(i)` for every index, free or taken, and each call does a bounds check plus a shift and mask through the `std::vector<bool>` proxy. `FilterView::onDelta` reads and writes one pass bit per changed index, and updates `m_count` one transition at a time.

Useful:
- [Note] Slot storage
- [Note] Deltas and Diffs

## Severity

- **Iteration of sparse views is O(slotCount), not O(count).** A store that grew to N items and shrank to a few still makes every full iteration test N bits. A filter that passes a small fraction of its source has the same cost, and nested filters repeat it at every level that is iterated.
- **The constant factor is high.** One loop step per slot costs a branch, a bounds check and a bit extraction, where a word scan would skip 64 free slots with one `std::countr_zero`.
- **`onDelta` is affected less.** Its cost is dominated by the predicate calls and by `SlotDiff` recording, and its bit accesses are scattered (delta spans are unordered). Bit access matters there mainly for bulk deltas, such as an initial load, where the changed indices cover most of the slots.
- No correctness or memory risk: `std::vector<bool>` is already as compact as a bitset.

## Possible efforts

- `std::vector<bool>` does not expose its words, so word-level code needs its own small bitset in `bits/` (a `std::vector<std::uint64_t>` plus the bit count). `SlotIndexPool` and `FilterView` would both switch to it.
- `SlotIterator` would skip free slots a word at a time (`std::countr_zero` on the current word, masked below the start index). This needs the views to expose their bitset, or a "next taken index" member, which would extend the `SlotViewable` concept.
- `onDelta` could compute transitions per word: with the old and new pass masks of a word, `created = new & ~old`, `deleted = old & ~new`, `kept = old & new`, and `count += popcount(new) - popcount(old)`. That requires grouping the changed indices by word first, either by bucketing the unordered spans or by having `SlotDelta` carry bitmasks instead of (or next to) index spans. The second option is simpler for listeners that work on words, and cheaper to produce from `SlotDiff`.
- SIMD only helps once whole words are processed in bulk (the constructor scan, bulk deltas, `deleted` masks). The predicate is an opaque function pointer, so the predicate calls themselves cannot be vectorized.
- Every change needs a benchmark on sparse and dense stores first; for dense views, a per-bit scan is already close to optimal.
