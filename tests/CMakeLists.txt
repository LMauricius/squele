add_executable(fuzz fuzz.cpp)
target_link_libraries(fuzz PRIVATE squele)
add_test(NAME fuzz COMMAND fuzz)
# Bounds checks in the standard containers, so out-of-range bookkeeping fails
target_compile_definitions(fuzz PRIVATE _GLIBCXX_ASSERTIONS)
