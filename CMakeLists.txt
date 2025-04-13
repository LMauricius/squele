cmake_minimum_required(VERSION 3.12)
project(squele)

# Add the library headers
add_library(squele INTERFACE)
target_include_directories(
        squele
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(squele PROPERTIES CXX_STANDARD 20)
set_target_properties(squele PROPERTIES CXX_STANDARD_REQUIRED ON)
target_compile_features(squele INTERFACE cxx_std_20)

install(TARGETS squele
        EXPORT ${PROJECT_NAME}_Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Add the examples
add_subdirectory(examples)