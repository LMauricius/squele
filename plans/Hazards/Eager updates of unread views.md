# [Hazard] Eager updates of unread views
---
done: true
---
A dependent view updates itself inside `onDelta`, while its source flushes. Every listener of a source therefore pays for every delta, whether or not anyone reads it afterwards: a cached filter nobody pulls still runs its predicate on every change of its source.

Useful:
- [Note] Pull data flow
- [Note] Deltas and Diffs

## Severity

- **A pull costs the work of the whole subtree below the flushed source, not of the path that was asked for.** Measured (`benchmarks/slot.cpp`): with 32 sibling filters over a 1M-item store, pulling one of them after a bulk change takes 68 ms, against about 2 ms for the one view that was read.
- Views kept alive by the `Ledger` cache (nobody holds them, `collect()` was not called) keep this cost indefinitely.
- No correctness or memory risk.

## Possible efforts

- `Ledger::collect()` after a batch of queries drops the views nobody holds; that is the current mitigation.
- A lazy mode for a view without listeners: `onDelta` only ORs the delta's touched words into a pending `BitVector` (O(touched words), no predicate calls), and `sync()` re-checks the pending bits against the source's taken bits. The changes of many cycles coalesce into one check per slot. With listeners the view must still produce deltas, so the lazy mode applies only to leaves; and the pass-to-pass kind (`replaced` against `modified`) must be kept per pending bit if the view later gains listeners.
- Done (see [Optimization] Lazy views through aggregated diffs): a view not read since its last delta folds the next ones into a pending diff and applies the net changes on its next `sync()`. Measured: pulling one of 32 dense siblings after a bulk change went from 64 ms to 6.6 ms; the remaining cost is the folding in the 31 others (O(touched words) each). With sparse deltas folding costs about as much as applying, so sparse fan-out is unchanged. A lazy view holds a pending diff (half a byte per slot) next to its own.
- Skipping the broadcast to listeners that are themselves clean and unread is not possible with the current eager contract: the source does not keep the delta past `flush()`.
