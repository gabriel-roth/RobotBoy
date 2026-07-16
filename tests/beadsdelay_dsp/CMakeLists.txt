cmake_minimum_required(VERSION 3.14)
project(robotboy_beadsdelay_dsp_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(BEADSDELAY_DSP ${REPO_ROOT}/src/beadsdelay/dsp)
set(PARTICULES_DSP ${REPO_ROOT}/src/particules/dsp)
set(PARTICULES_TESTS ${REPO_ROOT}/tests/particules_dsp)

file(GLOB_RECURSE BEADSDELAY_DSP_SRC CONFIGURE_DEPENDS ${BEADSDELAY_DSP}/src/*.cpp)
file(GLOB_RECURSE PARTICULES_DSP_SRC CONFIGURE_DEPENDS ${PARTICULES_DSP}/src/*.cpp)
add_library(robotboy_beadsdelay_dsp STATIC ${BEADSDELAY_DSP_SRC} ${PARTICULES_DSP_SRC})
target_include_directories(robotboy_beadsdelay_dsp PUBLIC
    ${BEADSDELAY_DSP}/include ${BEADSDELAY_DSP}/src
    ${PARTICULES_DSP}/include ${PARTICULES_DSP}/src)

add_library(catch2_amalgamated STATIC ${PARTICULES_TESTS}/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated PUBLIC ${PARTICULES_TESTS}/catch2)

file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)
add_executable(beadsdelay_dsp_tests ${TEST_SRC})
target_include_directories(beadsdelay_dsp_tests PRIVATE
    ${PARTICULES_TESTS}/catch2
    ${BEADSDELAY_DSP}/include ${BEADSDELAY_DSP}/src
    ${PARTICULES_DSP}/include ${PARTICULES_DSP}/src)
target_link_libraries(beadsdelay_dsp_tests PRIVATE robotboy_beadsdelay_dsp catch2_amalgamated)

enable_testing()
add_test(NAME beadsdelay_dsp_tests COMMAND beadsdelay_dsp_tests)
