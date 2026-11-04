# [Task] Demo example
Rewrite `examples/example1.cpp` as the basic demo, with asserts as a self-check (there are no tests yet).

1. Create a `SlotList<int>` with `std::make_shared` and a `Ledger`.
2. Get the same `Filter` (even numbers) over `From{list}` twice; assert both calls return the same view.
3. Add, modify and erase items; `flush()` the list, read the filter; assert its contents and delta after each step.
4. Erase and add again; assert the freed index is reused.
5. Drop the views, call `collect()` and assert the cache is empty.

Fixes:
- [Bug] example1 includes nonexistent headers
