# [Optimization] Bit-plane SlotDiff
---
done: true
---
Store the per-slot change kinds of `SlotDiff` as four bit planes per 64 slots instead of one byte per slot plus a separate touched bit. One block of four words (32 bytes, half a cache line) describes 64 slots:

- `touched`: recorded since the last flush.
- `existed`: held an item at the last flush.
- `exists`: holds an item now.
- `same`: still the item it held at the last flush (changed in place only).

The kinds are derived per word with no per-slot work: `created = exists & ~existed`, `deleted = existed & ~exists`, `replaced = existed & exists & ~same`, `modified = same`, `nothing = touched & ~existed & ~exists`. Recording is word operations on a mask `m` of indices (`n = m & ~touched` are the newly touched ones):

- create: `exists |= m; same &= ~m; touched |= m`
- erase: `existed |= n; exists &= ~m; same &= ~m; touched |= m`
- modify: `existed |= n; same |= n; exists |= m; touched |= m`

These compose exactly like the current `coalesce` table (a create after a delete gives `existed & exists & ~same`, a replace; a delete after a create clears both, nothing), so a single index and a whole word are recorded by the same code.

Effects:
- 0.5 byte per slot instead of 1.125, and one cache line per recorded index instead of two (the kind and the touched bit share a block).
- Bulk recording is O(touched words): `SlotFilterView` records a whole word's transitions with four mask operations instead of one `record()` per index.
- A listener no longer needs the source's `takenBits()` in its word loop: `exists` says which touched slots hold an item.
- `SlotDelta` exposes the blocks: `words` (a span of blocks indexed by word) replaces `touched` and `changes`; `touchedWords` stays. Spans are built by walking the derived masks, with no byte reads, into two buffers (two spans each, from both ends); a lone change per word is written branch-free to all spans.

Decisions:
- Interleaved blocks (one cache line per record) over separate planes (a contiguous `touched` that `BitSpan` could view). The consumers that read `touched` all walk `touchedWords`, so a strided block costs them nothing.
- The block type lives in `deltas/index.hpp` next to `SlotDelta`, since it is part of the delta's contract.
- `SlotChange` stays as the per-slot answer of `block.kind(bit)` for consumers that want a switch; the fuzz test keeps checking it.

Addresses:
- [Hazard] Diff bookkeeping per listened view
- [Hazard] Bit bookkeeping on sparse deltas

Useful:
- [Note] Deltas and Diffs
- [Note] Slot storage

## [Task] SlotWordChange block type
---
done: true
---
`struct SlotWordChange { BitWord touched, existed, exists, same; }` in `deltas/index.hpp`, with the derived masks (`created()`, `deleted()`, `replaced()`, `modified()`, `nothing()`), `kind(bit)` returning `SlotChange`, and the three recording operations taking a mask (`create(m)`, `erase(m)`, `modify(m)`), each asserting the impossible sequences (a create on an existing slot, an erase or modify on a free one). The fuzz test checks every two-step sequence against the current `coalesce` table before anything else changes.

Blocks:
- [Task] SlotDiff on bit planes

Useful:
- [Note] Deltas and Diffs

## [Task] SlotDiff on bit planes
---
done: true
---
`SlotDiff` keeps a 64-byte aligned `std::vector<SlotWordChange>` (grown on demand like `m_kinds` today) and `m_touchedWords`. `record(i, kind)` becomes the word form on a one-bit mask; new word-level entry points `createWord(w, m)`, `eraseWord(w, m)`, `modifyWord(w, m)` push `w` to `m_touchedWords` when its block was clear. `delta()` builds the spans by walking the derived masks of each touched word; `clear()` zeroes the touched blocks. The strong exception guarantee is kept: growth happens before any block changes. `touchedCount` is kept by popcount of newly touched bits.

Blocks:
- [Task] SlotDelta words member
- [Task] SlotDiff::fold

Useful:
- [Note] Deltas and Diffs

## [Task] SlotDelta words member
---
done: true
---
`SlotDelta::words` (`std::span<const SlotWordChange>`, indexed by word) replaces `touched` and `changes`; `touchedWords` stays. `IndexInterest::TOUCHED` and `CHANGES` merge into one flag `WORDS`. The fuzz `Mirror` checks `words` against the spans the way it checks the bit members now (every touched bit has a kind, every span index is touched, padding and untouched blocks are zero).

Blocks:
- [Task] Word-level recording in SlotFilterView

## [Task] Word-level recording in SlotFilterView
---
done: true
---
The word path of `onDelta` reads `words[at]`: the predicate runs on `touched & exists`, and the transitions are recorded with masks on the view's own diff: `createWord(at, next & ~old)`, `eraseWord(at, old & ~next)`, `modifyWord(at, old & next & touched & ~replaced)`, and for the kept replaced bits an `eraseWord` followed by a `createWord`. `takenBits()` is no longer read in the loop. The per-index path stays on the spans.

Blocks:
- [Task] Bit-plane results and docs

## [Task] Bit-plane results and docs
---
done: true
---
Results (A/B against the previous commit, best of 7, pinned): dense bulk deltas through two filters 7.0 to 3.3 ms (all modified) and 10.5 to 4.4 ms (all replaced); a dense depth-8 chain 46 to 16 ms; sparse cases within noise. Memory of 32 listened filters over 1M slots: 41 to 19 MB (dense) and 42 to 15 MB (sparse).

A/B benchmark against the previous commit (interleaved, pinned runs) on `benchmarks/slot.cpp`, dense and sparse, including the tree cases; keep the change only where the sparse cases do not regress. Record the memory per listened view before and after. Update the Notes "Slot storage" and "Deltas and Diffs", and the two Hazards.
