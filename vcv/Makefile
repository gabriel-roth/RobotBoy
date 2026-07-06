RACK_DIR ?= $(HOME)/Dev/Rack-SDK

# The canonical plugin.json and panel SVGs live at the repo root and are shared
# with the MetaModule build. Sync them into vcv/ HERE, at parse time (via
# $(shell ...)), because plugin.mk reads plugin.json for SLUG the moment it's
# included below — a normal target would run too late. These copies are
# git-ignored and regenerated on every invocation, so they can't drift from the
# canonical root files. Symlinks won't do: this platform's rsync silently skips
# symlinked distributables during `make dist`.
_sync := $(shell cp ../plugin.json plugin.json && rm -rf res && cp -R ../res res)

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
