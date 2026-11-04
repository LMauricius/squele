# [Task] Demo example
Rewrite `examples/example1.cpp` as the basic demo, with asserts as a self-check (there are no tests yet).

1. Create a `SlotList<int>` with `std::make_shared` and a `Ledger`.
2. Get the same `Filter` (even numbers) over `From{list}` twice; assert both calls return the same view.
3. Register a small recording listener on the filter view. Remove it before step 6.
4. Add, modify and erase items; call `sync()` and `flush()` on the filter (the sync pulls the list); assert its contents and the recorded delta after each step.
5. Erase and add again; assert the freed index is reused.
6. Keep a `weak_ptr` to the filter view, drop the `shared_ptr`s, call `collect()` and assert the `weak_ptr` expired.

Fixes:
- [Bug] example1 includes nonexistent headers
