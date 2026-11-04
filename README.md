# SQueLe
**S**lice **Que**ry **Le**dger [squeal] - A modern C++ data query library

# Dependencies
- C++20

# Installing
SQueLe is header-only. Install it with CMake:

```sh
cmake -S . -B build
cmake --install build --prefix <prefix>
```

Then use it from your own CMake project:

```cmake
find_package(squele REQUIRED)
target_link_libraries(<target> PRIVATE squele::squele)
```

You can also copy the `include/squele` folder to any include location.
