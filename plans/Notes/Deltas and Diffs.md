# [Note] Deltas and Diffs
A *Delta* is temporary: it only views storage owned by someone else (usually as `std::span`s) and is valid only during the `onDelta` call. A listener that needs the changes later must copy them, typically by adding the delta to its own *Diff*.

A *Diff* is self-sufficient: it owns the changes accumulated between flushes, coalesces repeated changes to the same element, and produces a Delta that stays valid until the Diff changes. It keeps its buffers' capacity between flushes, so steady-state flushing does not allocate.

A view with nothing to report notifies nobody on `flush()`; this keeps repeated pulls cheap.

For slots, "one past the largest index that may be taken" (`slotCount`) is used instead of "largest index", which has no valid value for an empty view.
