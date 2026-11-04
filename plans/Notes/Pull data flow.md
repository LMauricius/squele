# [Note] Pull data flow
Sources push deltas to their listeners on `flush()`, but nothing happens until a consumer asks. Before a dependent view reads its source, it calls `pull(source)`: the source is synced (if it is `Syncable`) and then flushed, so every change reaches the listeners first.

Skipping the pull is not only stale but unsafe: an original may hold an unflushed erase, and reading the erased element is UB.

Several views may share a source, and each one pulls it. So `sync()` and `flush()` must be cheap when nothing changed.

`Ledger::get` syncs the view it returns; a consumer holding a view calls `sync()` itself before reading again.
