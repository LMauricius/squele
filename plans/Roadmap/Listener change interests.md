# [Optimization] Listener change interests
Let each listener declare which kinds of changes it is interested in (for example created, deleted, replaced, modified, moved, or only the counts). A source then builds only the parts of its `Delta` that at least one listener needs, and skips the rest.

The interests could be detected at compile time in `Broadcaster::addListener`, the same way it already detects an optional `onDirty()`, and combined into one mask per broadcaster. `SlotDiff::delta` would fill only the requested buffers (or bit storage, see [Optimization] Bitmask spans in SlotDelta); unrequested members stay empty.

Open questions:
- How does a listener declare its interests: a static member, a trait, or a concept on the members of `Delta` it reads?
- Does a view's own interest in its source depend on its listeners' interests? A filter whose listeners only want counts still needs created and deleted indices to keep `m_pass` correct.
- Should recording itself be skipped (not only the delta assembly), when no listener wants a change kind?

Addresses:
- [Hazard] Iteration over individual bits

Useful:
- [Note] Deltas and Diffs
- [Note] View lifetime and listeners
