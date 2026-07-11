cmake_minimum_required(VERSION 3.14)
project(robotboy_particules_dsp_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PARTICULES_DSP ${REPO_ROOT}/src/particules/dsp)

# Particules granular DSP as a static lib — the same sources the plugin compiles.
file(GLOB_RECURSE PARTICULES_DSP_SRC CONFIGURE_DEPENDS ${PARTICULES_DSP}/src/*.cpp)
add_library(robotboy_particules_dsp STATIC ${PARTICULES_DSP_SRC})
target_include_directories(robotboy_particules_dsp PUBLIC ${PARTICULES_DSP}/include ${PARTICULES_DSP}/src)

# Catch2 (vendored amalgamated single-header + impl).
add_library(catch2_amalgamated STATIC ${CMAKE_CURRENT_LIST_DIR}/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated PUBLIC ${CMAKE_CURRENT_LIST_DIR}/catch2)

# Test executable — the granular-engine test files.
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)
add_executable(particules_dsp_tests ${TEST_SRC})
target_include_directories(particules_dsp_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/catch2      # shim <catch2/...> -> amalgamated
    ${PARTICULES_DSP}/include
    ${PARTICULES_DSP}/src
    ${REPO_ROOT}/src/particules)          # particules_block_runtime.h etc.
target_link_libraries(particules_dsp_tests PRIVATE robotboy_particules_dsp catch2_amalgamated)

# CTest: a single entry that runs the whole Catch2 binary. (catch_discover_tests
# needs the Catch2 CMake package, which the amalgamated distribution omits; the
# binary itself reports per-assertion results and returns non-zero on failure.)
enable_testing()
add_test(NAME particules_dsp_tests COMMAND particules_dsp_tests)
