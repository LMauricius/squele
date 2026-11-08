# Known issues: views

- [Bug] Deferred apply sized by the last folded delta
  ---
  Severity: ub
  File: include/squele/views/filterView.hpp
  done: true
  ---
  A deferring view (`FilterView`, `ArraySortView`) kept only the slot count of the last delta it folded, then applied the folded changes with it. An earlier delta can name larger indices: a sort that grows and shrinks again within one flush reports swaps up to its peak, and its next delta has a smaller slot count. The deferred apply then indexed `m_pass` or `m_rank` past their size. The pending slot count is now the largest since the last apply, and is reset after it. The fix covers `MapView` and `ExtendView` too; `checkDeferredSlotCount` in `tests/fuzz.cpp` reproduces the case for all four views (it fails on the old code, with the standard containers' bounds checks enabled in the test build).
