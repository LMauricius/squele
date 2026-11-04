# [Task] Broadcaster mixin
Replace the `Listenable` mixin in `components/listenable.hpp` with `Broadcaster` in `components/broadcaster.hpp` to drop virtual listeners.

- A listener is anything with `onDelta(const DeltaT &)` (see `Listening`); it inherits nothing.
- `Broadcaster<TDeltaT>` is a plain mixin without virtual members. `template <class L> addListener(L *)` stores a type-erased pair: the object pointer and a captureless function pointer that casts it back and calls `onDelta`.
- `removeListener(L *)` erases one entry with that object pointer, keeping the multiset semantics; the documented precondition is asserted before erasing.
- Protected `broadcast(const DeltaT &)` calls every listener.
- Document the reentrancy and lifetime rules from the Note.
- Add the missing `<vector>` and `<algorithm>` includes.

Blocks:
- [Task] SlotStore

Fixes:
- [Bug] Listenable missing standard includes
- [Bug] removeListener erases end iterator

Useful:
- [Note] Concepts over virtual bases
- [Note] View lifetime and listeners
