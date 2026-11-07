# [Hazard] Opaque predicate calls
---
done: true
---
`Filter` holds its predicate as a function pointer, so the query stays comparable and hashable. Every check in `FilterView` is therefore an indirect call the compiler cannot inline or vectorize, and the work around it (bit updates, recording) cannot be hoisted across it.

Useful:
- [Note] Query keys

## Severity

- Bulk work is bound by the calls: constructing a filter over 1M items takes about 7.7 ms, and a bulk delta through two filter levels about 7.8 ms (`benchmarks/slot.cpp`), where the predicates themselves are a modulo and a compare.
- A chain of d filters costs d calls per changed item; the chain cannot be fused into one check.
- No correctness or memory risk.

## Possible efforts

- Accept a stateless predicate type as a template parameter (`Filter<Q, Pred>` with an empty, default-constructible `Pred`): equality and hashing then follow from the type alone, and the call inlines. Function pointers stay as one allowed `Pred`.
- A predicate over whole words (given 64 items, return a mask) would let the filter avoid per-item calls for trivially vectorizable predicates; only worth it with the template form above.
- Done (see [Optimization] Template predicate): `Filter<Q, Pred>` takes empty functors and lambdas, which are inlined. Measured over 1M items: construction 7.5 ms (pointer, per-item branches) to 1.7 ms (pointer) and 1.2 ms (functor) with a word-level, branch-free scan; a bulk delta through two filters 3.2 ms (pointer) to 2.7 ms (functor). The indirect call itself was a smaller cost than the mispredicted branch on its result.
