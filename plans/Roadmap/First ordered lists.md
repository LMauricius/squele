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

Subtask:
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

# [Feature] Sort
A `Sort<Q, Cmp>` query whose view, `ArraySortView`, holds the items of any `ListViewable` source ordered by a comparator. It is the first `OrderedViewable`: its indices are dense positions `0..count-1`, and index order is sorted order. It sorts any list source fully, and adds special cases for slot sources: after the first full sort, it sorts only the items a delta changed and merges them into the sorted rest, and it reports its reordering as swaps, so a filter over it follows the order without calling its predicate.

Decisions:
- `ListViewable`: a `Viewable` with `T::Item`, `count()`, and `begin()`/`end()` iterating its items as `const Item &`. `SlotViewable` refines it.
- `OrderedViewable`: a `SlotViewable` that is dense (`slotCount() == count()`, every index below it taken) and sorted (the item at `i` is never less than the item at `i - 1` under `T::Compare`), with `compare()` returning the comparator. Density and order are documented contracts, not checked.
- The comparator is a strict weak order on items (`std::strict_weak_order<const Cmp &, const Item &, const Item &>`). Equal items keep an unspecified relative order (the sort is not stable), so an item never moves past its equals. `Cmp` follows the key rules of the `Filter` predicate (a function pointer, an empty type, or a comparable and hashable value) and defaults to `std::ranges::less`, so `Sort{From{store}}` works.
- The view owns no item copies. It keeps one dense array of entries, `m_order`, each holding a pointer to its source item, and compares through the pointers directly. The taken bits are the first `count` bits. Its own delta is always a `SlotDelta`.
- Pointer validity: `SlotDelta` gains `relocated`, a flag that is always filled. It is all or nothing: when it is true, every pointer read before this delta is invalid; when it is false, every one stays valid, except for the items the delta itself lists as created, replaced or swapped. Sources set it rarely: `SlotStore` never does (its chunks never move), and a filter forwards its source's flag. `SlotDiff` sets it with `relocate()`, keeps it through `fold` (as an OR) and resets it in `clear()`; a diff holding only the flag is not empty, so the relocation still reaches listeners.
- General path: when the view does not understand its source's delta (any delta that is not a `SlotDelta` of a `SlotViewable` source), every flush of the source re-sorts fully, collecting the pointers again by iterating the source. Pointers are read only while the view is clean, and a dirty view re-sorts before it is read (see [Note] Pull data flow). The output marks every position modified (word level) plus the created or erased tail, since no item identity survives a re-sort.
- Slot path: an entry also holds its source index, and `m_rank` maps a source index back to its position (sized by the source's `slotCount`). One class picks the path at compile time (`if constexpr (SlotViewable<Source>)`). A delta with `relocated` set refreshes every pointer from its index (O(n) lookups, no comparisons, order kept) before anything else; otherwise only the entries of created, replaced and swapped indices read the source.
- Slot path, swaps: a swap in the source exchanges the indices of two entries in `m_rank` and rereads their two pointers (the items keep their positions), so it calls no comparison and produces no output.
- Slot path, *partially unsorted*: after a delta, only the changed items are out of place, while the others stay in sorted order. Deleted items are removed and modified or replaced items are taken out, compacting `m_order` once from the first affected position. The changed items (created, modified, replaced) are sorted alone in O(k log k), then merged into `m_order` from the back, with a binary search per item: O(k log n) comparisons and O(n - p) moves, where `p` is the first affected position. A modified item that is still ordered against unchanged neighbours stays in place.
- Slot path, bulk: a delta that changes most items (a threshold measured by benchmark) re-sorts everything instead, but still reports swaps, since source indices keep item identity.
- Slot path, output: the reordering is a permutation of positions, recorded as swaps (one per element of each cycle but the last, so at most one per moved position). A created item is recorded at its final position and an erased one at a position past the new `count`; a modified item is recorded at its final position. `SlotDiff::swap` makes the earlier changes follow their items, so the delta meets the "swaps first" rule. Moved but otherwise unchanged items are only swapped, never touched. The view's own `relocated` is the source's flag: its items are the source's items.
- Positions shift on insertion and removal, so one change near the front swaps every later position: O(n) swaps per flush in the worst case, for this view and for every filter over it.
- Like `SlotFilterView`, the view is eager while read every cycle, and otherwise stays dirty until `sync()`. The slot path folds deltas into a pending diff and applies the net changes; the general path only remembers that a re-sort is due.

Useful:
- [Note] Deltas and Diffs
- [Note] Pull data flow
- [Note] Query keys

Subtask:
- [Task] Relocation flag in SlotDelta
  ---
  done: true
  ---
  `SlotDelta::relocated` and `SlotDiff::relocate()`, with `fold`, `empty` and `clear`. `SlotStore` never sets it; `SlotFilterView` forwards it, eager or lazy. The fuzz test relocates at random and checks the flag reaches every level.
- [Task] KeyCallable concept
  ---
  done: true
  ---
  Move the key rules of `FilterPredicate` (pointer, empty type, or comparable and hashable) into a shared concept with equality and hash helpers (`bits/callable.hpp`: `callableEqual`, `combineCallableHash`), so `Filter` and `Sort` key their callables the same way. `FilterPredicate` becomes `KeyCallable` plus `std::predicate`; `Filter` uses the helpers.
- [Task] ListViewable and OrderedViewable concepts
  ---
  done: true
  ---
  Both concepts in `core/concepts.hpp`, with `SlotViewable` refining `ListViewable`, and the density and order contracts of `OrderedViewable` documented.
- [Task] Order merge and permutation tools
  ---
  done: true
  ---
  Two small tools in `bits/`: a backward in-place merge of a sorted batch into a sorted dense array (binary search per batch item, block moves), and a cycle walk that turns old-to-new positions into swaps (it swaps the permutation entries along, so it needs no visited bits and leaves the identity). Both are tested alone (`checkOrderTools`).
- [Task] Sort query and ArraySortView
  `Sort<Q, Cmp>` (equality, `std::hash`, CTAD) and `ArraySortView` with the general path: full sort on construction and on every source flush, the all-modified output, and the `OrderedViewable` interface. A slot source already gets index entries, `m_rank`, the swap remap and the pointer refresh on `relocated`; every other change re-sorts fully for now.

  Blocks:
  - [Task] Swap example
- [Task] Incremental sorting for slot sources
  The slot path: interests in the created, deleted, modified and replaced spans and `SWAPPED`; compaction, the sorted batch merge, modified items kept in place, the output swaps and change records, the bulk re-sort threshold, and the pending diff. Buffers keep their capacity, so a steady state does not allocate.
- [Task] Sort fuzzing
  Random creates, erases, modifications, relocations and source swaps on `Sort` over a store, `Sort` over a filter and a filter over `Sort`, plus a test-only `ListViewable` source with its own delta type for the general path. After each round, the view must be dense, sorted, and hold the source's items, and the model listener must replay its delta. A counting comparator checks that rounds of source swaps alone call no comparison.
- [Task] Sort benchmarks
  Construction, sparse and bulk deltas against a full re-sort (to set the threshold), and a filter over a sort following its swaps.
- [Task] Sort docs
  A Note on ordered views (density, order contract, the two paths, swap output and its O(n) cost), the Note on Deltas and Diffs (`relocated`), the README, and the Hazard "Uncoalesced swaps" updated with the sort case.
