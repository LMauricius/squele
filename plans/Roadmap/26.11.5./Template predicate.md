# [Optimization] Template predicate
---
done: true
---
Make the predicate type a template parameter of `Filter`, so a stateless predicate inlines into `SlotFilterView` instead of going through a function pointer on every check. `Filter<Q, Pred>` keeps the query comparable and hashable by restricting `Pred`:

- a function pointer (the current form): compared and hashed by value;
- an empty class type (a named functor or a captureless lambda): equal by type, hashed as a constant, since the `Ledger` already keeps one cache per query type;
- any other type must be equality comparable and have a `std::hash` specialization (a stateful functor compared by value).

CTAD keeps the call sites: `Filter{From{store}, isEven}` deduces the pointer form, `Filter{From{store}, IsEven{}}` the empty form. A lambda written in place is a distinct type each time it is spelled (as a pointer is a distinct value today), so sharing a view still means naming the predicate.

Expected gain: the bulk work of a filter (construction, bulk deltas) is bound by the indirect calls today, about 7.7 ms per 1M items for a modulo predicate; inlining removes the call and lets the compiler hoist the bit work across it.

Decisions:
- `Pred` defaults to `bool (*)(const Item &)`, so existing code and docs stay valid.
- `SlotFilterView` stores the predicate as `[[no_unique_address]] Pred m_pred`, so the empty form costs no space.
- No word-level predicate interface for now; it only pays off for predicates the compiler can vectorize, which needs this change first.

Addresses:
- [Hazard] Opaque predicate calls

Useful:
- [Note] Query keys

## [Task] FilterPredicate concept and Filter<Q, Pred>
---
done: true
---
A concept `FilterPredicate<P, Item>`: `std::predicate<const P &, const Item &>`, copyable, and one of: a function pointer, an empty type, or equality comparable with `std::hash<P>`. `Filter` gains the `TPredT` parameter with the pointer default, `operator==` and `std::hash` follow the three cases, and the implicit deduction guide covers `Filter{query, pred}`. The example and the fuzz test exercise both the pointer and the functor form, and check that two queries with the same functor type share one view.

Blocks:
- [Task] Inlined predicate in SlotFilterView

Useful:
- [Note] Query keys

## [Task] Inlined predicate in SlotFilterView
---
done: true
---
`SlotFilterView` takes the predicate type from `Query::Predicate` and stores it with `[[no_unique_address]]`; `check()` calls it directly. Inlining exposed a data-dependent branch on the predicate's result, so the word loop and the constructor scan accumulate the result branch-free, and the constructor scans the source's taken words instead of iterating items.

Results (A/B, best of 7, pinned, 1M items): construction 7.5 to 1.7 ms (pointer) and 1.2 ms (functor); bulk delta through two filters 3.2 ms (pointer) and 2.7 ms (functor); sparse cases within noise or faster. A/B benchmark of `construct filter`, the bulk deltas and the tree cases with a functor predicate against the pointer form; the benchmark gains a functor variant of its predicates.

Blocks:
- [Task] Predicate docs

## [Task] Predicate docs
---
done: true
---
Update the Note "Query keys" (callables in queries: pointer, empty type, or comparable and hashable value) and the README if it shows the pointer form only; update the Hazard.
