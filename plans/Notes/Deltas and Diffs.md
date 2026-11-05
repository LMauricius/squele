# [Note] Deltas and Diffs
A *Delta* is temporary: it only views storage owned by someone else (usually as `std::span`s) and is valid only during the `onDelta` call. A source flushes only while its contents are consistent, so a listener reads the source and updates itself inside `onDelta` (eagerly); that is cheaper than copying the delta and replaying it later. A listener that must defer the changes copies them into its own *Diff*.

Delta index spans are unordered; no planned consumer gains from index order, and sorting would cost O(k log k) per flush in every view.

A *Diff* is self-sufficient: it owns the changes accumulated between flushes, coalesces repeated changes to the same element, and produces a Delta that stays valid until the Diff changes. It keeps its buffers' capacity between flushes, so steady-state flushing does not allocate.

A view with nothing to report notifies nobody on `flush()`; this keeps repeated pulls cheap.

For slots, "one past the largest index that may be taken" (`slotCount`) is used instead of "largest index", which has no valid value for an empty view.

A Delta may carry several forms of the same changes (for example index spans and bit masks). New forms are added next to the existing ones, never instead of them, and only when some listener gets faster with them; each listener reads whichever form is cheapest for its work.

`SlotDelta` carries both forms: index spans, and bits (`touched`, `touchedWords`, `changes`). `touchedWords` lists the non-zero touched words, so word-level work costs O(changed words) even on huge views. The word form pays off from about 2 changes per touched word; `SlotFilterView` picks its path per delta by that ratio.

A source fills only the delta members its listeners asked for (`addInterests` with `SlotInterest` flags); the others are empty. Interests are counted per flag, so a member is built while at least one listener wants it. A listener must ask for every member it reads.
