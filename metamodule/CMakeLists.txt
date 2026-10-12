cmake_minimum_required(VERSION 3.22)

if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
else()
    message(FATAL_ERROR "METAMODULE_SDK_DIR is not set. Pass -DMETAMODULE_SDK_DIR=<path> "
        "or export the METAMODULE_SDK_DIR environment variable, pointing at a "
        "checkout of https://github.com/4ms/metamodule-plugin-sdk.")
endif()
include(${METAMODULE_SDK_DIR}/plugin.cmake)

set(SRC "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ ${CMAKE_CURRENT_LIST_DIR}/../plugin.json ROBOTBOY_PLUGIN_JSON)
string(JSON ROBOTBOY_VERSION GET ${ROBOTBOY_PLUGIN_JSON} version)
project(RobotBoy VERSION ${ROBOTBOY_VERSION} LANGUAGES C CXX ASM)

add_library(RobotBoy STATIC)
target_sources(RobotBoy PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/register.cc
    # VCV-adapter modules
    ${SRC}/src/plugin.cpp
    ${SRC}/src/mf20/MF20Filter.cpp
    ${SRC}/src/Onbetap.cpp
    ${SRC}/src/particules/Particules.cpp
    ${SRC}/src/particules/Ondes.cpp
    ${SRC}/src/Vespid.cpp
    ${SRC}/src/retours_delay/Retours.cpp
    # Native Loooop/Löp cores
    ${CMAKE_CURRENT_LIST_DIR}/loooop/LoooopCore.cc
    ${CMAKE_CURRENT_LIST_DIR}/loooop/LopCore.cc
    ${SRC}/src/loooop/dsp/LoopEngine.cpp
    ${SRC}/src/loooop/display/LoopWaveformRenderer.cpp
    # Mutable Instruments Beads (vendored) + RobotBoy host adapter:
    # Particules, Retours and Ondes (see src/beads/vendor/NOTICE.md)
    ${SRC}/src/beads/vendor/beads/dsp/delay_clock.cc
    ${SRC}/src/beads/vendor/beads/dsp/delay_controller.cc
    ${SRC}/src/beads/vendor/beads/dsp/mu_law.cc
    ${SRC}/src/beads/vendor/beads/dsp/processor.cc
    ${SRC}/src/beads/vendor/beads/dsp/trigger_generator.cc
    ${SRC}/src/beads/vendor/beads/resources.cc
    ${SRC}/src/beads/vendor/stmlib/dsp/units.cc
    ${SRC}/src/beads/vendor/stmlib/utils/random.cc
    ${SRC}/src/beads/host/engine_host.cpp
    ${SRC}/src/beads/host/gain_control.cpp
)
target_include_directories(RobotBoy PRIVATE
    ${SRC}/src
    ${SRC}/src/loooop
    ${SRC}/src/mf20
    ${SRC}/src/particules
    ${SRC}/src/retours_delay
    ${SRC}/src/beads
    ${SRC}/src/beads/vendor
    ${CMAKE_CURRENT_LIST_DIR}/loooop
)
target_compile_definitions(RobotBoy PRIVATE METAMODULE_BUILTIN ROBOTBOY_COMBINED)
# Vendored Beads: portable stmlib code paths (TEST) and a private stmlib
# namespace, so it can't collide with the simulator's own Mutable ports.
target_compile_definitions(RobotBoy PRIVATE TEST stmlib=robotboy_stmlib)
# The headless simulator builds us as a built-in like the GUI simulator does,
# but (unlike the GUI build) does not define SIMULATOR globally. register.cc
# relies on SIMULATOR to suppress its init()/pluginInstance definitions, which
# would otherwise collide with the simulator's generated init_RobotBoy.
if(METAMODULE_HEADLESS)
    target_compile_definitions(RobotBoy PRIVATE SIMULATOR)
endif()
# -funsafe-math-optimizations matches the VCV build (vcv/Makefile), so both
# platforms run the same numerics — and on Cortex-A7 it is what lets GCC
# auto-vectorize float loops with NEON at all (NEON is not IEEE-strict, so
# without it the resampler FIRs stay scalar). It does NOT imply
# -ffinite-math-only: isfinite()-based NaN recovery (engine sanitize paths)
# keeps working.
target_compile_options(RobotBoy PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
    -funsafe-math-optimizations)

create_plugin(
    SOURCE_LIB    RobotBoy
    PLUGIN_NAME   RobotBoy
    PLUGIN_JSON   ${SRC}/plugin.json
    SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION   ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins
)
