# [Optimization] Bitmask spans in SlotDelta
Give `SlotDelta` span-like members over individual bits, next to (or instead of) its index spans. A listener could then process changes a whole word at a time: `SlotFilterView::onDelta` would compute `created = new & ~old`, `deleted = old & ~new` and `activeCount += popcount(new) - popcount(old)` per word, and `SlotIterator` would skip free slots with `std::countr_zero`.

This needs a custom bit storage class in `bits/`, because `std::vector<bool>` does not expose its words. Its layout should suit bulk SIMD operations: contiguous 64-bit words, aligned and padded to the vector width, so loops need no scalar tail. `SlotIndexPool::m_taken`, `SlotFilterView::m_pass` and `SlotDiff` would all store their bits in it, and the delta members would be read-only views over it.

Open questions:
- Should the bit members replace the index spans, or coexist with them? Index spans stay cheaper for sparse deltas (a few changes in a large view).
- One mask per change kind (created, deleted, replaced, modified), or a "touched" mask plus the kind array that `SlotDiff` already keeps?
- Is a dense mask over `slotCount` acceptable, or does a sparse delta need a word-indexed form (word index plus word) to stay O(changes)?

Addresses:
- [Hazard] Iteration over individual bits

Useful:
- [Note] Slot storage
- [Note] Deltas and Diffs
