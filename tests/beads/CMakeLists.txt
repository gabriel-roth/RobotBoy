cmake_minimum_required(VERSION 3.14)
project(robotboy_beads_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(BEADS ${REPO_ROOT}/src/vendor/beads_dsp)

# beads DSP as a static lib — the same sources the plugin compiles.
file(GLOB_RECURSE BEADS_SRC CONFIGURE_DEPENDS ${BEADS}/src/*.cpp)
add_library(robotboy_beads_dsp STATIC ${BEADS_SRC})
target_include_directories(robotboy_beads_dsp PUBLIC ${BEADS}/include ${BEADS}/src)

# Catch2 (vendored amalgamated single-header + impl).
add_library(catch2_amalgamated STATIC ${CMAKE_CURRENT_LIST_DIR}/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated PUBLIC ${CMAKE_CURRENT_LIST_DIR}/catch2)

# Test executable — 16 verbatim upstream test files.
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)
add_executable(beads_tests ${TEST_SRC})
target_include_directories(beads_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/catch2      # shim <catch2/...> -> amalgamated
    ${BEADS}/include
    ${BEADS}/src
    ${REPO_ROOT}/src/particules)          # particules_block_runtime.h etc.
target_link_libraries(beads_tests PRIVATE robotboy_beads_dsp catch2_amalgamated)

# CTest: a single entry that runs the whole Catch2 binary. (catch_discover_tests
# needs the Catch2 CMake package, which the amalgamated distribution omits; the
# binary itself reports per-assertion results and returns non-zero on failure.)
enable_testing()
add_test(NAME beads_tests COMMAND beads_tests)
