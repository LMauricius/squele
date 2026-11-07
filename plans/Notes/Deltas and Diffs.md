# [Note] Deltas and Diffs
A *Delta* is temporary: it only views storage owned by someone else (usually as `std::span`s) and is valid only during the `onDelta` call. A source flushes only while its contents are consistent, so a listener reads the source and updates itself inside `onDelta` (eagerly); that is cheaper than copying the delta and replaying it later. A listener that must defer the changes folds them into its own *Diff* (see [Note] Pull data flow).

Delta index spans are unordered; no planned consumer gains from index order, and sorting would cost O(k log k) per flush in every view.

A *Diff* is self-sufficient: it owns the changes accumulated between flushes, coalesces repeated changes to the same element, and produces a Delta that stays valid until the Diff changes. It keeps its buffers' capacity between flushes, so steady-state flushing does not allocate. A Diff can also *fold* a Delta: appending a later Delta of the same view yields the net change of both, as if every change had been recorded in the Diff. For slots this is four bit operations per touched word of the Delta (`SlotWordChange::fold`).

A view with nothing to report notifies nobody on `flush()`; this keeps repeated pulls cheap.

For slots, "one past the largest index that may be taken" (`slotCount`) is used instead of "largest index", which has no valid value for an empty view.

A Delta may carry several forms of the same changes (for example index spans and bit masks). A form is added only when some listener gets faster with it, and each listener reads whichever form is cheapest for its work. A form may be replaced by a cheaper one that carries the same information.

`SlotDelta` carries two forms: index spans, and word blocks (`words`, `touchedWords`). A `SlotWordChange` block describes 64 slots as four bit planes (`touched`, `existed`, `exists`, `same`); every change kind is a mask expression over them, and recording a change on one slot or on a whole word is the same few bit operations. `touchedWords` lists the touched blocks, so word-level work costs O(changed words) even on huge views. The word form pays off from about 2 changes per touched word; `SlotDelta::sparse()` (from the always-filled `touchedCount`) states that rule once, and `SlotFilterView` picks its path per delta by it.

`SlotDiff` builds the spans from the blocks into two buffers, two spans per buffer growing from both ends, so the buffers hold at most twice the touched count. A lone change in its word is written to all four spans at once and kept by advancing only its own cursor: branching on its kind would mispredict once per word.

A slot delta also carries an ordered list of swaps (`swapped`), applied before every other change: the spans and word blocks describe the changes that follow the swaps. `SlotDiff::swap` swaps the plane bits of both indices, so the changes recorded earlier follow their items, and folding a delta applies its swaps to the folded planes before its words. A swapped item that did not change otherwise stays untouched, so a filter swaps its pass flags without calling the predicate. A word whose touched bits were all swapped away stays listed (all zero), and a per-word `listed` bit, kept only while there are swaps, stops it from being listed twice.

An item keeps its address unless the delta lists it as created, replaced or swapped; a modified item keeps it too, so a source that gives an item a new address reports it as replaced. A listener may therefore keep pointers to its source's items and refresh only those. A source whose storage moves items in bulk sets `relocated` instead, which invalidates every pointer at once; `SlotStore` never does, since its chunks never move. `SlotDiff::relocate()` records it, folding keeps it (as an OR), and a diff holding only the flag still notifies its listeners.

A source fills only the delta members its listeners asked for (`addInterests` with `SlotInterest` flags); the others are empty. Interests are counted per flag, so a member is built while at least one listener wants it. A listener must ask for every member it reads. `SPARSE_SPANS` asks for the index spans only while the delta is `sparse()`: a listener that handles dense deltas through the bits (every filter) uses it, so a bulk change through a chain of filters never builds spans.
