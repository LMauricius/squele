# [Optimization] Lazy views through aggregated diffs
---
done: true
---
Let a dependent view defer the deltas of its source: instead of applying a delta inside `onDelta`, the view folds it into a *pending* `SlotDiff` of source changes, and applies the aggregate when it is next synced. Many source flushes then cost one application, and a view nobody reads costs only the folding.

Folding a delta into a diff is composition of changes per slot, the same table that `SlotDiff` coalesces with today (a create folded onto a pending delete is a replace, a delete onto a pending create is nothing). On bit planes (see [Optimization] Bit-plane SlotDiff) it is four word operations per touched word of the delta, for `t = d.touched`:

- `existed |= d.existed & ~touched`
- `exists = (exists & ~t) | d.exists`
- `same = ((same | ~touched) & d.same & t) | (same & ~t)`
- `touched |= t`

so folding costs O(touched words of the delta), whatever the number of changes. Applying the aggregate later reuses the eager code: `SlotDiff::delta()` of the pending diff has the same shape as a source delta, so `SlotFilterView` gets one `apply(const Delta &)` for both paths. The source is consistent at application time (the view applies inside its own `sync()`, right after pulling the source), and only slots whose `exists` bit is set are read, so no deleted item is touched.

Laziness is compatible with having listeners: a view's own diff fills when the aggregate is applied, which happens in `sync()`, which the view's listeners trigger through `pull()`. The dirty flag already propagates on the first change of a cycle, so nothing downstream notices the difference except timing.

## When to apply immediately and when lazily

Eager application is cheaper when the view is read after every source flush anyway: folding would add a pass per delta, and nothing coalesces. Lazy application is cheaper when the view is read less often than its source flushes (the changes of several cycles coalesce; repeated modifications of one slot cost one predicate call) or not at all (an unread sibling costs O(touched words) per flush instead of a predicate call per change).

The rule, per view and with no API: a view remembers whether it was read (synced or iterated after `sync()`) since the last delta it received. If it was, the next delta is applied eagerly; if not, the next delta is folded. A view read every cycle stays eager; one that misses a cycle turns lazy and stays so until it is read again, when the aggregate is applied and the flag is set. Consumers that read a view through `Ledger::get()` or `sync()` set the flag without knowing about the rule.

Memory: a lazy view holds a pending diff of the source's index space next to its own diff; on bit planes that is 0.5 byte per slot, released when the aggregate is applied and the pending blocks are cleared (capacity stays, as with every diff).

Decisions:
- The pending diff is a `SlotDiff`, not a copy of the delta: one representation, one coalescing table, and `apply()` reads it like any delta.
- Deltas are folded whole (every touched word), including `nothing` kinds; they fold to nothing.
- No explicit `setLazy()` first; the one-bit rule covers the measured cases. A forced mode stays an open idea.
- Built after the bit planes: folding on the byte kinds is per index and would not pay off for unread views with many changes.

Addresses:
- [Hazard] Eager updates of unread views

Useful:
- [Note] Pull data flow
- [Note] Deltas and Diffs
- [Note] View lifetime and listeners

## [Task] SlotDiff::fold
---
done: true
---
`SlotDiff::fold(const SlotDelta &d)`: composes `d.words` of each `d.touchedWords` entry into this diff with the four word operations, grows the blocks to `d.slotCount` first (strong exception guarantee), updates `touchedCount` and `m_touchedWords`. The fuzz test checks that folding the deltas of a random sequence of mutations, split at random points, equals recording the whole sequence into one diff.

Blocks:
- [Task] Pending diff in SlotFilterView

Useful:
- [Note] Deltas and Diffs

## [Task] Pending diff in SlotFilterView
---
done: true
---
The body of `onDelta` becomes `apply(const Delta &d)`. `onDelta` either applies or folds into `m_pending`; `sync()` pulls the source and then applies `m_pending.delta(...)` if it is non-empty and clears it. `dirty()` needs no change: a fold happens only during a source flush, which follows the source's dirty notification, and the flag is cleared only by `sync()`.

Blocks:
- [Task] Read-since-delta rule

Useful:
- [Note] Pull data flow

## [Task] Read-since-delta rule
---
done: true
---
The one-bit rule: `m_readSinceDelta` is set at the end of `sync()` and cleared by `onDelta`; `onDelta` applies when it was set (and nothing is pending) and folds otherwise. The fuzz test keeps a sibling filter that is pulled in one round of five, so it turns lazy and folds the deltas its siblings pull.

Results (A/B, best of 7, pinned): 32 dense siblings, all modified, pull one: 64 to 6.6 ms; depth-8 chains and siblings pulled every cycle within noise; sparse fan-out unchanged (folding a sparse delta costs about as much as applying it). Benchmark the fan-out case pulling one sibling (expected: from about 68 ms to the cost of one view) and the chain cases pulled every cycle (expected: unchanged), and a case that pulls every other cycle.

Blocks:
- [Task] Lazy view docs

## [Task] Lazy view docs
---
done: true
---
Update the Notes "Pull data flow" (eager and lazy application, the rule, where the source is read) and "Deltas and Diffs" (folding), and the Hazard.
