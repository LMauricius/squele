# [Task] Ledger cache rework
---
done: true
---
Rework `core/ledger.hpp` around `Queryable`, with hashed caches and explicit clean-up.

- `get(const Q &q)` and `get_unsynced(const Q &q)`: on a cache miss, call `q.result(*this)` and store the result. `Q` is `std::remove_cvref_t` of the argument. Everything a view needs lives in its query, so no extra constructor arguments: they would be ignored on a hit and are not part of the key.
- Each query type gets one `std::unordered_map<Q, std::shared_ptr<typename Q::View>>`. The per-type maps sit behind a small virtual `CacheBase`, held in `std::unordered_map<std::type_index, std::unique_ptr<CacheBase>>`; `std::any` cannot be iterated generically, which `collect()` needs.
- `collect()` erases every entry whose `use_count() == 1`, repeating until a pass erases nothing. `clear()` drops everything.
- `get` calls `sync()` only when the view is `Syncable`.
- `From` queries bypass the cache: `get` returns `q.result(*this)` directly.
- Insert into the cache only after `result` returns.
- Document that the ledger is not thread-safe, and that consumers that read a view repeatedly should hold its `shared_ptr` and call `sync()` on it instead of calling `get` each time (a hash lookup plus a `shared_ptr` copy).

Blocks:
- [Task] Filter query and SlotFilterView

Fixes:
- [Bug] Ledger std::forward missing template argument
- [Bug] Ledger constexpr type_index
- [Bug] Ledger cache any_cast reads empty any
- [Bug] ViewCache stores views by value
- [Bug] Ledger forwarding reference breaks QueryT

Useful:
- [Note] Query keys
- [Note] Concepts over virtual bases
