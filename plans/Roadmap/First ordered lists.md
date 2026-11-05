# [Feature] Slot swaps
Let slot deltas describe swaps of two indices, and let `SlotFilterView` follow a swap by swapping its pass bits instead of calling the predicate again. No view swaps items yet; ordered views will be the first. A filter applies every swap of its source to itself as well, since it reuses the source's indices.

Decisions:
- A swap is a transposition `{a, b}` of two indices; either one may be free in a view (a filter swaps a passing item with a failing one). `SlotDelta::moved` and `Move{from, to}` became `swapped` and `Swap{a, b}`, and `SlotInterest::MOVED` became `SWAPPED`.
- `swapped` is ordered and applied first, to the state at the last flush. The spans and word blocks then describe the changes after the swaps. A swap is not counted in `touchedCount`.
- `SlotDiff::swap(a, b)` swaps the four plane bits of `a` and `b`, so the changes recorded before the swap follow their items, and appends `{a, b}` to an ordered list. Swaps are not coalesced: k swaps give k entries.
- `SlotDiff::fold` applies the later delta's swaps to its planes (and appends them) before folding its words, so a lazy filter folds swaps like any other change.
- A word whose last touched bit is swapped away stays in `touchedWords` with an all-zero block. A per-word `listed` bit keeps it from being listed twice; it is filled and used only once a swap was recorded since the last `clear()`, so recording without swaps does not pay for it.
- A filter does not forward a swap of two failing slots: both are free in the filter, so the swap changes nothing it holds or reports.
- No item moves to a free index in `SlotStore`, and `SlotStore` does not swap either.

Useful:
- [Note] Deltas and Diffs
- [Note] Pull data flow

## Tasks

- [Task] Swaps in SlotDelta and SlotDiff
  ---
  done: true
  ---
  The renames above. `SlotDiff` gained `swap(a, b)` (asserts `a != b`; grows the blocks and reserves the lists before touching any bit), an ordered swap list that `delta()` exposes under `SWAPPED`, and swap handling in `fold`, `empty` and `clear`. `delta()` skips words with no touched bits.
- [Task] Swap-aware SlotFilterView
  ---
  done: true
  ---
  Asks for `SWAPPED`. `apply` swaps the pass bits of each swapped pair (the active count does not change) and records the swap in its own diff while it has listeners, before the per-index or word-level path; two failing slots are skipped. The swap loop stays out of line, since inlining it slowed sparse deltas without swaps by about 5%. The pending diff is built with `SWAPPED` too.
- [Task] Swap fuzzing and docs
  ---
  done: true
  ---
  A test-only `SwapSource` toggles, modifies and swaps slots under two nested filters (one eager, one lazy); rounds of swaps alone must not call the predicate. The model listener replays `swapped`, and `checkFolding` folds random swaps. The Note on Deltas and Diffs describes the swap form.
- [Task] Swap example
  An example shows a filter following the swaps of an ordered view, once one exists.
