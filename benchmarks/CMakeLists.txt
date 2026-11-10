# Benchmarks are built but not run by ctest; build with -DCMAKE_BUILD_TYPE=Release
add_executable(slot slot.cpp)
target_link_libraries(slot PRIVATE squele)
add_executable(sort sort.cpp)
target_link_libraries(sort PRIVATE squele)
add_executable(extend extend.cpp)
target_link_libraries(extend PRIVATE squele)
