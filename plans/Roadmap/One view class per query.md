# [Task] One view class per query
Bring the existing views in line with the rule that a query creates one view class for every source kind, with the parts for stronger source concepts constrained by `requires` (see [Note] Concepts over virtual bases and [Note] Naming conventions). Finished Roadmap entries keep the old names, since they record history.

Useful:
- [Note] Concepts over virtual bases
- [Note] Naming conventions
- [Note] Header layout

- [Task] Merge SlotFilterView into FilterView
  Rename `SlotFilterView` to `FilterView` (`views/slotFilterView.hpp` becomes `views/filterView.hpp`) and drop the separate `FilterView` declaration. Its slot parts (pass bits, the eager and lazy delta paths, the slot interface) are constrained with `requires SlotViewable<Source>`. The general path stays unimplemented, so a non-slot source fails the class's constraint with a clear error instead of hitting an incomplete type. `Filter::View` becomes `FilterView<Source, Predicate>` without `std::conditional_t`. Update every use: `arraySortView.hpp`, `examples/example1.cpp`, `tests/fuzz.cpp`, `benchmarks/`, `CLAUDE.md`, the Notes on header layout, naming conventions and Deltas and Diffs, and the Hazards that name the view.
- [Task] Constrain path-only members of ArraySortView
  The slot-only members (`m_rank`, `m_pending`, `m_pendingSlotCount`, `m_stale`, `m_to`) and the general-only `m_resortDue` exist on both paths today; give them zero size on the other path (`[[no_unique_address]]` with an empty type), and constrain the slot-only private methods with `requires` instead of guarding their bodies with `if constexpr` where a whole method belongs to one path. Behaviour and benchmarks must not change.
