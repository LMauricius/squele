# [Task] Broadcaster mixin
---
done: true
---
Replace the `Listenable` mixin in `components/listenable.hpp` with `Broadcaster` in `components/broadcaster.hpp` to drop virtual listeners.

- A listener is anything with `onDelta(const DeltaT &)` and `onDirty()` (see `Listening`); it inherits nothing.
- `Broadcaster<TDeltaT>` is a plain mixin without virtual members. `template <class L> addListener(L *)` stores a type-erased entry: the object pointer and two captureless function pointers that cast it back and call `onDelta` or `onDirty`.
- `removeListener(L *)` erases one entry with that object pointer, keeping the multiset semantics; the documented precondition is asserted before erasing.
- Protected `broadcast(const DeltaT &)` and `broadcastDirty()` call every listener.
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
- [Note] Pull data flow
