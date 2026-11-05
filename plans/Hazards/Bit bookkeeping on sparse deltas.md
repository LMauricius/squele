# [Hazard] Bit bookkeeping on sparse deltas
`SlotDiff` keeps a touched bit per slot and walks the touched words to build the index spans and to clear itself. On a sparse delta (about one change per 64-slot word), every recorded index costs an extra scattered word access in `record()`, `delta()` and `clear()`, where the former touch-order list was read sequentially.

Useful:
- [Note] Deltas and Diffs

## Severity

- Small deltas over sparse stores became about 15 to 20% slower (for 1000 changes over 1M slots: 15.7 us to 19.2 us in `benchmarks/slot.cpp`). Dense and bulk deltas became about 2x faster, so the trade was taken.
- No correctness or memory risk.

## Possible efforts

- Keep a touch-order index list next to the bits for building spans and clearing. Measured: it recovers the sparse case but makes dense bulk deltas about 10% slower, so it was not kept.
- Skip building the index spans when no listener reads them (see [Optimization] Listener change interests).
