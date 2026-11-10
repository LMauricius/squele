# [Optimization] Listener change interests
---
done: true
---
Let each listener declare which kinds of changes it is interested in (for example created, deleted, replaced, modified, moved, or only the counts). A source then builds only the parts of its `Delta` that at least one listener needs, and skips the rest.

Listeners add and remove their interests at run time, and the broadcaster combines them into one mask. `SlotDiff::delta` fills only the requested buffers and bit members (see [Optimization] Bitmask spans in SlotDelta); unrequested members stay empty.

Decisions:
- Interests are flags of `IndexInterest` (a namespace), one per `SlotDelta` member, so index spans and bit members are requested separately. `count` and `slotCount` are always filled, so a listener with no interests receives only the counts.
- `Broadcaster` keeps a raw count per flag (`addInterests`, `removeInterests`) and caches the mask of non-zero counts (`interests()`). Interests are independent of `addListener`: a listener adds them right before or after it is added, and removes them when it stops listening. Correct requests are the listener's duty; a member nobody asked for is silently empty.
- A view's interests in its source do not depend on its own listeners: `SlotFilterView` always asks for every member except `moved`, since `m_pass` needs every change.
- Recording is not skipped: a counts-only listener must still be notified when something changed.

Addresses:
- [Hazard] Iteration over individual bits

Useful:
- [Note] Deltas and Diffs
- [Note] View lifetime and listeners

## [Task] Interest counting in Broadcaster
---
done: true
---
`IndexInterest` flags (one per `SlotDelta` member, plus `ALL`) and `SlotDelta::Interests`. `Broadcaster` counts each flag and caches the non-zero mask; `Listenable` requires `addInterests` and `removeInterests`.

## [Task] Selective SlotDiff::delta
---
done: true
---
`SlotDiff::delta` takes the wanted mask. It skips the touched walk when no index span is wanted, pushes only wanted kinds (a table indexed by `SlotChange`), and leaves unwanted bit members empty. `SlotStore` and `SlotFilterView` pass `interests()`; the filter adds its fixed source interests in its constructor and removes them in its destructor. The fuzz test checks the counting and that unwanted members stay empty.

