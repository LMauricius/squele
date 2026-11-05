# Known issues: core and components

## core/ledger.hpp

- [Bug] Ledger std::forward missing template argument
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  done: true
  ---
  `std::forward(q)` in `get` and `get_unsynced` must be `std::forward<QueryT>(q)`.
- [Bug] Ledger constexpr type_index
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  done: true
  ---
  `std::type_index` is not a literal type; `constexpr auto ind` must be `const`.
- [Bug] Ledger cache any_cast reads empty any
  ---
  Severity: ub
  File: include/squele/core/ledger.hpp
  done: true
  ---
  `emplace(ind)` inserts an empty `std::any`; `std::any_cast<CacheT *>(&any)` then returns `nullptr`, which is dereferenced. Emplace a `CacheT{}` and use `std::any_cast<CacheT>(&it->second)`.
- [Bug] ViewCache stores views by value
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  done: true
  ---
  `ViewCache` maps to `ViewT`, but the ledger stores and returns `std::shared_ptr<ViewT>`.
- [Bug] Ledger forwarding reference breaks QueryT
  ---
  Severity: latent
  File: include/squele/core/ledger.hpp
  done: true
  ---
  For lvalue arguments `QueryT` deduces to a reference, breaking `QueryT::ViewT` and the cache key. Use `std::remove_cvref_t<QueryT>`.

## core/concepts.hpp

- [Bug] Query concept takes incomplete Ledger by value
  ---
  Severity: latent
  File: include/squele/core/concepts.hpp
  done: true
  ---
  `requires(T q, Ledger l)` with a forward-declared `Ledger`; use `Ledger &l`.
- [Bug] Query concept misuses std::less
  ---
  Severity: latent
  File: include/squele/core/concepts.hpp
  done: true
  ---
  `std::less(l, r)` constructs a comparator instead of comparing; use `std::less<T>{}(l, r)`.
- [Bug] ListenerOf passes listener by value
  ---
  Severity: latent
  File: include/squele/core/concepts.hpp
  done: true
  ---
  `Listenable::addListener` takes a pointer; the concept should call `s.addListener(&l)`.

## components/listenable.hpp

- [Bug] Listenable missing standard includes
  ---
  Severity: compile
  File: include/squele/components/listenable.hpp
  done: true
  ---
  Needs `<vector>` and `<algorithm>`.
- [Bug] removeListener erases end iterator
  ---
  Severity: ub
  File: include/squele/components/listenable.hpp
  done: true
  ---
  Removing an unknown listener erases `end()`, silently corrupting memory instead of failing.

## components/broadcaster.hpp

- [Bug] Misspelled onDirty is silently ignored
  ---
  Severity: latent
  File: include/squele/components/broadcaster.hpp
  ---
  `addListener` detects `onDirty()` with `if constexpr`, so a listener whose `onDirty` is misspelled or has the wrong signature compiles fine and never receives dirty notifications. Dependent views are protected by `static_assert(Dependent<...>)`, but a plain consumer that relies on `onDirty` (for example, to schedule its own sync) is not. A possible fix is an explicit opt-in, such as `addDirtyListener(p_l)`, that requires `onDirty()` and fails to compile without it.
