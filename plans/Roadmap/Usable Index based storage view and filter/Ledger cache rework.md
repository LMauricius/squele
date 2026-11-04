# [Task] Ledger cache rework
Rework `core/ledger.hpp` around `Queryable`, with hashed caches and explicit clean-up.

- `get(const Q &q, Args &&...a)` and `get_unsynced(...)`: on a cache miss, call `q.result(*this, a...)` and store the result. `Q` is `std::remove_cvref_t` of the argument. On a hit the extra arguments are ignored.
- Each query type gets one `std::unordered_map<Q, std::shared_ptr<typename Q::View>>`. The per-type maps sit behind a small virtual `CacheBase`, held in `std::unordered_map<std::type_index, std::unique_ptr<CacheBase>>`; `std::any` cannot be iterated generically, which `collect()` needs. This is the only virtual class in the plan.
- The ledger owns its views (`shared_ptr`). `collect()` erases every entry whose `use_count() == 1`, repeating until a pass erases nothing, because freeing a dependent view can release its source. A chain of depth `d` takes `d` passes. `clear()` drops everything.
- `get` calls `sync()` only when the view is `Syncable`; originals have nothing to sync.
- `From` queries bypass the cache (see its Task). `get` returns `q.result(*this)` directly.
- `result` may call back into the ledger (a `Filter` fetches its source). Do not hold an iterator into a cache map across that call; insert after `result` returns.
- The ledger is not thread-safe; document it.

Blocks:
- [Task] Filter query and SlotFilterView

Fixes:
- [Bug] Ledger std::forward missing template argument
- [Bug] Ledger constexpr type_index
- [Bug] Ledger cache any_cast reads empty any
- [Bug] ViewCache stores views by value
- [Bug] Ledger forwarding reference breaks QueryT
