# [Hazard] Full rebuilds in general extends
`ExtendView` takes its general path whenever its sources are not one slot source or all dense: for example, two stores, or a filter and a store. Matching is by iteration rank, and a general source tells nothing about which items changed, so every sync with any change walks all sources side by side, rebuilds every item and calls every function again.

Useful:
- [Note] Derived views

## Severity

- A sync costs O(n) item rebuilds and O(n * functions) calls, however small the change. A zip of two 1M-item stores pays this for a single modification.
- Every position is reported modified or replaced, so every listener (a filter, a sort over the extend) redoes its work for all of them too.
- No correctness risk.

## Possible efforts

- For slot sources, a rank can be computed from the taken bits (a prefix popcount, kept in a Fenwick tree or per-block counts). A change at a source index then maps to a rank, and the positions from there on shift by one: still O(n - p) for an insertion, but O(changed) for modifications.
- Skip function calls for positions whose pointer tuple and source items are unchanged, once slot sources report which items they modified.
