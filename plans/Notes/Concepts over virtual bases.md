# [Note] Concepts over virtual bases
Contracts are concepts, not virtual base classes, and each concept requires only the members its users call. No class implements a no-op to satisfy an interface; a user that needs an optional member checks for it with `if constexpr` (for example, the `Ledger` calls `sync()` only on `Syncable` views).

When objects of different types must sit in one container (such as listeners), erase their type with an object pointer plus a captureless function pointer that casts it back. This costs no vtable in the stored objects and no allocation per entry.

Virtual classes remain only where a concept cannot work: internal type erasure that must be iterated generically (the `Ledger`'s per-query-type caches).

Prefer self-sufficient value types to builder or manager classes: the type that accumulates state also exposes the operations on it (see `SlotDiff`).
