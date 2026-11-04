# Known issues: build

- [Bug] example1 includes nonexistent headers
  ---
  Severity: build
  File: examples/example1.cpp
  ---
  Includes `squele/collection.hpp`, `squele/composable/*`, `squele/core_concepts.hpp` and `squele/views/sorted.hpp`, none of which exist.
- [Bug] Incomplete CMake install rules
  ---
  Severity: build
  File: CMakeLists.txt
  ---
  Uses `CMAKE_INSTALL_*DIR` without `include(GNUInstallDirs)`; the `EXPORT` has no `install(EXPORT ...)`, and headers are never installed.
- [Bug] IndexTable getUnused has no return
  ---
  Severity: ub
  File: include/squele/bits/indexTable.hpp
  ---
  Empty body with a non-void return type. The header also relies on a transitive `<concepts>` include for `std::integral`.
