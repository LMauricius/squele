# [Hazard] Teardown of large listener sets
`Broadcaster::removeListener` searches its listener vector linearly, and `Ledger::collect()` repeats its passes over every cache until a pass releases nothing.

Useful:
- [Note] View lifetime and listeners

## Severity

- Removing all L listeners of one source costs O(L^2): measured 2.4 ms for 4096 listeners added and removed (about 0.3 ns per compared entry). Only sources with thousands of direct listeners notice.
- `collect()` costs O(depth x views): a chain of depth d needs d passes, each visiting every cached view. Called rarely, so this is minor.
- No correctness or memory risk.

## Possible efforts

- Swap the removed entry with the last one instead of `erase`; still O(L) per removal, but no shifting. Broadcast order is unspecified, so this is allowed.
- O(1) removal needs the listener to remember its slot (an index or a handle returned by `addListener`), which changes the `Listenable` contract.
- `collect()` could release in one pass by visiting views in dependency order, which the ledger does not know today; a per-view "my sources" list would give it.
