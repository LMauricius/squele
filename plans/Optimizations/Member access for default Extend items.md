# [Optimization] Member access for default Extend items
`ExtendView` rebuilds whole items and keeps a parallel array of source pointers, because a named aggregate offers no member access by position in C++20. The default items (`std::pair`, `std::tuple`) do offer it (`std::get`), so for them the view could drop the pointer array and update items in place. Measured on 1M items (`benchmarks/extend.cpp`, Release): construction takes 9.4 ms against 3.5 ms for a `Map` of the same function, and 104857 modified items 1.8 ms against 0.6 ms.

Addresses:
- [Hazard] Rebuilds on shifted positions in extends

Useful:
- [Note] Derived views

- [Task] Pointer reads from default items
  Read a position's source pointers from its item with `std::get` when `Item` is the default, and keep `m_ptrs` (as a zero-size member otherwise) only for named items. Saves 8 bytes per source per position and one write per rebuild.
- [Task] In-place updates of default items
  Rewrite only the pointers of a kept position, and recompute results in place; a swap in one of several sources then needs no destroy and construct, only pointer writes and function calls.
- [Task] Opt-in member access for named items
  An optional customization point (for example a `members()` function returning a tuple of references) would give named items the same paths. Open: whether it is worth one more user-facing rule.
