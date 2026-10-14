cmake_minimum_required(VERSION 3.14)
project(robotboy_beads_engine_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(BEADS ${REPO_ROOT}/src/beads)

# The vendored Beads DSP and RobotBoy's host adapter — the same sources and
# defines the plugin builds use (Makefile, metamodule/CMakeLists.txt).
add_library(robotboy_beads STATIC
    ${BEADS}/vendor/beads/dsp/delay_clock.cc
    ${BEADS}/vendor/beads/dsp/delay_controller.cc
    ${BEADS}/vendor/beads/dsp/mu_law.cc
    ${BEADS}/vendor/beads/dsp/processor.cc
    ${BEADS}/vendor/beads/dsp/trigger_generator.cc
    ${BEADS}/vendor/beads/resources.cc
    ${BEADS}/vendor/stmlib/dsp/units.cc
    ${BEADS}/vendor/stmlib/utils/random.cc
)
file(GLOB HOST_SRC CONFIGURE_DEPENDS ${BEADS}/host/*.cpp)
target_sources(robotboy_beads PRIVATE ${HOST_SRC})
target_include_directories(robotboy_beads PUBLIC ${BEADS}/vendor ${BEADS})
target_compile_definitions(robotboy_beads PUBLIC TEST stmlib=robotboy_stmlib)
target_compile_options(robotboy_beads PUBLIC -funsafe-math-optimizations -Wno-vla-cxx-extension -Wno-vla-extension -Wno-unknown-warning-option)

add_library(catch2_amalgamated STATIC ${CMAKE_CURRENT_LIST_DIR}/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated PUBLIC ${CMAKE_CURRENT_LIST_DIR}/catch2)

file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)
add_executable(beads_engine_tests ${TEST_SRC})
target_include_directories(beads_engine_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/catch2)
target_link_libraries(beads_engine_tests PRIVATE robotboy_beads catch2_amalgamated)

enable_testing()
add_test(NAME beads_engine_tests COMMAND beads_engine_tests)
