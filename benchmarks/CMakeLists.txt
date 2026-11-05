# Benchmarks are built but not run by ctest; build with -DCMAKE_BUILD_TYPE=Release
add_executable(slot slot.cpp)
target_link_libraries(slot PRIVATE squele)
