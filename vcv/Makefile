RACK_DIR ?= $(HOME)/Dev/Rack-SDK

FLAGS += -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
         -I../src/vendor/beads_dsp/include

SOURCES += $(wildcard ../src/*.cpp)
SOURCES += ../src/loooop/Loooop.cpp ../src/loooop/Lop.cpp
SOURCES += ../src/loooop/dsp/LoopEngine.cpp
SOURCES += ../src/loooop/display/LoopWaveformRenderer.cpp
SOURCES += $(wildcard ../src/mf20/*.cpp)
SOURCES += $(wildcard ../src/particules/*.cpp)
SOURCES += $(wildcard ../src/vendor/beads_dsp/src/*.cpp)
SOURCES += $(wildcard ../src/vendor/beads_dsp/src/*/*.cpp)

DISTRIBUTABLES += res plugin.json

include $(RACK_DIR)/plugin.mk

# Modules require C++17/20 (std::clamp, structured bindings, etc.)
CXXFLAGS := $(filter-out -std=c++11 -std=c++14, $(CXXFLAGS)) -std=c++20
