# [Note] View lifetime and listeners
A broadcasting view stores raw pointers to its listeners, and a listener refers back to its source. Therefore:

- Views live in `shared_ptr`s and are neither copyable nor movable.
- A dependent view owns its source through a `shared_ptr`, so the source outlives it, and the dependent view removes itself as a listener in its destructor.
- Adding or removing listeners from inside `onDelta` is UB; it invalidates the broadcast loop.
- A listener may pass a `ListenerTag` to `addListener` and take it as a second `onDelta` argument; a view with several sources (`ExtendView`) tags each with its number, so it knows which one sent a delta even when sources share a type or one source is listed twice. `removeListener` matches the tag too.
- External listeners must be removed before the view or the listener dies.
- Listener interests are counted separately from listeners: add them around `addListener` and remove them with `removeListener`, the same number of times. A leaked interest only wastes delta assembly; a missing one leaves members empty.
