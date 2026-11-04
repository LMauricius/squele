# [Note] View lifetime and listeners
A broadcasting view stores raw pointers to its listeners, and a listener refers back to its source. Therefore:

- Views live in `shared_ptr`s and are neither copyable nor movable.
- A dependent view owns its source through a `shared_ptr`, so the source outlives it, and the dependent view removes itself as a listener in its destructor.
- Adding or removing listeners from inside `onDelta` is UB; it invalidates the broadcast loop.
- External listeners must be removed before the view or the listener dies.
