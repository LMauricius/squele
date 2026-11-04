# [Task] Concept-based Listenable
Rework the `Listenable` mixin in `components/listenable.hpp` to drop virtual listeners.

- A listener is anything with `onDelta(const DeltaT &)` (see `ListenerOf`); it inherits nothing. The delta and its spans are only valid during the call.
- `Listenable<TDeltaT>` is a plain mixin without virtual members. `template <class L> addListener(L *)` stores a type-erased pair: the object pointer and a captureless function pointer that casts it back and calls `onDelta`. This avoids a vtable in every listener and an allocation per listener.
- `removeListener(L *)` erases one entry with that object pointer, keeping the multiset semantics; the documented precondition is asserted before erasing.
- Protected `notify(const DeltaT &)` calls every listener.
- Adding or removing listeners from inside `onDelta` is UB (it would invalidate the loop in `notify`); document it.
- Listeners are stored as raw pointers to a specific object, so a class using this mixin must not be copied or moved while it has listeners. Views delete their copy and move operations.
- Add the missing `<vector>` and `<algorithm>` includes.

Blocks:
- [Task] SlotList

Fixes:
- [Bug] Listenable missing standard includes
- [Bug] removeListener erases end iterator
