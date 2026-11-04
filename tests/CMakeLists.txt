add_executable(fuzz fuzz.cpp)
target_link_libraries(fuzz PRIVATE squele)
add_test(NAME fuzz COMMAND fuzz)
