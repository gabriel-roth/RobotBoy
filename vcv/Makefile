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

FLAGS += -I../src -I../src/loooop -I../src/mf20 -I../src/particules

SOURCES += $(wildcard ../src/*.cpp)
SOURCES += ../src/loooop/Loooop.cpp ../src/loooop/Lop.cpp
SOURCES += ../src/loooop/dsp/LoopEngine.cpp
SOURCES += ../src/loooop/display/LoopWaveformRenderer.cpp
SOURCES += $(wildcard ../src/mf20/*.cpp)
SOURCES += $(wildcard ../src/particules/*.cpp)
SOURCES += $(wildcard ../src/lucarne/*.cpp)
SOURCES += $(wildcard ../src/retours_delay/*.cpp)

# Mutable Instruments Beads (vendored, src/beads/vendor/NOTICE.md) and the
# RobotBoy host adapter around it: Particules, Retours and Ondes run on it.
# -DTEST selects stmlib's portable code paths; -Dstmlib renames the vendored
# stmlib namespace so it can't collide with other Mutable ports.
FLAGS += -I../src/beads/vendor -I../src/beads -DTEST -Dstmlib=robotboy_stmlib
SOURCES += ../src/beads/vendor/beads/dsp/delay_clock.cc \
           ../src/beads/vendor/beads/dsp/delay_controller.cc \
           ../src/beads/vendor/beads/dsp/mu_law.cc \
           ../src/beads/vendor/beads/dsp/processor.cc \
           ../src/beads/vendor/beads/dsp/trigger_generator.cc \
           ../src/beads/vendor/beads/resources.cc \
           ../src/beads/vendor/stmlib/dsp/units.cc \
           ../src/beads/vendor/stmlib/utils/random.cc
SOURCES += $(wildcard ../src/beads/host/*.cpp)

# Classic mode: the pre-Beads (v2.1) engines, kept frozen for old patches.
# Retours' engine reaches into particules_dsp/src's internal headers.
FLAGS += -I../src/particules/dsp/include -I../src/particules/dsp/src \
         -I../src/retours_delay/dsp/include -I../src/retours_delay/dsp/src
SOURCES += $(wildcard ../src/particules/dsp/src/*.cpp)
SOURCES += $(wildcard ../src/particules/dsp/src/*/*.cpp)
SOURCES += $(wildcard ../src/retours_delay/dsp/src/*.cpp)
SOURCES += $(wildcard ../src/retours_delay/dsp/src/*/*.cpp)

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
