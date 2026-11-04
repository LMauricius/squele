# [Note] Deltas and Diffs
A *Delta* is temporary: it only views storage owned by someone else (usually as `std::span`s) and is valid only during the `onDelta` call. A source flushes only while its contents are consistent, so a listener reads the source and updates itself inside `onDelta` (eagerly); that is cheaper than copying the delta and replaying it later. A listener that must defer the changes copies them into its own *Diff*.

Delta index spans are unordered; no planned consumer gains from index order, and sorting would cost O(k log k) per flush in every view.

A *Diff* is self-sufficient: it owns the changes accumulated between flushes, coalesces repeated changes to the same element, and produces a Delta that stays valid until the Diff changes. It keeps its buffers' capacity between flushes, so steady-state flushing does not allocate.

A view with nothing to report notifies nobody on `flush()`; this keeps repeated pulls cheap.

For slots, "one past the largest index that may be taken" (`slotCount`) is used instead of "largest index", which has no valid value for an empty view.
