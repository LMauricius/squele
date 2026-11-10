# [Optimization] Eager single-source ExtendView
`ExtendView` always defers: a function reads every source, so it cannot run when one source has flushed and another has not been pulled yet. With one source that reason disappears, and the view could apply deltas eagerly while it is read every cycle, as `MapView` does, instead of folding every delta into a pending diff. Measured on 1M items (`benchmarks/extend.cpp`, Release): 64 modified items take 3.6 us against 0.9 us for `MapView`.

Useful:
- [Note] Derived views
- [Note] Pull data flow

- [Task] Eager apply with one source
  With one source, apply a delta at once when the view was read since the last one and nothing is pending, else fold it (the `MapView` rule); keep the deferred path for several sources.
- [Task] Extend sparse delta benchmark
  Measure 64 and 1024 modified items with one source before and after.
