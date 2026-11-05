# [Hazard] Diff bookkeeping per listened view
Every view with listeners owns a `SlotDiff`, whose change blocks (four words per 64 slots, half a byte per slot) grow to the highest index ever recorded, and never shrink. In a tree, each listened view pays this again over the same index space, on top of its own flags (`m_pass` for a filter).

Useful:
- [Note] Deltas and Diffs
- [Note] Slot storage

## Severity

- **Memory is O(views x slotCount), mostly zero bytes.** Measured: 32 filters with listeners over a 1M-slot store took 41 MB after one bulk delta with one byte of kinds plus one touched bit per slot, and 15 to 19 MB with the bit-plane blocks (about 0.5 MB per view).
- A sparse store (grown large, then shrunk) keeps the full cost forever: the blocks are sized by the highest index, not by the item count.
- The span buffers grow to twice the largest touched count of any flush, and keep that size.
- A lazy filter (one not read every cycle) holds a second diff of its source's changes, so it pays the same again whether or not it has listeners.
- Speed is unaffected; the blocks are only read at touched words. No correctness risk.

## Possible efforts

- Bit-plane blocks (done, see [Optimization] Bit-plane SlotDiff) halved the cost.
- Page the blocks (for example 64 blocks per page, allocated on first touch and kept in a page table), so a sparse store pays for the pages its changes reach, not for its highest index.
