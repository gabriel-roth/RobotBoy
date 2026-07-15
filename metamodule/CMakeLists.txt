cmake_minimum_required(VERSION 3.22)

if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "/Users/gabrielroth/Dev/metamodule-plugin-sdk")
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
    ${SRC}/src/particules/Particules.cpp
    ${SRC}/src/particules/Ondes.cpp
    ${SRC}/src/beadsdelay/Echos.cpp
    # Native Loooop/Löp cores
    ${CMAKE_CURRENT_LIST_DIR}/loooop/LoooopCore.cc
    ${CMAKE_CURRENT_LIST_DIR}/loooop/LopCore.cc
    ${SRC}/src/loooop/dsp/LoopEngine.cpp
    ${SRC}/src/loooop/display/LoopWaveformRenderer.cpp
    # granular DSP engine (Particules)
    ${SRC}/src/particules/dsp/src/particules_processor.cpp
    ${SRC}/src/particules/dsp/src/buffer/recording_buffer.cpp
    ${SRC}/src/particules/dsp/src/fx/reverb.cpp
    ${SRC}/src/particules/dsp/src/fx/saturation.cpp
    ${SRC}/src/particules/dsp/src/grain/grain_engine.cpp
    ${SRC}/src/particules/dsp/src/grain/grain_scheduler.cpp
    ${SRC}/src/particules/dsp/src/grain/grain.cpp
    ${SRC}/src/particules/dsp/src/input/auto_gain.cpp
    ${SRC}/src/particules/dsp/src/pitch/pitch_quantizer.cpp
    ${SRC}/src/particules/dsp/src/quality/quality_processor.cpp
    ${SRC}/src/particules/dsp/src/random/attenurandomizer.cpp
    ${SRC}/src/particules/dsp/src/random/random.cpp
    ${SRC}/src/particules/dsp/src/wavetable/wavetable_oscillator.cpp
    # Échos delay DSP engine
    ${SRC}/src/beadsdelay/dsp/src/echos_processor.cpp
    ${SRC}/src/beadsdelay/dsp/src/time/base_time.cpp
    ${SRC}/src/beadsdelay/dsp/src/engine/echo_engine.cpp
    ${SRC}/src/beadsdelay/dsp/src/pitch/rotary_shifter.cpp
)
target_include_directories(RobotBoy PRIVATE
    ${SRC}/src
    ${SRC}/src/loooop
    ${SRC}/src/mf20
    ${SRC}/src/particules
    ${SRC}/src/particules/dsp/include
    ${SRC}/src/particules/dsp/src
    ${SRC}/src/beadsdelay
    ${SRC}/src/beadsdelay/dsp/include
    ${SRC}/src/beadsdelay/dsp/src
    ${CMAKE_CURRENT_LIST_DIR}/loooop
)
target_compile_definitions(RobotBoy PRIVATE METAMODULE_BUILTIN ROBOTBOY_COMBINED)
target_compile_options(RobotBoy PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>)

create_plugin(
    SOURCE_LIB    RobotBoy
    PLUGIN_NAME   RobotBoy
    PLUGIN_JSON   ${SRC}/plugin.json
    SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION   ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins
)
