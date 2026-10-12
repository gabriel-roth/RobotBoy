cmake_minimum_required(VERSION 3.14)
project(robotboy_retours_delay_dsp_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(RETOURS_DELAY_DSP ${REPO_ROOT}/src/retours_delay/dsp)
set(PARTICULES_DSP ${REPO_ROOT}/src/particules/dsp)
set(PARTICULES_TESTS ${REPO_ROOT}/tests/particules_dsp)

file(GLOB_RECURSE RETOURS_DELAY_DSP_SRC CONFIGURE_DEPENDS ${RETOURS_DELAY_DSP}/src/*.cpp)
file(GLOB_RECURSE PARTICULES_DSP_SRC CONFIGURE_DEPENDS ${PARTICULES_DSP}/src/*.cpp)
add_library(robotboy_retours_delay_dsp STATIC ${RETOURS_DELAY_DSP_SRC} ${PARTICULES_DSP_SRC})
target_include_directories(robotboy_retours_delay_dsp PUBLIC
    ${RETOURS_DELAY_DSP}/include ${RETOURS_DELAY_DSP}/src
    ${PARTICULES_DSP}/include ${PARTICULES_DSP}/src)

add_library(catch2_amalgamated STATIC ${PARTICULES_TESTS}/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated PUBLIC ${PARTICULES_TESTS}/catch2)

file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)
add_executable(retours_delay_dsp_tests ${TEST_SRC})
target_include_directories(retours_delay_dsp_tests PRIVATE
    ${PARTICULES_TESTS}/catch2
    ${RETOURS_DELAY_DSP}/include ${RETOURS_DELAY_DSP}/src
    ${PARTICULES_DSP}/include ${PARTICULES_DSP}/src)
target_link_libraries(retours_delay_dsp_tests PRIVATE robotboy_retours_delay_dsp catch2_amalgamated)

enable_testing()
add_test(NAME retours_delay_dsp_tests COMMAND retours_delay_dsp_tests)
