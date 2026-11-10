# [Optimization] Word-level ExtendView bookkeeping
`ExtendView` asks every source whether a position is taken, one call per source per position (`readIndex`), both when it is constructed and for every touched position. The taken bits of a position are the AND of the sources' taken bits (a prefix of the smallest count for dense sources), so a whole word can be computed at once.

Useful:
- [Note] Derived views
- [Note] Slot storage

- [Task] Taken words from source bits
  Compute each touched word's new taken mask from the sources' `takenBits()` words; derive created and erased positions as masks against the view's own word, and read pointers only for the kept and created bits.
- [Task] Word-level construction
  Build the initial items by walking the AND of the sources' taken words, so construction makes no per-index `isTaken` calls.
