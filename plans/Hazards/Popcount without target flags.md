# [Hazard] Popcount without target flags
`std::popcount` and `std::has_single_bit` on a `BitWord` compile to a libgcc call (`__popcountdi2`) unless the build enables the POPCNT instruction (`-mpopcnt`, or `-march=x86-64-v2` and above). `std::countr_zero` is unaffected (`bsf` is baseline x86-64).

Useful:
- [Note] Slot storage

## Severity

- A call of about 15 instructions per use instead of one. Today popcount runs twice per touched word in `SlotFilterView::onDelta`, so bulk deltas pay roughly 2 x 16K calls per 1M slots: small next to the predicate calls, but a tight word loop (a future set view, or counting pass bits) would be dominated by it.
- Header-only, so the user's build flags decide; the library cannot fix it alone.

## Possible efforts

- Document the recommended flags for users (`-march=x86-64-v2` or newer).
- Where a loop only needs "zero, one or many bits", `(w & (w - 1)) == 0` avoids popcount entirely.
