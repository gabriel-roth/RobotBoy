RACK_DIR ?= $(HOME)/Dev/Rack-SDK

# The canonical plugin.json and panel SVGs live at the repo root and are shared
# with the MetaModule build. Sync them into vcv/ HERE, at parse time (via
# $(shell ...)), because plugin.mk reads plugin.json for SLUG the moment it's
# included below — a normal target would run too late. These copies are
# git-ignored and regenerated on every invocation, so they can't drift from the
# canonical root files. Symlinks won't do: this platform's rsync silently skips
# symlinked distributables during `make dist`.
_sync := $(shell (cp ../plugin.json plugin.json && rm -rf res && cp -R ../res res) || echo SYNC_FAILED)
ifneq (,$(findstring SYNC_FAILED,$(_sync)))
$(error failed to sync plugin.json/res from repo root)
endif

FLAGS += -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
         -I../src/particules/dsp/include

SOURCES += $(wildcard ../src/*.cpp)
SOURCES += ../src/loooop/Loooop.cpp ../src/loooop/Lop.cpp
SOURCES += ../src/loooop/dsp/LoopEngine.cpp
SOURCES += ../src/loooop/display/LoopWaveformRenderer.cpp
SOURCES += $(wildcard ../src/mf20/*.cpp)
SOURCES += $(wildcard ../src/particules/*.cpp)
SOURCES += $(wildcard ../src/particules/dsp/src/*.cpp)
SOURCES += $(wildcard ../src/particules/dsp/src/*/*.cpp)

# Echos (beads-delay) reaches into particules_dsp/src's internal headers
# (buffer/, fx/, quality/, util/, random/ — see echos_processor.h), so its
# include path mirrors particules_dsp's own dsp/include *and* dsp/src, in
# addition to beadsdelay's own dsp/include and dsp/src (mirrors
# tests/beadsdelay_dsp/CMakeLists.txt's four include dirs).
FLAGS += -I../src/beadsdelay/dsp/include -I../src/beadsdelay/dsp/src \
         -I../src/particules/dsp/src

SOURCES += $(wildcard ../src/beadsdelay/*.cpp)
SOURCES += $(wildcard ../src/beadsdelay/dsp/src/*.cpp)
SOURCES += $(wildcard ../src/beadsdelay/dsp/src/*/*.cpp)

DISTRIBUTABLES += res plugin.json

include $(RACK_DIR)/plugin.mk

# Modules require C++17/20 (std::clamp, structured bindings, etc.)
CXXFLAGS := $(filter-out -std=c++11 -std=c++14, $(CXXFLAGS)) -std=c++20

# Rack's compile.mk maps our ../src/... sources to build/../src/....o, which
# normalizes to vcv/src/ — outside build/, so plugin.mk's clean misses them.
clean: clean-escaped-objects
.PHONY: clean-escaped-objects
clean-escaped-objects:
	@if [ -d src ]; then find src \( -name '*.o' -o -name '*.d' \) -delete; find src -type d -empty -delete; fi
