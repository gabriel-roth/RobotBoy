# Particules Track Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the accepted Particules items from the 2026-07-09 backlog spec: F3 (scale-aware pitch lock), F5 (grain-count LED), F6 (input-level readout), F7 (grain-trigger pulse separation), F8 (menu undo, VCV-only), Q9 (dry-post-gain option, default ON), P1 (reverb idle sleep).

**Architecture:** Engine-side changes go in `src/vendor/beads_dsp/` (reverb sleep, dry-tap option, parameters). Wrapper-side changes go in `src/particules/` (block runtime, new scale/format helper headers, menu + JSON wiring in `Particules.cpp`). Headless-testable logic lives in headers or engine sources covered by the two test lanes; Rack-only behavior (menus, undo) is compile-checked and goes on the user-run checklist.

**Tech Stack:** C++ (Rack SDK v2 / MetaModule SDK), Catch2 lane (`tests/beads/`), g++ assert lane (`tests/run.sh`), CMake for MetaModule.

**Source spec:** `docs/superpowers/specs/2026-07-09-feature-and-perf-backlog-design.md` (do not modify it).

## Global Constraints

- All work happens in the worktree `/Users/gabrielroth/Dev/RobotBoy/.worktrees/particules-track` on branch `particules-track`. Every path below is relative to that worktree root.
- Two test lanes, both must pass before each commit that touches their code:
  - g++ assert lane: `tests/run.sh` (also runs the python guard tests). Exit 0 = pass.
  - Catch2 lane: `tests/beads/run.sh` (CMake + CTest). Expect `100% tests passed`.
- Compile check for wrapper changes (no Rack test lane exists): `cd vcv && make -j8` (RACK_DIR defaults to `$HOME/Dev/Rack-SDK`). A clean build is the check.
- **No new ParamIds, no panel changes** anywhere in this track — everything is context-menu + JSON. Therefore no `metamodule/*_info.hh` param-sync work is needed.
- Q9 menu option is "Dry signal follows input gain", **default ON** (post-gain dry out of the box). This deliberately changes default behavior for existing patches — decided 2026-07-10.
- F8 undo wraps discrete menu items only; the manual-gain slider is excluded — decided 2026-07-10.
- GUI/simulator verification is **user-run only** (per project policy): collect manual checks in the final checklist, never drive the simulator from an agent.
- Commit messages: short, one sentence, ≤15 words, no AI attribution lines.
- `#ifdef METAMODULE` guards Rack-only code, matching existing patterns in `Particules.cpp`.

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `src/particules/particules_block_runtime.h` | Modify | F7 pulse separation, F5 LED density mapping |
| `src/particules/particules_gain_display.h` | Create | F6 linear→dB label formatting (headless-testable) |
| `src/particules/particules_scales.h` | Create | F3 scale tables + ratio builder (headless-testable) |
| `src/particules/Particules.cpp` | Modify | F3/F5/F6/Q9 wiring, menus, JSON; F8 undo helper |
| `src/vendor/beads_dsp/include/beads/parameters.h` | Modify | Q9 `dry_post_gain` flag |
| `src/vendor/beads_dsp/src/beads_processor.cpp` | Modify | Q9 dry-tap move |
| `src/vendor/beads_dsp/src/fx/fx_engine.h` | Modify | P1 `DelayLine::ClearData()` |
| `src/vendor/beads_dsp/src/fx/reverb.h` / `reverb.cpp` | Modify | P1 idle sleep |
| `tests/beads/test_particules_block_runtime.cpp` | Modify | F7 + F5 tests |
| `tests/beads/test_processor.cpp` | Modify | Q9 tests |
| `tests/beads/test_reverb.cpp` | Modify | P1 tests |
| `tests/particules/test_gain_display.cpp` | Create | F6 tests |
| `tests/particules/test_particules_scales.cpp` | Create | F3 table tests |

Task order matters only in two places: F8 (Task 7) wraps menu items created by F3 (Task 4) and Q9 (Task 5), so it comes after them. Everything else is independent.

---

### Task 1: F7 — Grain-trigger pulse separation

**Files:**
- Modify: `src/particules/particules_block_runtime.h` (StartGrainTriggerPulse/ConsumeTriggerPulseSample, currently lines 52–62)
- Test: `tests/beads/test_particules_block_runtime.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: same two method signatures, new semantics — a `StartGrainTriggerPulse(int)` arriving while a pulse is running (or during the gap sample) queues the new pulse and forces exactly one low sample between pulses. `Particules.cpp` call sites are unchanged.

**Why:** `StartGrainTriggerPulse` overwrites the remaining-samples counter, and a 1 ms pulse is shorter than the ~1.33 ms block, so back-to-back block triggers merge into a continuous high level on the R output. Downstream triggers see one event instead of many.

- [ ] **Step 1: Write the failing tests**

Append to `tests/beads/test_particules_block_runtime.cpp`:

```cpp
TEST_CASE("ParticulesBlockRuntime: back-to-back trigger pulses get one separating low sample", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;

    runtime.StartGrainTriggerPulse(3);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);

    // Retrigger while the first pulse still has one sample to run.
    runtime.StartGrainTriggerPulse(3);

    // Current pulse runs down...
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    // ...then exactly one forced low sample...
    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);
    // ...then the pending pulse runs in full.
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);
}

TEST_CASE("ParticulesBlockRuntime: single trigger pulse behavior is unchanged", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;

    runtime.StartGrainTriggerPulse(2);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);

    // A pulse started when idle starts immediately (no spurious gap).
    runtime.StartGrainTriggerPulse(1);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);
}

TEST_CASE("ParticulesBlockRuntime: retrigger during the gap replaces the pending pulse", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;

    runtime.StartGrainTriggerPulse(2);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    runtime.StartGrainTriggerPulse(5);   // queued: pulse still running
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);   // first pulse drains
    runtime.StartGrainTriggerPulse(3);   // arrives in the gap window: replaces the 5
    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);  // the single gap sample
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);
}
```

- [ ] **Step 2: Run the Catch2 lane to verify they fail**

Run: `tests/beads/run.sh`
Expected: build succeeds, `beads_tests` FAILS on the new "back-to-back" and "gap replaces" cases (the merged-pulse behavior returns `true` where the tests demand the low sample).

- [ ] **Step 3: Implement pulse separation**

In `src/particules/particules_block_runtime.h`, replace the two methods (lines 52–62):

```cpp
    // A Start arriving while a pulse is running (or during the gap sample)
    // queues the new pulse behind exactly one forced low sample, so
    // downstream Schmitt triggers see separate events instead of one long
    // gate (1 ms pulses are shorter than the ~1.33 ms block, so dense
    // grain clouds otherwise merge into a DC-ish high level).
    void StartGrainTriggerPulse(int samples) {
        int n = samples > 0 ? samples : 0;
        if (grain_trigger_remaining_ > 0 || gap_pending_) {
            pending_pulse_ = n;
            gap_pending_ = true;
        } else {
            grain_trigger_remaining_ = n;
        }
    }

    bool ConsumeTriggerPulseSample() {
        if (grain_trigger_remaining_ > 0) {
            --grain_trigger_remaining_;
            return true;
        }
        if (gap_pending_) {
            // This sample is the forced low; the pending pulse starts next.
            gap_pending_ = false;
            grain_trigger_remaining_ = pending_pulse_;
            pending_pulse_ = 0;
            return false;
        }
        return false;
    }
```

And add the two members next to `grain_trigger_remaining_` in the private section:

```cpp
    int pending_pulse_ = 0;
    bool gap_pending_ = false;
```

- [ ] **Step 4: Run both test lanes**

Run: `tests/beads/run.sh` — Expected: `100% tests passed`.
Run: `tests/run.sh` — Expected: exit 0 (this header isn't in the g++ lane, but keep the invariant of running both).

- [ ] **Step 5: Commit**

```bash
git add src/particules/particules_block_runtime.h tests/beads/test_particules_block_runtime.cpp
git commit -m "feat: separate back-to-back grain trigger pulses with one low sample"
```

---

### Task 2: F5 — Grain-count LED density

**Files:**
- Modify: `src/particules/particules_block_runtime.h`
- Modify: `src/particules/Particules.cpp` (process(), lines 407–418)
- Test: `tests/beads/test_particules_block_runtime.cpp`

**Interfaces:**
- Consumes: `processor_.ActiveGrainCount()` (`beads/beads.h:36`, already exists) and `processor_.GrainTriggeredThisBlock()`.
- Produces: `void NoteGrainActivity(int active_count, bool triggered)` on `ParticulesBlockRuntime` — sets the LED to a density-scaled brightness, never dimming an already-brighter LED. `SetGrainLed`/`GrainLed`/`DecayGrainLed` unchanged.

**Why:** The LED is currently a boolean flash (any grain → full brightness). Scaling brightness by `ActiveGrainCount()` (floor 0.25 for one grain, full at ~10) makes it read as grain density. Constants are taste — tuned later on the simulator (user-run).

- [ ] **Step 1: Write the failing tests**

Append to `tests/beads/test_particules_block_runtime.cpp`:

```cpp
TEST_CASE("ParticulesBlockRuntime: grain LED scales with active grain count", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;

    // One grain: visible floor.
    runtime.NoteGrainActivity(1, true);
    REQUIRE(runtime.GrainLed() == Approx(0.25f + 0.75f / 10.0f));

    // Ten or more grains: full bright, clamped at 1.
    runtime.SetGrainLed(0.0f);
    runtime.NoteGrainActivity(10, true);
    REQUIRE(runtime.GrainLed() == Approx(1.0f));
    runtime.SetGrainLed(0.0f);
    runtime.NoteGrainActivity(30, true);
    REQUIRE(runtime.GrainLed() == Approx(1.0f));

    // Triggered this block but count snapshot is 0 (grain died within the
    // block): still registers at the floor.
    runtime.SetGrainLed(0.0f);
    runtime.NoteGrainActivity(0, true);
    REQUIRE(runtime.GrainLed() == Approx(0.25f));

    // No activity at all: LED untouched.
    runtime.SetGrainLed(0.1f);
    runtime.NoteGrainActivity(0, false);
    REQUIRE(runtime.GrainLed() == Approx(0.1f));

    // Never dims a brighter LED (decay machinery owns dimming).
    runtime.SetGrainLed(1.0f);
    runtime.NoteGrainActivity(1, true);
    REQUIRE(runtime.GrainLed() == Approx(1.0f));
}
```

- [ ] **Step 2: Run the Catch2 lane to verify it fails**

Run: `tests/beads/run.sh`
Expected: FAILS with "no member named 'NoteGrainActivity'" (compile error counts as the failing state; note it and move on).

- [ ] **Step 3: Implement the mapping**

In `src/particules/particules_block_runtime.h`, add below `SetGrainLed`/`GrainLed` (after line 65):

```cpp
    // Grain-density LED: one grain still flashes visibly (floor 0.25),
    // ~10+ concurrent grains reads full-bright. `triggered` is the
    // any-activity OR-in so grains whose whole lifetime fits inside one
    // block still register even when the count snapshot missed them.
    // Only ever brightens — DecayGrainLed() owns dimming.
    void NoteGrainActivity(int active_count, bool triggered) {
        float target = 0.0f;
        if (active_count > 0) {
            target = std::min(1.0f, 0.25f + 0.75f * static_cast<float>(active_count) / 10.0f);
        } else if (triggered) {
            target = 0.25f;
        }
        if (target > grain_led_) grain_led_ = target;
    }
```

- [ ] **Step 4: Run the Catch2 lane**

Run: `tests/beads/run.sh` — Expected: `100% tests passed`.

- [ ] **Step 5: Wire the wrapper**

In `src/particules/Particules.cpp`, replace (currently lines 407–415):

```cpp
			bool triggered = processor_.GrainTriggeredThisBlock();
			block_runtime_.CommitProcessedBlock(scratch_output_buf_, kWrapperBlockSize);
			if (triggered) {
				block_runtime_.SetGrainLed(1.f);
				if (grain_trigger_out_) {
					block_runtime_.StartGrainTriggerPulse(
						static_cast<int>(args.sampleRate * 0.001f));
				}
			}
```

with:

```cpp
			bool triggered = processor_.GrainTriggeredThisBlock();
			block_runtime_.CommitProcessedBlock(scratch_output_buf_, kWrapperBlockSize);
			block_runtime_.NoteGrainActivity(processor_.ActiveGrainCount(), triggered);
			if (triggered && grain_trigger_out_) {
				block_runtime_.StartGrainTriggerPulse(
					static_cast<int>(args.sampleRate * 0.001f));
			}
```

- [ ] **Step 6: Compile check the wrapper**

Run: `cd vcv && make -j8`
Expected: clean build, no warnings about `NoteGrainActivity`.

- [ ] **Step 7: Commit**

```bash
git add src/particules/particules_block_runtime.h src/particules/Particules.cpp tests/beads/test_particules_block_runtime.cpp
git commit -m "feat: grain LED brightness tracks active grain count"
```

---

### Task 3: F6 — Input-level readout in the gain menu

**Files:**
- Create: `src/particules/particules_gain_display.h`
- Create: `tests/particules/test_gain_display.cpp`
- Modify: `src/particules/Particules.cpp` (includes; context menu after the ManualGainItem block, ~line 556)

**Interfaces:**
- Consumes: `processor_.InputLevel()` (`beads/beads.h:38`; linear peak envelope, ±5 V → 1.0, ~500 ms release).
- Produces: `std::string FormatInputLevelDb(float linear_level)` — `"−18.4 dB"`-style label, `"silent"` below −60 dB.

- [ ] **Step 1: Write the failing test**

Create `tests/particules/test_gain_display.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include "../../src/particules/particules_gain_display.h"

int main() {
    assert(FormatInputLevelDb(1.0f)  == "0.0 dB");
    assert(FormatInputLevelDb(0.5f)  == "-6.0 dB");   // 20*log10(0.5) = -6.02
    assert(FormatInputLevelDb(0.1f)  == "-20.0 dB");

    // At/below the -60 dB floor (10^(-60/20) = 0.001) reads as silent.
    assert(FormatInputLevelDb(0.0f)     == "silent");
    assert(FormatInputLevelDb(0.0009f)  == "silent");
    assert(FormatInputLevelDb(0.001f)   == "silent");

    // Garbage in never renders garbage out.
    assert(FormatInputLevelDb(std::nanf(""))   == "silent");
    assert(FormatInputLevelDb(-0.5f)           == "silent");

    std::printf("test_gain_display: all assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Run the g++ lane to verify it fails**

Run: `tests/run.sh`
Expected: FAILS building `test_gain_display.cpp` — `particules_gain_display.h: No such file or directory`.

- [ ] **Step 3: Implement the formatter**

Create `src/particules/particules_gain_display.h`:

```cpp
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

// Format AutoGain::InputLevel() (linear peak, ±5 V → 1.0) for the gain
// section of the context menu. At or below -60 dB (the AutoGain::kMinGainDb
// floor) the level reads as "silent". The negated comparison also routes
// NaN and negative inputs to "silent".
inline std::string FormatInputLevelDb(float linear_level) {
    if (!(linear_level > 0.001f))
        return "silent";
    float db = 20.0f * std::log10(linear_level);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f dB", db);
    return buf;
}
```

- [ ] **Step 4: Run the g++ lane**

Run: `tests/run.sh`
Expected: `test_gain_display: all assertions passed`, overall exit 0.

- [ ] **Step 5: Add the menu entry**

In `src/particules/Particules.cpp`:

Add the include after line 6 (`#include "particules_density_control.h"`):

```cpp
#include "particules_gain_display.h"
```

In `ParticulesWidget::appendContextMenu`, insert after the ManualGainItem block (after the closing brace of the `{ auto* item = new ManualGainItem; ... }` scope, ~line 556) and before the SEED CV section:

```cpp
		// --- Input level readout (display-only) ---
		// On VCV, step() refreshes the label live while the menu is open.
		// On MetaModule menus don't animate, so the label is computed once
		// at menu-open time — acceptable.
		struct InputLevelItem : MenuItem {
			Particules* module;
#ifndef METAMODULE
			void step() override {
				rightText = FormatInputLevelDb(module->processor_.InputLevel());
				MenuItem::step();
			}
#endif
		};
		{
			auto* item = new InputLevelItem;
			item->module = module;
			item->text = "Input";
			item->rightText = FormatInputLevelDb(module->processor_.InputLevel());
			item->disabled = true;
			menu->addChild(item);
		}
```

- [ ] **Step 6: Compile check**

Run: `cd vcv && make -j8`
Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add src/particules/particules_gain_display.h tests/particules/test_gain_display.cpp src/particules/Particules.cpp
git commit -m "feat: live input-level readout in the gain menu"
```

---

### Task 4: F3 — Scale-aware pitch lock

**Files:**
- Create: `src/particules/particules_scales.h`
- Create: `tests/particules/test_particules_scales.cpp`
- Modify: `src/particules/Particules.cpp` (member state, JSON, process(), menu)

**Interfaces:**
- Consumes: `processor_.LoadScale(const double*, uint32_t)` / `ClearScale()` / `SetScaleRoot(int midi)` (`beads/beads.h:44-46`); `PitchQuantizer::loadRatios` contract: ratios ascending, 1/1-relative (degree 0 implicit), last entry is the period (2.0). Engine applies the scale quantizer *before* `pitch_lock` (`grain_engine.cpp:148-151`), so a unified selector must never set both.
- Produces: `particules::ScaleSemitones(int mode, uint32_t* count)`, `particules::BuildScaleRatios(const int*, uint32_t, double*)`, `particules::kMaxScaleNotes`, enum `particules::PitchScaleMode` (0–7). Module member `pitch_scale_` (replaces `pitch_lock_`), `pitch_root_`, `scale_dirty_`, and `ApplyScaleToProcessor()`. JSON keys `pitchScale` (0–7) and `pitchRoot` (0–11); legacy `pitchLock` (0–2) still read on load.

**Threading note (why `scale_dirty_`):** `LoadScale` writes a 128-double table the audio thread reads per grain. Menu callbacks run on the UI thread, so — following the existing `clear_requested_` pattern at `Particules.cpp:110` — menu/load/reset only set an atomic flag, and `process()` applies the scale at the next block boundary. This also handles re-pushing scale state after `processor_.Init()` (constructor and `onSampleRateChange` placement-new the Impl, losing quantizer state).

- [ ] **Step 1: Write the failing table tests**

Create `tests/particules/test_particules_scales.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../src/particules/particules_scales.h"

int main() {
    using namespace particules;

    // Every scale mode: table exists, ratios ascending, 1/1-relative
    // (all > 1.0), terminated by the 2.0 octave period.
    for (int mode = kPitchChromatic; mode <= kPitchMinorPentatonic; ++mode) {
        uint32_t count = 0;
        const int* semis = ScaleSemitones(mode, &count);
        assert(semis != nullptr);
        assert(count >= 5 && count <= kMaxScaleNotes);

        double ratios[kMaxScaleNotes];
        BuildScaleRatios(semis, count, ratios);

        double prev = 1.0;
        for (uint32_t i = 0; i < count; ++i) {
            assert(ratios[i] > prev);
            prev = ratios[i];
        }
        assert(std::fabs(ratios[count - 1] - 2.0) < 1e-12);
    }

    // Chromatic has all 12 degrees; pentatonics have 5; diatonics 7.
    uint32_t count = 0;
    ScaleSemitones(kPitchChromatic, &count);        assert(count == 12);
    ScaleSemitones(kPitchMajor, &count);            assert(count == 7);
    ScaleSemitones(kPitchMinor, &count);            assert(count == 7);
    ScaleSemitones(kPitchMajorPentatonic, &count);  assert(count == 5);
    ScaleSemitones(kPitchMinorPentatonic, &count);  assert(count == 5);

    // Legacy pitch-lock modes have no table.
    for (int mode : {kPitchOff, kPitchOctaves, kPitchOctavesFifths}) {
        count = 99;
        assert(ScaleSemitones(mode, &count) == nullptr);
        assert(count == 0);
    }

    std::printf("test_particules_scales: all assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Run the g++ lane to verify it fails**

Run: `tests/run.sh`
Expected: FAILS building `test_particules_scales.cpp` — header not found.

- [ ] **Step 3: Implement the scale tables**

Create `src/particules/particules_scales.h`:

```cpp
#pragma once

#include <cmath>
#include <cstdint>

// Tables for the unified "Lock pitch" selector. Modes 0-2 are the legacy
// engine-side pitch_lock modes (beads::QuantizePitchLock); modes 3-7 load
// the engine's PitchQuantizer with a 12-TET ratio table instead. The two
// mechanisms are mutually exclusive by construction (the engine applies the
// scale quantizer before pitch_lock, so setting both would double-quantize).
namespace particules {

enum PitchScaleMode {
    kPitchOff = 0,
    kPitchOctaves = 1,
    kPitchOctavesFifths = 2,
    kPitchChromatic = 3,
    kPitchMajor = 4,
    kPitchMinor = 5,
    kPitchMajorPentatonic = 6,
    kPitchMinorPentatonic = 7,
    kPitchScaleModeCount = 8,
};

constexpr uint32_t kMaxScaleNotes = 12;

// Semitone degrees above the root, excluding the implicit 1/1 (degree 0)
// and ending with the period (12 = octave) — the exact shape
// PitchQuantizer::loadRatios expects after conversion to ratios.
// Returns nullptr (count 0) for the legacy non-scale modes.
inline const int* ScaleSemitones(int mode, uint32_t* count) {
    static const int kChromatic[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    static const int kMajor[]     = {2, 4, 5, 7, 9, 11, 12};
    static const int kMinor[]     = {2, 3, 5, 7, 8, 10, 12};
    static const int kMajorPent[] = {2, 4, 7, 9, 12};
    static const int kMinorPent[] = {3, 5, 7, 10, 12};
    switch (mode) {
        case kPitchChromatic:       *count = 12; return kChromatic;
        case kPitchMajor:           *count = 7;  return kMajor;
        case kPitchMinor:           *count = 7;  return kMinor;
        case kPitchMajorPentatonic: *count = 5;  return kMajorPent;
        case kPitchMinorPentatonic: *count = 5;  return kMinorPent;
        default:                    *count = 0;  return nullptr;
    }
}

// Fill `out` (capacity >= count) with 2^(semitone/12) ratios.
inline void BuildScaleRatios(const int* semitones, uint32_t count, double* out) {
    for (uint32_t i = 0; i < count; ++i)
        out[i] = std::exp2(static_cast<double>(semitones[i]) / 12.0);
}

} // namespace particules
```

- [ ] **Step 4: Run the g++ lane**

Run: `tests/run.sh`
Expected: `test_particules_scales: all assertions passed`, exit 0.

- [ ] **Step 5: Wire the module state**

All edits in `src/particules/Particules.cpp`.

**5a.** Add the include after `#include "particules_gain_display.h"`:

```cpp
#include "particules_scales.h"
```

**5b.** Replace the member (line 100):

```cpp
	int  pitch_lock_ = 0;  // 0=off, 1=octaves, 2=octaves+5ths
```

with:

```cpp
	// Unified pitch selector: 0-2 = legacy pitch_lock modes (engine
	// QuantizePitchLock), 3-7 = scale modes (engine PitchQuantizer).
	int pitch_scale_ = 0;
	int pitch_root_ = 0;   // 0-11 semitones above C; scale modes only
	// Menu/load/reset mutate scale state on the UI thread; the audio thread
	// applies it at the next block boundary (same pattern as clear_requested_).
	std::atomic<bool> scale_dirty_{false};
```

**5c.** In the constructor, after `processor_.Init(dsp_memory_, req.total_bytes, sampleRate);` (line 185), add:

```cpp
		scale_dirty_.store(true, std::memory_order_release);
```

**5d.** In `onSampleRateChange`, inside the `if (dsp_memory_)` block after `processor_.Init(...)` (line 219), add:

```cpp
			// Init placement-news the Impl — quantizer state is gone.
			scale_dirty_.store(true, std::memory_order_release);
```

**5e.** In `onReset`, replace `pitch_lock_          = 0;` with:

```cpp
		pitch_scale_         = 0;
		pitch_root_          = 0;
		scale_dirty_.store(true, std::memory_order_release);
```

**5f.** In `dataToJson`, replace the `pitchLock` line with:

```cpp
	json_object_set_new(root, "pitchScale",      json_integer(pitch_scale_));
	json_object_set_new(root, "pitchRoot",       json_integer(pitch_root_));
```

**5g.** In `dataFromJson`, replace the `pitchLock` block with:

```cpp
	if ((j = json_object_get(root, "pitchScale")))
		pitch_scale_ = clamp((int)json_integer_value(j), 0, 7);
	else if ((j = json_object_get(root, "pitchLock")))   // pre-scale patches
		pitch_scale_ = clamp((int)json_integer_value(j), 0, 2);
	if ((j = json_object_get(root, "pitchRoot")))
		pitch_root_ = clamp((int)json_integer_value(j), 0, 11);
	scale_dirty_.store(true, std::memory_order_release);
```

**5h.** Add the apply helper as a method after `ResetControlConditioners()` (line 248):

```cpp
	// Audio-thread only: (re)push the scale selection into the engine.
	void ApplyScaleToProcessor() {
		uint32_t count = 0;
		const int* semis = particules::ScaleSemitones(pitch_scale_, &count);
		if (semis) {
			double ratios[particules::kMaxScaleNotes];
			particules::BuildScaleRatios(semis, count, ratios);
			processor_.LoadScale(ratios, count);
			processor_.SetScaleRoot(60 + pitch_root_);
		} else {
			processor_.ClearScale();
		}
	}
```

**5i.** In `updateSlowParams`, replace `params_.pitch_lock = pitch_lock_;` (line 321) with:

```cpp
		// Scale modes use the engine quantizer; pitch_lock must stay 0 there
		// or the engine would quantize twice (scale first, then pitch_lock).
		params_.pitch_lock =
			(pitch_scale_ <= particules::kPitchOctavesFifths) ? pitch_scale_ : 0;
```

**5j.** In `process()`, extend the block-boundary section (after the `clear_requested_` check at lines 399–400):

```cpp
			if (scale_dirty_.exchange(false, std::memory_order_acquire))
				ApplyScaleToProcessor();
```

- [ ] **Step 6: Replace the menu**

In `ParticulesWidget::appendContextMenu`, replace the Pitch Lock section (lines 565–570):

```cpp
		// --- Pitch Lock ---
		menu->addChild(createIndexSubmenuItem("Lock pitch",
			{"Off", "Octaves", "Octaves + 5ths", "Chromatic", "Major", "Minor",
			 "Major pentatonic", "Minor pentatonic"},
			[=]() { return module->pitch_scale_; },
			[=](int val) {
				module->pitch_scale_ = val;
				module->scale_dirty_.store(true, std::memory_order_release);
			}
		));

		// --- Scale root (scale modes only) ---
		{
			auto* rootItem = createIndexSubmenuItem("Root",
				{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
				[=]() { return module->pitch_root_; },
				[=](int val) {
					module->pitch_root_ = val;
					module->scale_dirty_.store(true, std::memory_order_release);
				}
			);
			// Disabled state is computed at menu-open; picking a scale and
			// then Root needs a menu re-open on VCV. Acceptable.
			rootItem->disabled = module->pitch_scale_ < particules::kPitchChromatic;
			menu->addChild(rootItem);
		}
```

- [ ] **Step 7: Compile check and full lanes**

Run: `cd vcv && make -j8` — Expected: clean build.
Run: `tests/run.sh` and `tests/beads/run.sh` — Expected: all pass (engine quantization itself is already covered by `tests/beads/test_pitch_quantizer.cpp` and `test_pitch_lock.cpp`).

- [ ] **Step 8: Commit**

```bash
git add src/particules/particules_scales.h tests/particules/test_particules_scales.cpp src/particules/Particules.cpp
git commit -m "feat: scale-aware pitch lock with root selection"
```

---

### Task 5: Q9 — Dry signal follows input gain (menu option, default ON)

**Files:**
- Modify: `src/vendor/beads_dsp/include/beads/parameters.h`
- Modify: `src/vendor/beads_dsp/src/beads_processor.cpp` (ProcessBlock, lines 175–182)
- Modify: `src/particules/Particules.cpp` (member, JSON, updateSlowParams, menu)
- Test: `tests/beads/test_processor.cpp`

**Interfaces:**
- Consumes: `AutoGain::Process` (dry now optionally passes through it, including its `SoftLimit` — exactly linear below |0.8|, so small-signal tests can assert exact gain).
- Produces: `bool dry_post_gain` on `beads::BeadsParameters` (default `true`); module member `bool dry_post_gain_ = true`; JSON key `dryPostGain`; menu bool "Dry signal follows input gain".

**Why:** The dry tap currently happens before auto-gain, so with up to +32 dB on the wet side, mid-knob DRY/WET mixes are grossly level-mismatched. Decision 2026-07-10: post-gain dry is the new default; the menu option restores exact-bypass dry. Existing saved patches have no key and load with the new default — accepted deliberately.

- [ ] **Step 1: Write the failing tests**

Append to `tests/beads/test_processor.cpp` (it already defines `TestProcessor` and `kBlockSize`):

```cpp
// Local sine fill with controllable amplitude: Q9 tests need the post-gain
// signal to stay inside SoftLimit's exactly-linear region (|x| <= 0.8).
static void make_scaled_sine_block(StereoFrame* buf, size_t n, int block_index,
                                   float amplitude) {
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(block_index * n + i);
        float v = amplitude * std::sin(2.0f * 3.14159265f * 440.0f * t / 48000.0f);
        buf[i] = {v, v};
    }
}

TEST_CASE("BeadsProcessor: dry tap follows input gain when dry_post_gain is set", "[processor][drytap]") {
    TestProcessor tp;
    BeadsParameters params;
    params.dry_wet = 0.0f;            // Full dry
    params.reverb = 0.0f;
    params.auto_gain = false;
    params.manual_gain_db = 12.0f;    // 10^(12/20) = 3.9811x
    params.dry_post_gain = true;      // The new default, set explicitly
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);

    // Settle manual-gain smoothing.
    for (int b = 0; b < 50; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // 0.05 * 3.9811 = 0.199 peak — inside SoftLimit's linear region, so the
    // dry output is exactly the gained input (crossfade dry gain is 1 at
    // dry_wet = 0).
    for (int b = 50; b < 55; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            REQUIRE(output[i].l == Approx(input[i].l * 3.9811f).margin(0.002f));
            REQUIRE(output[i].r == Approx(input[i].r * 3.9811f).margin(0.002f));
        }
    }
}

TEST_CASE("BeadsProcessor: dry_post_gain=false keeps the pre-gain bypass dry", "[processor][drytap]") {
    TestProcessor tp;
    BeadsParameters params;
    params.dry_wet = 0.0f;
    params.reverb = 0.0f;
    params.auto_gain = false;
    params.manual_gain_db = 12.0f;    // gain applies to wet path only
    params.dry_post_gain = false;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);

    for (int b = 0; b < 50; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // Dry equals the RAW input despite the +12 dB input gain.
    for (int b = 50; b < 55; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            REQUIRE(output[i].l == Approx(input[i].l).margin(0.002f));
            REQUIRE(output[i].r == Approx(input[i].r).margin(0.002f));
        }
    }
}
```

- [ ] **Step 2: Run the Catch2 lane to verify they fail**

Run: `tests/beads/run.sh`
Expected: FAILS — `dry_post_gain` is not a member of `BeadsParameters` (compile error is the failing state).

- [ ] **Step 3: Implement the flag**

**3a.** In `src/vendor/beads_dsp/include/beads/parameters.h`, add after the `pitch_lock` block (line 55):

```cpp
    // Dry-path tap point for the DRY/WET crossfade. true (default): dry is
    // tapped after auto/manual input gain (and its soft limit), so mid-knob
    // mixes stay level-matched against the gained wet path. false: dry is
    // the raw input, so DRY/WET=0 is a bit-exact bypass.
    bool dry_post_gain = true;
```

**3b.** In `src/vendor/beads_dsp/src/beads_processor.cpp`, replace (lines 178–182):

```cpp
        // Save pre-processing frame for dry output path so DRY/WET=0 matches bypass.
        s.dry_input_buf[i] = in;

        // 1. Auto-gain
        in = s.auto_gain.Process(in, s.params.manual_gain_db, s.params.auto_gain);
```

with:

```cpp
        // Dry tap for the DRY/WET crossfade: pre-gain keeps DRY/WET=0 a
        // bit-exact bypass; post-gain (default) keeps mid-knob mixes
        // level-matched against the auto-gained wet path. Note the post-gain
        // dry also passes AutoGain's SoftLimit, so it is not bit-clean at
        // hot inputs — documented in the manual.
        if (!s.params.dry_post_gain) s.dry_input_buf[i] = in;

        // 1. Auto-gain
        in = s.auto_gain.Process(in, s.params.manual_gain_db, s.params.auto_gain);
        if (s.params.dry_post_gain) s.dry_input_buf[i] = in;
```

- [ ] **Step 4: Run the Catch2 lane**

Run: `tests/beads/run.sh`
Expected: `100% tests passed` — including the pre-existing "Dry pass-through" SECTION in `test_processor.cpp`, which asserts peak 0.9–1.1 at 0 dB manual gain and still passes with the new default (SoftLimit(1.0) ≈ 0.93).

- [ ] **Step 5: Wire the wrapper**

In `src/particules/Particules.cpp`:

**5a.** Add member after `bool grain_trigger_out_ = false;` (line 101):

```cpp
	bool dry_post_gain_ = true;
```

**5b.** In `onReset`, after `grain_trigger_out_   = false;`:

```cpp
		dry_post_gain_       = true;
```

**5c.** In `dataToJson`, after the `grainTriggerOut` line:

```cpp
	json_object_set_new(root, "dryPostGain", json_boolean(dry_post_gain_));
```

**5d.** In `dataFromJson`, after the `grainTriggerOut` block:

```cpp
	if ((j = json_object_get(root, "dryPostGain")))
		dry_post_gain_ = json_boolean_value(j);
```

**5e.** In `updateSlowParams`, after `params_.manual_gain_db = ...` (line 340):

```cpp
		params_.dry_post_gain = dry_post_gain_;
```

**5f.** In `appendContextMenu`, after the Input level readout block (Task 3) and before SEED CV mode:

```cpp
		// --- Dry tap point ---
		menu->addChild(createBoolMenuItem("Dry signal follows input gain", "",
			[=]() { return module->dry_post_gain_; },
			[=](bool val) { module->dry_post_gain_ = val; }
		));
```

- [ ] **Step 6: Compile check**

Run: `cd vcv && make -j8` — Expected: clean build.

- [ ] **Step 7: Commit**

```bash
git add src/vendor/beads_dsp/include/beads/parameters.h src/vendor/beads_dsp/src/beads_processor.cpp src/particules/Particules.cpp tests/beads/test_processor.cpp
git commit -m "feat: dry tap follows input gain by default, menu option restores bypass"
```

---

### Task 6: P1 — Reverb idle sleep

**Files:**
- Modify: `src/vendor/beads_dsp/src/fx/fx_engine.h` (DelayLine)
- Modify: `src/vendor/beads_dsp/src/fx/reverb.h`, `src/vendor/beads_dsp/src/fx/reverb.cpp`
- Test: `tests/beads/test_reverb.cpp`

**Interfaces:**
- Consumes: existing `DelayLine` (gets a new `ClearData()`), `Clamp`, `OnePole`.
- Produces: `bool Reverb::IsAsleep() const` (test/debug observability). `Process()` semantics: while asleep, exact dry passthrough (`*out = in`); `SetAmount(> 0)` wakes within one call.

**Why:** The full Dattorro tank (12 delay lines, 8 allpasses, per-sample fb recompute) runs unconditionally even though `params.reverb` defaults to 0 — most patches pay the full tank for silence. Biggest single MetaModule CPU item left. Sleep only when `amount_ == 0` **and** the tank's wet output has stayed below −80 dBFS for 250 ms; flush state at sleep entry so wake is clean. The 250 ms hold preserves the ring-out-across-brief-zero behavior documented at `reverb.cpp:136-142` for any audible tail.

- [ ] **Step 1: Write the failing tests**

Append to `tests/beads/test_reverb.cpp` (it already includes `fx/reverb.h` and has buffer-setup patterns to copy from the "Init and process without crash" case):

```cpp
TEST_CASE("Reverb: sleeps after amount sits at 0 and the tail decays", "[reverb][sleep]") {
    std::vector<float> buffer(beads::Reverb::kMinBufferSize + 1000, 0.0f);
    beads::Reverb rv;
    rv.Init(buffer.data(), buffer.size(), 48000.0f);
    rv.SetDecay(0.5f);
    rv.SetDiffusion(0.7f);
    rv.SetMakeupGain(1.5849f);

    // Build a tail.
    rv.SetAmount(0.5f);
    float l, r;
    rv.Process(1.0f, 1.0f, &l, &r);
    for (int i = 0; i < 4800; ++i) rv.Process(0.0f, 0.0f, &l, &r);

    // Turn amount to 0: not asleep yet (hold period).
    rv.SetAmount(0.0f);
    REQUIRE(!rv.IsAsleep());

    // A second of silence at amount 0: tail decays below -80 dBFS and the
    // 250 ms hold elapses.
    for (int i = 0; i < 96000; ++i) rv.Process(0.0f, 0.0f, &l, &r);
    REQUIRE(rv.IsAsleep());

    // Asleep passthrough is exact.
    rv.Process(0.3f, -0.2f, &l, &r);
    REQUIRE(l == 0.3f);
    REQUIRE(r == -0.2f);
    REQUIRE(rv.IsAsleep());   // input while asleep does not wake it
}

TEST_CASE("Reverb: brief amount=0 dip keeps the tail ringing", "[reverb][sleep]") {
    std::vector<float> buffer(beads::Reverb::kMinBufferSize + 1000, 0.0f);
    beads::Reverb rv;
    rv.Init(buffer.data(), buffer.size(), 48000.0f);
    rv.SetDecay(0.9f);
    rv.SetDiffusion(0.7f);
    rv.SetMakeupGain(1.5849f);

    rv.SetAmount(0.8f);
    float l, r;
    rv.Process(1.0f, 1.0f, &l, &r);
    for (int i = 0; i < 2400; ++i) rv.Process(0.0f, 0.0f, &l, &r);

    // 50 ms at amount 0 — well inside the 250 ms hold.
    rv.SetAmount(0.0f);
    for (int i = 0; i < 2400; ++i) rv.Process(0.0f, 0.0f, &l, &r);
    REQUIRE(!rv.IsAsleep());

    // Back up: the tail is still in the tank.
    rv.SetAmount(0.8f);
    float energy = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        rv.Process(0.0f, 0.0f, &l, &r);
        energy += l * l + r * r;
    }
    REQUIRE(energy > 1e-6f);
}

TEST_CASE("Reverb: wake from sleep is clean and functional", "[reverb][sleep]") {
    std::vector<float> buffer(beads::Reverb::kMinBufferSize + 1000, 0.0f);
    beads::Reverb rv;
    rv.Init(buffer.data(), buffer.size(), 48000.0f);
    rv.SetDecay(0.5f);
    rv.SetDiffusion(0.7f);
    rv.SetMakeupGain(1.5849f);

    // Sleep it.
    rv.SetAmount(0.5f);
    float l, r;
    rv.Process(1.0f, 1.0f, &l, &r);
    rv.SetAmount(0.0f);
    for (int i = 0; i < 96000; ++i) rv.Process(0.0f, 0.0f, &l, &r);
    REQUIRE(rv.IsAsleep());

    // Wake: no stale tail (state was flushed), no NaN.
    rv.SetAmount(0.7f);
    REQUIRE(!rv.IsAsleep());
    for (int i = 0; i < 4800; ++i) {
        rv.Process(0.0f, 0.0f, &l, &r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        REQUIRE(std::fabs(l) < 1e-5f);
        REQUIRE(std::fabs(r) < 1e-5f);
    }

    // And it still reverberates.
    rv.Process(1.0f, 1.0f, &l, &r);
    float energy = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        rv.Process(0.0f, 0.0f, &l, &r);
        energy += l * l + r * r;
    }
    REQUIRE(energy > 1e-6f);
}
```

- [ ] **Step 2: Run the Catch2 lane to verify they fail**

Run: `tests/beads/run.sh`
Expected: FAILS — no member `IsAsleep`.

- [ ] **Step 3: Add `DelayLine::ClearData()`**

In `src/vendor/beads_dsp/src/fx/fx_engine.h`, add to `DelayLine`'s public section (after `Advance()`, before `size()`):

```cpp
    // Zero the stored samples (keeps the buffer pointer and write position).
    // Used by the reverb's idle-sleep flush.
    void ClearData() {
        if (buffer_) {
            for (size_t i = 0; i < size_; ++i) buffer_[i] = 0.0f;
        }
    }
```

- [ ] **Step 4: Add sleep state to `Reverb`**

**4a.** In `src/vendor/beads_dsp/src/fx/reverb.h`, add to the public section after `Process(...)`:

```cpp
    // True when the tank is asleep (amount held at 0 and the tail decayed):
    // Process() is an exact dry passthrough. Exposed for tests/diagnostics.
    bool IsAsleep() const { return asleep_; }
```

**4b.** In the private section, after the `wet_xfade_` member:

```cpp
    // Idle sleep: when amount_ == 0 and the wet output has stayed below
    // kSleepEnvThreshold for kSleepHoldSeconds, flush all tank state and
    // short-circuit Process() to the dry path. SetAmount(> 0) wakes it;
    // the flush at sleep entry means wake starts from a clean tank.
    // The hold keeps brief CV zero-crossings from flushing an audible tail.
    bool asleep_ = false;
    float wet_env_ = 0.0f;              // abs-peak envelope of wet output
    float wet_env_decay_ = 0.99896f;    // ~20 ms; recomputed in Init()
    int quiet_samples_ = 0;
    int sleep_hold_samples_ = 12000;    // 250 ms @ 48k; recomputed in Init()
    static constexpr float kSleepEnvThreshold = 1e-4f;   // -80 dBFS
    static constexpr float kSleepHoldSeconds = 0.25f;

    void FlushTank();
```

- [ ] **Step 5: Implement sleep in `reverb.cpp`**

**5a.** In `Reverb::Init`, after the `feedback_l_/feedback_r_` resets (line 37):

```cpp
    asleep_ = false;
    wet_env_ = 0.0f;
    quiet_samples_ = 0;
    sleep_hold_samples_ = (sample_rate > 0.0f)
        ? static_cast<int>(sample_rate * kSleepHoldSeconds)
        : 12000;
    wet_env_decay_ = (sample_rate > 0.0f)
        ? std::exp(-1.0f / (0.020f * sample_rate))
        : 0.99896f;
```

**5b.** In `Reverb::SetAmount`, after the `amount_ = Clamp(...)` line:

```cpp
    if (amount_ > 0.0f) {
        // Wake. State was flushed at sleep entry, so the tank starts clean.
        asleep_ = false;
        quiet_samples_ = 0;
    }
```

**5c.** In `Reverb::Process`, after the `enabled_` early-out:

```cpp
    if (asleep_) {
        // amount_ == 0 here, so dry_xfade_ == 1 and wet_xfade_ == 0: the
        // full path would return the dry input anyway. Skip the whole tank.
        *left_out = left_in;
        *right_out = right_in;
        return;
    }
```

**5d.** At the end of `Reverb::Process`, after the `wet_l`/`wet_r` computation and before the final crossfade lines, add:

```cpp
    // Idle-sleep bookkeeping. The explicit greater-than comparison (not
    // std::max) means a NaN wet frame decays the envelope instead of
    // poisoning it, so a NaN'd tank still reaches sleep and gets flushed.
    float wet_abs_l = std::fabs(wet_l);
    float wet_abs_r = std::fabs(wet_r);
    float wet_abs = wet_abs_l > wet_abs_r ? wet_abs_l : wet_abs_r;
    wet_env_ *= wet_env_decay_;
    if (wet_abs > wet_env_) wet_env_ = wet_abs;
    if (amount_ == 0.0f && wet_env_ < kSleepEnvThreshold) {
        if (++quiet_samples_ >= sleep_hold_samples_) {
            FlushTank();
            asleep_ = true;
        }
    } else {
        quiet_samples_ = 0;
    }
```

**5e.** Add `FlushTank` at the end of the file (before the closing namespace brace):

```cpp
void Reverb::FlushTank() {
    // One-time cost at sleep entry (~11k floats zeroed) — never on the
    // steady-state path.
    ap_in_1_.ClearData();
    ap_in_2_.ClearData();
    ap_in_3_.ClearData();
    ap_in_4_.ClearData();
    delay_l1_.ClearData();
    ap_l1_.ClearData();
    ap_l2_.ClearData();
    delay_l2_.ClearData();
    delay_r1_.ClearData();
    ap_r1_.ClearData();
    ap_r2_.ClearData();
    delay_r2_.ClearData();
    feedback_l_ = 0.0f;
    feedback_r_ = 0.0f;
    lp_state_l_ = 0.0f;
    lp_state_r_ = 0.0f;
    dc_estimate_l_ = 0.0f;
    dc_estimate_r_ = 0.0f;
    wet_env_ = 0.0f;
    quiet_samples_ = 0;
}
```

- [ ] **Step 6: Run the Catch2 lane**

Run: `tests/beads/run.sh`
Expected: `100% tests passed` — including the pre-existing "Dry signal passes when amount is 0" and tail/NaN reverb cases.

- [ ] **Step 7: Compile check both hosts' shared source**

Run: `cd vcv && make -j8` — Expected: clean build.

- [ ] **Step 8: Commit**

```bash
git add src/vendor/beads_dsp/src/fx/fx_engine.h src/vendor/beads_dsp/src/fx/reverb.h src/vendor/beads_dsp/src/fx/reverb.cpp tests/beads/test_reverb.cpp
git commit -m "perf: reverb sleeps after amount sits at 0 and tail decays"
```

---

### Task 7: F8 — Menu undo (VCV-only)

**Files:**
- Modify: `src/particules/Particules.cpp` only.

**Interfaces:**
- Consumes: Rack's `history::ModuleChange` and `APP->engine->moduleToJson()` (available via `plugin.hpp`/`rack.hpp`); the menu lambdas from Tasks 4 and 5.
- Produces: `withMenuUndo(Particules*, const char*, F&&)` — free function template; whole-module JSON snapshot before/after the mutation, pushed to the undo stack. No-op passthrough on MetaModule.

**Scope (decided 2026-07-10):** wrap SEED CV mode, Lock pitch + Root, Auto gain enable/disable, Grain trigger on R, Dry signal follows input gain. **Not** the manual-gain slider (drag coalescing is poor) and **not** Clear buffer (mutates engine audio state that isn't in JSON).

- [ ] **Step 1: Add the helper**

In `src/particules/Particules.cpp`, after the `QualityParamQuantity::getDisplayValueString()` definition (line 439) and before `ManualGainQuantity`:

```cpp
// Wrap a context-menu mutation in a whole-module undo snapshot. Because the
// snapshot is the module's full JSON (params + data), one helper covers
// every menu field with no per-item code. VCV-only: MetaModule has no undo
// stack, so there it just runs the mutation.
#ifndef METAMODULE
template <typename F>
static void withMenuUndo(Particules* module, const char* label, F&& mutate) {
	json_t* oldJ = APP->engine->moduleToJson(module);
	mutate();
	history::ModuleChange* h = new history::ModuleChange;
	h->name = label;
	h->moduleId = module->id;
	h->oldModuleJ = oldJ;
	h->newModuleJ = APP->engine->moduleToJson(module);
	APP->history->push(h);
}
#else
template <typename F>
static void withMenuUndo(Particules*, const char*, F&& mutate) {
	mutate();
}
#endif
```

- [ ] **Step 2: Wrap the menu mutations**

All in `appendContextMenu`:

**2a.** `AutoGainItem::onAction` becomes:

```cpp
			void onAction(const event::Action& e) override {
				if (!module->auto_gain_) {
					withMenuUndo(module, "enable auto gain",
						[this]() { module->auto_gain_ = true; });
				}
				module->processor_.TriggerAutoGainCalibration();
			}
```

**2b.** `ManualGainItem::onAction` becomes:

```cpp
			void onAction(const event::Action& e) override {
				if (module->auto_gain_) {
					withMenuUndo(module, "disable auto gain",
						[this]() { module->auto_gain_ = false; });
				}
			}
```

(The manual-gain slider and the MetaModule dB list are deliberately not wrapped.)

**2c.** SEED CV mode setter becomes:

```cpp
			[=](int val) {
				withMenuUndo(module, "change SEED CV mode",
					[=]() { module->seed_state_ = val; });
			}
```

**2d.** Lock pitch setter (Task 4) becomes:

```cpp
			[=](int val) {
				withMenuUndo(module, "change pitch lock", [=]() {
					module->pitch_scale_ = val;
					module->scale_dirty_.store(true, std::memory_order_release);
				});
			}
```

**2e.** Root setter (Task 4) becomes:

```cpp
				[=](int val) {
					withMenuUndo(module, "change scale root", [=]() {
						module->pitch_root_ = val;
						module->scale_dirty_.store(true, std::memory_order_release);
					});
				}
```

**2f.** Grain trigger bool setter becomes:

```cpp
			[=](bool val) {
				withMenuUndo(module, "toggle grain trigger output",
					[=]() { module->grain_trigger_out_ = val; });
			}
```

**2g.** Dry-post-gain bool setter (Task 5) becomes:

```cpp
			[=](bool val) {
				withMenuUndo(module, "toggle dry gain follow",
					[=]() { module->dry_post_gain_ = val; });
			}
```

(Clear buffer stays unwrapped.)

**Undo restores scale state correctly because** `dataFromJson` (which undo replays) sets `scale_dirty_`, so the engine gets the restored scale at the next block boundary.

- [ ] **Step 3: Compile check**

Run: `cd vcv && make -j8` — Expected: clean build. If `APP->engine->moduleToJson` is not the available API in this Rack SDK version, check `$HOME/Dev/Rack-SDK/include/engine/Engine.hpp` for the exact signature (`json_t* moduleToJson(Module*)`) before substituting anything.

- [ ] **Step 4: Run both test lanes (regression)**

Run: `tests/run.sh` and `tests/beads/run.sh` — Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/particules/Particules.cpp
git commit -m "feat: context-menu changes push undo snapshots on VCV"
```

---

### Task 8 (OPTIONAL — pending user decision): Integrate beads_dsp as first-party code

> **Do not execute until the user has approved.** Evaluation and recommendation are in the accompanying analysis; short version: the code is a hard fork (every shared file differs from upstream, ~1,650 diff lines, whole subsystems added/removed, no submodule, no push-back or pull-forward planned), this very track edits "vendor" files in three tasks, and the `vendor/` label misleadingly signals don't-touch. Recommendation: move `src/vendor/beads_dsp/` → `src/particules/dsp/` (keeping its internal `include/beads` + `src` layout and the `beads::` namespace), and add a provenance NOTICE with the upstream MIT copyright (Neal Sanche), which is currently missing from the tree — an MIT-compliance gap regardless of this decision.

**Files:**
- Move: `src/vendor/beads_dsp/` → `src/particules/dsp/` (git mv; `src/vendor/` becomes empty and disappears)
- Create: `src/particules/dsp/NOTICE.md`
- Modify: `vcv/Makefile` (lines 13, 21–22), `metamodule/CMakeLists.txt` (lines 27–38, 45), `tests/run.sh` (line 27 include flag), `tests/beads/CMakeLists.txt` (line 9 `BEADS` var, stale comment at line 19), `src/particules/Particules.cpp` (line 3 relative include)

- [ ] **Step 1: Move the tree**

```bash
git mv src/vendor/beads_dsp src/particules/dsp
```

- [ ] **Step 2: Repoint every reference**

```bash
grep -rn "vendor/beads_dsp\|vendor" --include="*.cpp" --include="*.h" --include="*.hpp" --include="Makefile" --include="*.txt" --include="*.sh" src vcv metamodule tests | grep -v Binary
```

Fix each hit:
- `vcv/Makefile:13`: `-I../src/vendor/beads_dsp/include` → `-I../src/particules/dsp/include`
- `vcv/Makefile:21-22`: `../src/vendor/beads_dsp/src/*.cpp` globs → `../src/particules/dsp/src/*.cpp`
- `metamodule/CMakeLists.txt:27-38`: `${SRC}/src/vendor/beads_dsp/src/...` → `${SRC}/src/particules/dsp/src/...`
- `metamodule/CMakeLists.txt:45`: include dir likewise
- `tests/run.sh:27`: `-I../src/vendor/beads_dsp/include` → `-I../src/particules/dsp/include`
- `tests/beads/CMakeLists.txt:9`: `set(BEADS ${REPO_ROOT}/src/vendor/beads_dsp)` → `set(BEADS ${REPO_ROOT}/src/particules/dsp)`
- `tests/beads/CMakeLists.txt:19`: fix the stale "16 verbatim upstream test files" comment → "the beads engine test files"
- `src/particules/Particules.cpp:3`: `#include "../vendor/beads_dsp/src/util/control_conditioner.h"` → `#include "dsp/src/util/control_conditioner.h"`

Then rerun the grep and confirm zero remaining `vendor/beads_dsp` hits.

- [ ] **Step 3: Add the provenance notice**

Create `src/particules/dsp/NOTICE.md`:

```markdown
# Provenance

This directory is the granular-DSP engine behind Particules. It began as the
`beads_dsp/` library from Neal Sanche's
[nosuch_texture](https://github.com/thorinside/nosuch_texture) (a Disting NT
recreation of Mutable Instruments Beads, design by Émilie Gillet) and has
since been heavily modified for VCV Rack / MetaModule use in this repo; it is
maintained here as first-party code and no longer tracks upstream.

The original code is MIT-licensed:

MIT License — Copyright (c) 2026 Neal Sanche

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 4: Full verification**

Run: `tests/run.sh` — Expected: exit 0 (includes `test_robotboy_identity.py` guards).
Run: `tests/beads/run.sh` — Expected: `100% tests passed` (delete `tests/beads/build/` first so CMake re-globs the moved sources: `rm -rf tests/beads/build`).
Run: `cd vcv && make -j8` — Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: adopt beads_dsp as first-party code under src/particules/dsp"
```

---

### Task 9: Final verification and user-run checklist

- [ ] **Step 1: Run everything**

```bash
tests/run.sh && tests/beads/run.sh && (cd vcv && make -j8)
```
Expected: all green.

- [ ] **Step 2: Present the user-run checklist**

GUI/simulator/listening checks are user-run by policy. Present this list (do not run these):

1. **F5** — VCV + simulator: grain LED reads as density (dim single flashes at sparse density, steady glow when dense). Tune the 0.25 floor / full-at-10 constants by eye if needed.
2. **F6** — open the context menu with audio playing: "Input" row shows a live level on VCV, a static-but-plausible level on MetaModule; unplugged shows "silent".
3. **F3** — Lock pitch → Major, Root → D, sweep PITCH with a melodic source: grains land on D major; Off/Octaves modes behave as before. Re-check after changing sample rate (scale must survive engine re-init). Note: after switching to a scale mode, the Root submenu enables on the next menu open.
4. **F7** — enable "Grain trigger on R output", crank density, scope the R output: distinct pulses with visible gaps, no continuous high level; a downstream trigger module fires repeatedly.
5. **F8** — VCV only: change each menu option (SEED CV mode, Lock pitch, Root, auto gain toggle, grain trigger, dry-follows-gain) and Ctrl-Z each; slider drags are deliberately not undoable; Clear buffer deliberately not undoable.
6. **Q9** — with a quiet input and auto gain active, sweep DRY/WET: no level jump through the middle (default ON); toggle the option off and confirm DRY/WET=0 is bit-transparent again. Old patches load with the new default — expected.
7. **P1** — MetaModule: patch with reverb at 0 shows the CPU drop after ~a second; turning reverb up from 0 produces no burp/stale tail; a long tail rung out just before turning the knob to 0 and quickly back up is preserved.

- [ ] **Step 3: Record outcomes**

After the user reports results, record them in `docs/` following the existing pattern (`docs/superpowers/specs/...` decision notes / `030b4cf`-style check-record commits).

---

## Self-review notes

- **Spec coverage:** F3 → Task 4; F5 → Task 2; F6 → Task 3; F7 → Task 1; F8 → Task 7; Q9 → Task 5; P1 → Task 6. Rejected/deferred/skipped items (F4, Q4, Q5, Q7, Q8, Q10, P2) intentionally absent. Q10's follow-up (remove kMidi) is out of scope for this track.
- **Spec deviations, deliberate:** (1) F3 uses an atomic dirty-flag + audio-thread apply rather than calling `LoadScale` from menu callbacks — the spec's "re-push after Init" requirement plus the existing `clear_requested_` pattern make this the safe reading. (2) F3 keeps reading the legacy `pitchLock` JSON key so pre-existing patches keep their lock mode. (3) Q9's `dry_post_gain` defaults to `true` in `BeadsParameters` itself (spec's design paragraph said `false` but the 2026-07-10 decision header says default ON; the decision wins).
- **Type consistency check:** `NoteGrainActivity(int, bool)` (Tasks 2); `FormatInputLevelDb(float) -> std::string` (Task 3); `ScaleSemitones(int, uint32_t*) -> const int*`, `BuildScaleRatios(const int*, uint32_t, double*)` (Task 4); `dry_post_gain` / `dry_post_gain_` (Task 5); `IsAsleep()`, `ClearData()`, `FlushTank()` (Task 6); `withMenuUndo(Particules*, const char*, F&&)` (Task 7). Verified consistent across tasks.
