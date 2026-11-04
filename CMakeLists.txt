cmake_minimum_required(VERSION 3.12)
project(squele)

include(GNUInstallDirs)

# Add the library headers
add_library(squele INTERFACE)
target_include_directories(
        squele
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(squele INTERFACE cxx_std_20)

# Install the headers and an importable target: find_package(squele)
install(TARGETS squele EXPORT squeleTargets)
install(DIRECTORY include/squele DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT squeleTargets
        NAMESPACE squele::
        FILE squeleConfig.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/squele)

# Add the examples
add_subdirectory(examples)

# Add the tests: ctest --test-dir build
enable_testing()
add_subdirectory(tests)
