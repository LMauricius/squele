# [Note] Pull data flow
Sources push deltas to their listeners on `flush()`, but nothing happens until a consumer asks. Before a dependent view reads its source, it calls `pull(source)`: the source is synced (if it is `Syncable`) and then flushed, so every change reaches the listeners first.

Skipping the pull is not only stale but unsafe: an original may hold an unflushed erase, and reading the erased element is UB.

Several views may share a source, and each one pulls it. So `sync()` and `flush()` must be O(1) when nothing changed; a *dirty* flag makes them so:

- A `Syncable` view is dirty when its `sync()` would change its contents; `dirty()` reports it, and `sync()` returns at once when clean. An original is never dirty (its contents are changed by hand).
- Dirtiness is pushed through listeners: a view calls `broadcastDirty()` when its diff goes from empty to non-empty (an original on its first mutation after a flush). A listener's `onDirty()` sets its flag and forwards the call only if it was clean, so a cycle costs each view one call.
- A dependent view clears its flag after pulling its source. Its own diff fills during that pull, and its listeners were already marked by the same propagation, so the invariant holds: a non-empty diff implies dirty listeners.
- A view records changes only while it has listeners, so a listener sees only the changes made after it was added. Listeners are added only between cycles, when the source has nothing unflushed (a dependent view pulls its source in its constructor); a listener added mid-cycle misses that cycle's changes silently. This is by design, not checked.

Call `flush()` only when the contents are consistent: listeners read the flushing view inside `onDelta`.

`Ledger::get` syncs the view it returns; a consumer holding a view calls `sync()` itself before reading again.
