# [Optimization] Word-level MapView apply
`MapView` applies a slot delta one index at a time from its spans, for every delta size. `FilterView` already switches to the word blocks for dense deltas; `MapView` can do the same for its bookkeeping. Measured on 1M items (`benchmarks/extend.cpp`, Release): 104857 modified items take 0.6 ms.

Per-item work (constructing, destroying and recomputing results) stays per index; only the taken bits, the count and the recording become word-level.

Useful:
- [Note] Derived views
- [Note] Deltas and Diffs

- [Task] Word path for MapView deltas
  For dense deltas (`!d.sparse()`, with `SPARSE_SPANS` instead of the full spans), walk `touchedWords`: the created, deleted and replaced masks come from the planes, the taken word becomes `(taken & ~touched) | exists`, the count changes by the popcounts, and the changes are recorded with `createWord`, `eraseWord` and `modifyWord`. Results are still built per set bit.
- [Task] Fold the source delta without ComparedResult
  Without `ComparedResult`, the view's own changes equal its source's (minus `relocated`), so `m_diff.fold(d)` with the flag cleared can replace the per-index recording. Measure it against the word path; keep the faster one.
- [Task] Map apply benchmark
  Sparse and bulk deltas, with and without a listener and with `ComparedResult`, before and after, as interleaved A/B runs.
