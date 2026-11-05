# [Note] Query keys
Queries are cache keys in the `Ledger`, so they must be copyable, equality comparable and hashable.

- Callables in a query are never `std::function`, which has no equality. A callable is a function pointer (compared by value), an empty type such as a named functor or a captureless lambda (compared by type alone, since the `Ledger` keeps one cache per query type), or a value type with `operator==` and `std::hash`. `Filter` deduces the callable's type, so a lambda stays a lambda and is inlined into the view; a unary `+` before it forces the pointer form.
- Code that runs a predicate over many slots accumulates its result without branching (`next |= BitWord{pred(x)} << b`): once the call is inlined, a branch on a data-dependent result mispredicts about half the time.
- An original view cannot be copied, so it is never its own query. `From{p_view}` stands in for it and compares by identity.
- The `Ledger` does not cache `From`: the cached key would hold its own reference, and `collect()` could never release the view.
- A cached query that contains a `From` keeps that original alive until the query is collected.
- `result()` may call back into the `Ledger` (to fetch a source), so the `Ledger` never holds an iterator into a cache across that call.
- The `Ledger` owns its views. `collect()` repeats until nothing is released, since freeing a view can release its sources; a chain of depth `d` takes `d` passes. The `Ledger` is not thread-safe.
