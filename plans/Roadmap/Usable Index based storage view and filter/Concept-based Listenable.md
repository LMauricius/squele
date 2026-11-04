# [Task] Concept-based Listenable
Rework the `Listenable` mixin in `components/listenable.hpp` to drop virtual listeners.

- A listener is anything with `onDelta(const DeltaT &)` (see `ListenerOf`); it inherits nothing. The delta and its spans are only valid during the call.
- `Listenable<TDeltaT>` is a plain mixin without virtual members. `template <class L> addListener(L *)` stores a type-erased pair: the object pointer and a captureless function pointer that casts it back and calls `onDelta`. This avoids a vtable in every listener and an allocation per listener.
- `removeListener(L *)` erases one entry with that object pointer, keeping the multiset semantics; the documented precondition is asserted before erasing.
- Protected `notify(const DeltaT &)` calls every listener.
- Add the missing `<vector>` and `<algorithm>` includes.

Blocks:
- [Task] SlotList

Fixes:
- [Bug] Listenable missing standard includes
- [Bug] removeListener erases end iterator
