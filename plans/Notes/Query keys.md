# [Note] Query keys
Queries are cache keys in the `Ledger`, so they must be copyable, equality comparable and hashable.

- Callables in a query are function pointers, not `std::function`, which has no equality. A stateless lambda converts to one; when the parameter type is deduced, write a unary `+` before the lambda.
- An original view cannot be copied, so it is never its own query. `From{p_view}` stands in for it and compares by identity.
- The `Ledger` does not cache `From`: the cached key would hold its own reference, and `collect()` could never release the view.
- A cached query that contains a `From` keeps that original alive until the query is collected.
- `result()` may call back into the `Ledger` (to fetch a source), so the `Ledger` never holds an iterator into a cache across that call.
- The `Ledger` owns its views. `collect()` repeats until nothing is released, since freeing a view can release its sources; a chain of depth `d` takes `d` passes. The `Ledger` is not thread-safe.
