RACK_DIR ?= $(HOME)/Dev/Rack-SDK

# VCV Rack build. The MetaModule build lives in metamodule/ and shares src/,
# res/ and plugin.json with this one.

FLAGS += -Isrc -Isrc/loooop -Isrc/mf20

SOURCES += $(wildcard src/*.cpp)
SOURCES += src/loooop/Loooop.cpp src/loooop/Lop.cpp
SOURCES += src/loooop/dsp/LoopEngine.cpp
SOURCES += src/loooop/display/LoopWaveformRenderer.cpp
SOURCES += $(wildcard src/mf20/*.cpp)
SOURCES += $(wildcard src/lucarne/*.cpp)
SOURCES += $(wildcard src/shapes/*.cpp)

# Mutable Instruments Beads (vendored, src/beads/vendor/NOTICE.md) and the
# RobotBoy host adapter around it: Particules, Retours and Ondes run on it.
# -DTEST selects stmlib's portable code paths; -Dstmlib renames the vendored
# stmlib and clouds namespaces so they can't collide with other Mutable ports.
FLAGS += -Isrc/beads/vendor -Isrc/beads -DTEST -Dstmlib=robotboy_stmlib -Dclouds=robotboy_clouds
SOURCES += src/beads/vendor/beads/dsp/delay_clock.cc \
           src/beads/vendor/beads/dsp/delay_controller.cc \
           src/beads/vendor/beads/dsp/mu_law.cc \
           src/beads/vendor/beads/dsp/processor.cc \
           src/beads/vendor/beads/dsp/trigger_generator.cc \
           src/beads/vendor/beads/resources.cc \
           src/beads/vendor/stmlib/dsp/units.cc \
           src/beads/vendor/stmlib/utils/random.cc
SOURCES += $(wildcard src/beads/host/*.cpp)

DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk

# Modules require C++17/20 (std::clamp, structured bindings, etc.)
CXXFLAGS := $(filter-out -std=c++11 -std=c++14, $(CXXFLAGS)) -std=c++20
