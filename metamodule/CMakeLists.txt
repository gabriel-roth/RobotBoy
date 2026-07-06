cmake_minimum_required(VERSION 3.22)

if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "/Users/gabrielroth/Dev/metamodule-plugin-sdk")
endif()
include(${METAMODULE_SDK_DIR}/plugin.cmake)

set(SRC "${CMAKE_CURRENT_LIST_DIR}/..")
project(Foobar VERSION 2.0.0 LANGUAGES C CXX ASM)

add_library(Foobar STATIC)
target_sources(Foobar PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/register.cc
    # VCV-adapter modules
    ${SRC}/src/plugin.cpp
    ${SRC}/src/mf20/MF20Filter.cpp
    ${SRC}/src/particules/Particules.cpp
    # Native Loooop/Löp cores
    ${CMAKE_CURRENT_LIST_DIR}/loooop/LoooopCore.cc
    ${CMAKE_CURRENT_LIST_DIR}/loooop/LopCore.cc
    ${SRC}/src/loooop/dsp/LoopEngine.cpp
    ${SRC}/src/loooop/display/LoopWaveformRenderer.cpp
    # beads DSP (Particules)
    ${SRC}/src/vendor/beads_dsp/src/beads_processor.cpp
    ${SRC}/src/vendor/beads_dsp/src/buffer/recording_buffer.cpp
    ${SRC}/src/vendor/beads_dsp/src/delay/delay_engine.cpp
    ${SRC}/src/vendor/beads_dsp/src/fx/reverb.cpp
    ${SRC}/src/vendor/beads_dsp/src/fx/saturation.cpp
    ${SRC}/src/vendor/beads_dsp/src/grain/grain_engine.cpp
    ${SRC}/src/vendor/beads_dsp/src/grain/grain_scheduler.cpp
    ${SRC}/src/vendor/beads_dsp/src/grain/grain.cpp
    ${SRC}/src/vendor/beads_dsp/src/input/auto_gain.cpp
    ${SRC}/src/vendor/beads_dsp/src/pitch/pitch_quantizer.cpp
    ${SRC}/src/vendor/beads_dsp/src/quality/quality_processor.cpp
    ${SRC}/src/vendor/beads_dsp/src/random/attenurandomizer.cpp
    ${SRC}/src/vendor/beads_dsp/src/random/random.cpp
    ${SRC}/src/vendor/beads_dsp/src/wavetable/wavetable_oscillator.cpp
)
target_include_directories(Foobar PRIVATE
    ${SRC}/src
    ${SRC}/src/loooop
    ${SRC}/src/mf20
    ${SRC}/src/particules
    ${SRC}/src/vendor/beads_dsp/include
    ${CMAKE_CURRENT_LIST_DIR}/loooop
)
target_compile_definitions(Foobar PRIVATE METAMODULE_BUILTIN FOOBAR_COMBINED)
target_compile_options(Foobar PRIVATE -std=c++20)

create_plugin(
    SOURCE_LIB    Foobar
    PLUGIN_NAME   Foobar
    PLUGIN_JSON   ${SRC}/plugin.json
    SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION   ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins
)
