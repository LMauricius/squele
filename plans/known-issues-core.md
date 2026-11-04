# Known issues: core and components

## core/ledger.hpp

- [Bug] Ledger std::forward missing template argument
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  ---
  `std::forward(q)` in `get` and `get_unsynced` must be `std::forward<QueryT>(q)`.
- [Bug] Ledger constexpr type_index
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  ---
  `std::type_index` is not a literal type; `constexpr auto ind` must be `const`.
- [Bug] Ledger cache any_cast reads empty any
  ---
  Severity: ub
  File: include/squele/core/ledger.hpp
  ---
  `emplace(ind)` inserts an empty `std::any`; `std::any_cast<CacheT *>(&any)` then returns `nullptr`, which is dereferenced. Emplace a `CacheT{}` and use `std::any_cast<CacheT>(&it->second)`.
- [Bug] ViewCache stores views by value
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  ---
  `ViewCache` maps to `ViewT`, but the ledger stores and returns `std::shared_ptr<ViewT>`.
- [Bug] Ledger forwarding reference breaks QueryT
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  ---
  For lvalue arguments `QueryT` deduces to a reference, breaking `QueryT::ViewT` and the cache key. Use `std::remove_cvref_t<QueryT>`.

## core/concepts.hpp

- [Bug] Query concept takes incomplete Ledger by value
  ---
  Severity: latent
  File: include/squele/core/concepts.hpp
  ---
  `requires(T q, Ledger l)` with a forward-declared `Ledger`; use `Ledger &l`.
- [Bug] Query concept misuses std::less
  ---
  Severity: latent
  File: include/squele/core/concepts.hpp
  ---
  `std::less(l, r)` constructs a comparator instead of comparing; use `std::less<T>{}(l, r)`.
- [Bug] ListenerOf passes listener by value
  ---
  Severity: latent
  File: include/squele/core/concepts.hpp
  ---
  `Listenable::addListener` takes a pointer; the concept should call `s.addListener(&l)`.

## components/listenable.hpp

- [Bug] Listenable missing standard includes
  ---
  Severity: compile
  File: include/squele/components/listenable.hpp
  ---
  Needs `<vector>` and `<algorithm>`.
- [Bug] removeListener erases end iterator
  ---
  Severity: ub
  File: include/squele/components/listenable.hpp
  ---
  Removing an unknown listener erases `end()`, silently corrupting memory instead of failing.
