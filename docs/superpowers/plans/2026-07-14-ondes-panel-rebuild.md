# Ondes Panel Rebuild Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the Ondes panel with `vcv-panel-gen` at 8 HP, add a live cyan wavetable display under the title, and put a cyan plate behind the controls (not the Out jack).

**Architecture:** Regenerate `res/Ondes.svg` from a new declarative spec (`panel-specs/ondes.yaml`). Add a pure, Rack-free bilinear-sampling helper (`WavetableFrame.hpp`) shared by a new `WavetableDisplay` NanoVG widget in `Ondes.cpp`; the module publishes its last post-CV bank/wave for the widget to read. Update `Ondes.cpp` control positions to match the new SVG, then rebuild VCV and the MetaModule faceplate.

**Tech Stack:** C++17 (Rack SDK + MetaModule SDK), Python `vcv-panel-gen`, Catch2 (vendored), CMake/Make.

## Global Constraints

- Work in the `add-ondes` worktree (`/Users/gabrielroth/Dev/RobotBoy/.worktrees/add-ondes`); never touch the main worktree.
- Never hand-edit the generated SVG — every panel change goes through `panel-specs/ondes.yaml` and regeneration.
- Do not add/remove/rename any param, input, or output. Enum set is fixed: params `PITCH_PARAM POSITION_PARAM POSITION_AMT_PARAM BANK_PARAM BANK_AMT_PARAM`; inputs `VOCT_INPUT POSITION_INPUT BANK_INPUT`; output `OUT_OUTPUT`.
- Panel width is exactly **8 HP** (40.64 mm); height fixed 128.5 mm.
- House theme is inherited from `~/.config/vcv-panel-gen/theme.yaml` (dark `#3d3d3d`, Futura labels, Shuttleblock title, uppercase, dark screws) — do not restate it in the spec unless overriding.
- The display shows a **bare** cyan waveform trace (no filename/readout text).
- Commit messages: short (≤15 words, one sentence), no "Co-Authored-By"/AI attribution.
- GUI/visual behavior is verified by a user-run checklist, not automated tests (per `no-agent-gui-simulator-tests`).
- Build the VCV plugin with `make -C vcv`; there is no `build-install.sh` in this repo.

---

### Task 1: Generate the 8 HP panel SVG from a spec

**Files:**
- Create: `panel-specs/ondes.yaml`
- Regenerate (do not hand-edit): `res/Ondes.svg`

**Interfaces:**
- Produces: `res/Ondes.svg` with a components layer carrying exactly these ids — `PITCH_PARAM#RoundBlackKnob`, `VOCT_INPUT#PJ301MPort`, `BANK_PARAM#RoundBlackKnob`, `POSITION_PARAM#RoundBlackKnob`, `BANK_INPUT#PJ301MPort`, `POSITION_INPUT#PJ301MPort`, `BANK_AMT_PARAM#Trimpot`, `POSITION_AMT_PARAM#Trimpot`, `OUT_OUTPUT#PJ301MPort`, and one screen widget `SCREEN#Widget` (the default id for a label-less `screen` row). Later tasks read the component cx/cy (mm) from this SVG.

- [ ] **Step 1: Activate the vcv-panel-gen virtualenv**

Run:
```bash
source ~/Dev/python-scripts/.venv/bin/activate
python ~/Dev/vcv-panel-gen/panel_gen.py --version
```
Expected: prints a version string, exits 0. (If `fonttools`/`PyYAML` are missing, `pip install fonttools PyYAML` into that venv.)

- [ ] **Step 2: Write the initial spec (no cyan zone yet — geometry comes from the first pass)**

Create `panel-specs/ondes.yaml`:
```yaml
# Ondes — wavetable oscillator, 8 HP. Regenerated with vcv-panel-gen; house
# theme (dark panel, Futura labels, Shuttleblock title) inherited from
# ~/.config/vcv-panel-gen/theme.yaml. Layout: title, wavetable display
# (screen), Pitch knob + V/Oct beside it, then Bank/Position columns each as
# knob -> CV jack -> attenuverter, Out centered at the bottom. A single cyan
# plate (zones:) backs Pitch/Bank/Position but not the Out jack — its geometry
# is derived from the generated component positions (see plan Task 1).
slug: Ondes
name: ONDES
hp: 8
title_size: 7
side_margin: 3
title_valign: center
rows:
  - type: screen
    height: 20
    gap_above: 1.0
  - type: inputs           # Pitch knob + V/Oct jack, horizontal companion
    label_side: above
    gap_above: 3.0
    items:
      - {label: Pitch, name: PITCH_PARAM, type: knobs, cv: VOCT_INPUT}
  - type: knobs            # Bank / Position main knobs
    gap_above: 3.0
    items:
      - {label: Bank,     name: BANK_PARAM}
      - {label: Position, name: POSITION_PARAM}
  - type: inputs           # Bank / Position CV jacks (unlabeled)
    label_side: below
    gap_above: 1.5
    items:
      - {label: "{blank}", name: BANK_INPUT}
      - {label: "{blank}", name: POSITION_INPUT}
  - type: knobs            # Bank / Position attenuverters (unlabeled trimpots)
    gap_above: 1.5
    items:
      - {label: "{blank}", name: BANK_AMT_PARAM,     widget: Trimpot, small: true}
      - {label: "{blank}", name: POSITION_AMT_PARAM, widget: Trimpot, small: true}
  - type: outputs          # Out — outside the cyan plate
    label_side: below
    gap_above: 3.0
    items:
      - {label: Out, name: OUT_OUTPUT}
```

- [ ] **Step 3: Validate the spec builds (no file written)**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/add-ondes
python ~/Dev/vcv-panel-gen/panel_gen.py panel-specs/ondes.yaml --check
```
Expected: `OK: panel-specs/ondes.yaml builds — … (nothing written).`
If it raises a `LayoutError` (title behind screw, overflow, or a `{blank}` label rule), adjust `title_size`/`gap_above`/`side_margin` (e.g. drop `title_size` to 6, or add `nudges: {TITLE: [0, 1]}`) and re-run until it passes. Do not proceed until `--check` is clean.

- [ ] **Step 4: Generate the SVG**

Run:
```bash
python ~/Dev/vcv-panel-gen/panel_gen.py panel-specs/ondes.yaml --out res/Ondes.svg
```
Expected: `Wrote res/Ondes.svg: 5 params, 3 inputs, 1 outputs, 0 lights, 1 screens.`

- [ ] **Step 5: Read the generated component + screen geometry**

Run:
```bash
grep -oE 'id="[^"]*"|cx="[0-9.]*"|cy="[0-9.]*"' res/Ondes.svg | grep -A2 -E 'PARAM|INPUT|OUTPUT'
grep -oE '<rect[^>]*id="SCREEN#Widget"[^>]*>' res/Ondes.svg
```
Record each control's `cx`/`cy` (mm) and the screen rect's `x`/`y`/`width`/`height` (mm). These feed Task 3. The cyan plate should span from just below the screen rect (screen `y`+`height`+~1 mm) to just above the Out jack (`OUT_OUTPUT` cy − ~9 mm), across the usable width (x ≈ `side_margin`−0.5, w ≈ panel_w − 2·(`side_margin`−0.5)).

- [ ] **Step 6: Add the cyan plate `zones:` block and regenerate**

Append to `panel-specs/ondes.yaml` (fill in the numbers from Step 5; example values shown — replace with the real ones):
```yaml
zones:
  # Cyan plate behind Pitch/Bank/Position; stops above the Out jack.
  - {x: 2.5, y: 34.0, w: 35.64, h: 62.0, fill: "#00e5ff", opacity: 0.14, rx: 2}
```
Then regenerate:
```bash
python ~/Dev/vcv-panel-gen/panel_gen.py panel-specs/ondes.yaml --out res/Ondes.svg
```
Expected: same component summary as Step 4. (Tune the cyan hex/opacity with the `picking-panel-rect-colors` skill if the composited color looks off; adjust only the `zones:` values and regenerate.)

- [ ] **Step 7: Build the composite preview and show it for sign-off**

Run:
```bash
python ~/Dev/vcv-panel-gen/preview.py res/Ondes.svg --open
```
Confirm with the user: 8 HP width, display region under the title, Pitch+V/Oct together, Bank/Position columns with CV+attenuverter, Out at the bottom outside the cyan plate, and the cyan plate covering Pitch/Bank/Position. Fix the spec (never the SVG) and regenerate for any change.

- [ ] **Step 8: Commit**

```bash
git add panel-specs/ondes.yaml res/Ondes.svg
git commit -m "feat: regenerate Ondes panel at 8 HP with display and cyan plate"
```

---

### Task 2: Pure wavetable-frame sampling helper (TDD)

**Files:**
- Create: `src/particules/WavetableFrame.hpp`
- Create: `tests/particules_dsp/test_wavetable_frame.cpp`

**Interfaces:**
- Produces: `float robotboy::wavetableFrameSample(const particules_dsp::WavetableProvider& provider, float bank01, float wave01, int sampleIndex)` — bilinearly blends the four neighbouring stored waveforms at normalized `bank01`/`wave01` in `[0,1]` and returns the sample at `sampleIndex` in `[0, kWavetableSize)`. Mirrors `WavetableOscillator::Process`'s bank×wave crossfade (`src/particules/dsp/src/wavetable/wavetable_oscillator.cpp:39-77`), without phase interpolation. Consumed by Task 3's widget.

- [ ] **Step 1: Write the failing test**

Create `tests/particules_dsp/test_wavetable_frame.cpp`:
```cpp
#include <catch2/catch_amalgamated.hpp>
#include "WavetableFrame.hpp"
#include "particules_dsp/types.h"

namespace {
// 2 banks x 2 waves; each waveform is a distinct constant so blends are easy to
// reason about. Arrays are kWavetableSize long (contract), constant-valued.
struct FakeProvider : particules_dsp::WavetableProvider {
    float w[2][2][particules_dsp::kWavetableSize];
    FakeProvider() {
        for (int b = 0; b < 2; ++b)
            for (int v = 0; v < 2; ++v) {
                float val = float(b * 2 + v);  // b0v0=0, b0v1=1, b1v0=2, b1v1=3
                for (int i = 0; i < particules_dsp::kWavetableSize; ++i) w[b][v][i] = val;
            }
    }
    const float* GetWaveform(int bank, int index) const override { return w[bank][index]; }
    int NumBanksAvailable() const override { return 2; }
    int WaveformsPerBank() const override { return 2; }
};
}

TEST_CASE("wavetableFrameSample returns exact stored sample at integer corners") {
    FakeProvider p;
    REQUIRE(robotboy::wavetableFrameSample(p, 0.f, 0.f, 0) == Catch::Approx(0.f));
    REQUIRE(robotboy::wavetableFrameSample(p, 0.f, 1.f, 5) == Catch::Approx(1.f));
    REQUIRE(robotboy::wavetableFrameSample(p, 1.f, 0.f, 9) == Catch::Approx(2.f));
    REQUIRE(robotboy::wavetableFrameSample(p, 1.f, 1.f, 200) == Catch::Approx(3.f));
}

TEST_CASE("wavetableFrameSample bilinearly blends between corners") {
    FakeProvider p;
    // wave midpoint at bank 0: halfway between b0v0=0 and b0v1=1 -> 0.5
    REQUIRE(robotboy::wavetableFrameSample(p, 0.f, 0.5f, 0) == Catch::Approx(0.5f));
    // bank midpoint at wave 0: halfway between b0v0=0 and b1v0=2 -> 1.0
    REQUIRE(robotboy::wavetableFrameSample(p, 0.5f, 0.f, 0) == Catch::Approx(1.0f));
    // center: mean of {0,1,2,3} -> 1.5
    REQUIRE(robotboy::wavetableFrameSample(p, 0.5f, 0.5f, 0) == Catch::Approx(1.5f));
}

TEST_CASE("wavetableFrameSample clamps out-of-range normalized inputs") {
    FakeProvider p;
    REQUIRE(robotboy::wavetableFrameSample(p, -1.f, 2.f, 0) == Catch::Approx(1.f)); // -> b0v1=1
}
```

- [ ] **Step 2: Run the test to verify it fails to compile/link**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/add-ondes/tests/particules_dsp
cmake -B build -G "Unix Makefiles" >/dev/null && cmake --build build -j 2>&1 | tail -5
```
Expected: build fails — `WavetableFrame.hpp` not found / `wavetableFrameSample` undeclared.

- [ ] **Step 3: Write the minimal implementation**

Create `src/particules/WavetableFrame.hpp`:
```cpp
#pragma once

#include "particules_dsp/types.h"
#include <algorithm>

namespace robotboy {

// Bilinearly blend the four neighbouring stored waveforms at normalized
// bank01/wave01 in [0,1] and return the sample at sampleIndex. Mirrors the
// bank x wave crossfade in WavetableOscillator::Process (no phase interp).
inline float wavetableFrameSample(const particules_dsp::WavetableProvider& provider,
                                  float bank01, float wave01, int sampleIndex) {
    const int numBanks = provider.NumBanksAvailable();
    const int perBank  = provider.WaveformsPerBank();
    if (numBanks <= 0 || perBank <= 0) return 0.f;

    const float bankPos = std::clamp(bank01, 0.f, 1.f) * float(numBanks - 1);
    int bankLo = int(bankPos);
    bankLo = std::clamp(bankLo, 0, numBanks - 1);
    int bankHi = std::min(bankLo + 1, numBanks - 1);
    const float bankFrac = bankPos - float(bankLo);

    const float wavePos = std::clamp(wave01, 0.f, 1.f) * float(perBank - 1);
    int waveLo = int(wavePos);
    waveLo = std::clamp(waveLo, 0, perBank - 1);
    int waveHi = std::min(waveLo + 1, perBank - 1);
    const float waveFrac = wavePos - float(waveLo);

    const float* ll = provider.GetWaveform(bankLo, waveLo);
    const float* lh = provider.GetWaveform(bankLo, waveHi);
    const float* hl = provider.GetWaveform(bankHi, waveLo);
    const float* hh = provider.GetWaveform(bankHi, waveHi);
    if (!ll || !lh || !hl || !hh) return 0.f;

    const float sLo = ll[sampleIndex] + (lh[sampleIndex] - ll[sampleIndex]) * waveFrac;
    const float sHi = hl[sampleIndex] + (hh[sampleIndex] - hl[sampleIndex]) * waveFrac;
    return sLo + (sHi - sLo) * bankFrac;
}

}  // namespace robotboy
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/add-ondes/tests/particules_dsp
cmake --build build -j >/dev/null && ctest --test-dir build --output-on-failure 2>&1 | tail -8
```
Expected: all tests pass, including the three new `wavetableFrameSample` cases.

- [ ] **Step 5: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/add-ondes
git add src/particules/WavetableFrame.hpp tests/particules_dsp/test_wavetable_frame.cpp
git commit -m "feat: add pure wavetable-frame sampling helper with tests"
```

---

### Task 3: WavetableDisplay widget, module readout, and updated control positions

**Files:**
- Modify: `src/particules/Ondes.cpp`

**Interfaces:**
- Consumes: `robotboy::wavetableFrameSample(...)` (Task 2); the component cx/cy and screen rect geometry recorded in Task 1 Step 5.
- Produces: public `float Ondes::lastBank` and `float Ondes::lastWave` (post-CV, clamped `[0,1]`), updated each `process()`; a `WavetableDisplay` widget added to `OndesWidget`.

- [ ] **Step 1: Publish the last bank/wave on the module**

In `src/particules/Ondes.cpp`, add public members to `struct Ondes` (after the pitch-cache fields near line 24):
```cpp
    // Last post-CV bank/wave (0-1), read by the panel display. UI-thread read of
    // an audio-thread write is a benign race (display only), as in Fundamental.
    float lastBank = 0.f;
    float lastWave = 0.f;
```
In `process()`, immediately after `bank` is computed (after line 61), store both:
```cpp
        lastBank = bank;
        lastWave = position;
```

- [ ] **Step 2: Add the WavetableDisplay widget**

In `src/particules/Ondes.cpp`, add the include near the top (after the existing includes):
```cpp
#include "WavetableFrame.hpp"
```
Add this widget above `struct OndesWidget` (after the `Ondes` module struct):
```cpp
// Draws the current bilinearly-interpolated wavetable frame as a cyan trace
// that morphs as Bank/Position (and their CV) change. Bare trace, no text.
struct WavetableDisplay : LedDisplay {
    Ondes* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            nvgScissor(args.vg, RECT_ARGS(args.clipBox));

            float bank = module ? module->lastBank : 0.f;
            float wave = module ? module->lastWave : 0.f;
            RackWavetableProvider provider;

            Rect scope = Rect(Vec(0, 0), box.size).shrink(Vec(4, 5));
            const int n = particules_dsp::kWavetableSize;
            nvgBeginPath(args.vg);
            for (int i = 0; i <= n; ++i) {
                float s = robotboy::wavetableFrameSample(provider, bank, wave, i % n);
                Vec p;
                p.x = float(i) / n;
                p.y = 0.5f - 0.5f * s;
                p = scope.pos + scope.size * p;
                if (i == 0) nvgMoveTo(args.vg, VEC_ARGS(p));
                else        nvgLineTo(args.vg, VEC_ARGS(p));
            }
            nvgLineCap(args.vg, NVG_ROUND);
            nvgMiterLimit(args.vg, 2.f);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStrokeColor(args.vg, nvgRGB(0x00, 0xe5, 0xff));  // cyan
            nvgStroke(args.vg);

            nvgResetScissor(args.vg);
        }
        LedDisplay::drawLayer(args, layer);
    }
};
```
Note: `RackWavetableProvider` (already included via `#include "RackWavetableProvider.hpp"` at the top of `Ondes.cpp`) reads the shared static `WavetableData`, so a stack instance is cheap and stateless.

- [ ] **Step 3: Update control positions and wire the display in OndesWidget**

Replace the body of `OndesWidget`'s constructor param/input/output placement with the coordinates recorded in Task 1 Step 5 (values below are placeholders — substitute the real cx/cy in mm from `res/Ondes.svg`). Change `PITCH_PARAM` from `RoundLargeBlackKnob` to `RoundBlackKnob` (regular size), and the attenuverters stay `Trimpot`:
```cpp
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(PX_PITCH, PY_PITCH)), module, Ondes::PITCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(PX_BANK, PY_BANK)),   module, Ondes::BANK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(PX_POS,  PY_POS)),    module, Ondes::POSITION_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(PX_BANK_AMT, PY_BANK_AMT)),  module, Ondes::BANK_AMT_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(PX_POS_AMT,  PY_POS_AMT)),   module, Ondes::POSITION_AMT_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PX_VOCT, PY_VOCT)),       module, Ondes::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PX_BANK_CV, PY_BANK_CV)), module, Ondes::BANK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PX_POS_CV, PY_POS_CV)),   module, Ondes::POSITION_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(PX_OUT, PY_OUT)),       module, Ondes::OUT_OUTPUT));
```
Then, after the ports, add the display sized from the `SCREEN#Widget` rect x/y/w/h in mm from Step 5 (substitute the real values for `WAVE_X/Y/W/H`):
```cpp
        WavetableDisplay* display = createWidget<WavetableDisplay>(mm2px(Vec(WAVE_X, WAVE_Y)));
        display->box.size = mm2px(Vec(WAVE_W, WAVE_H));
        display->module = module;
        addChild(display);
```
Keep the two existing `ScrewBlack` children as-is (they are at box corners, not tied to the panel width beyond `box.size.x`).

- [ ] **Step 4: Build the VCV plugin**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/add-ondes
make -C vcv -j 2>&1 | tail -15
```
Expected: compiles cleanly to `vcv/plugin.dylib` with no errors/warnings referencing `Ondes.cpp` or `WavetableFrame.hpp`.

- [ ] **Step 5: Install into VCV Rack (per the robotboy-vcv-install memory)**

Run (confirm the Rack2 plugins dir path first):
```bash
RB="$HOME/Library/Application Support/Rack2/plugins/RobotBoy"
mkdir -p "$RB/res"
cp vcv/plugin.dylib "$RB/plugin.dylib"
cp plugin.json "$RB/plugin.json"
cp -R res/* "$RB/res/"
```
Expected: files copied without error.

- [ ] **Step 6: User checklist (manual GUI verification — no automated GUI test)**

Ask the user to open VCV Rack and confirm on the Ondes module:
  1. Panel is 8 HP; title, then display, then Pitch+V/Oct, then Bank/Position columns (knob, CV jack, attenuverter), then Out at the bottom.
  2. The cyan plate sits behind Pitch/Bank/Position and **not** behind the Out jack.
  3. The display shows a cyan waveform that **morphs** as Bank and Position knobs (and their CV) move, and reacts to the attenuverters.
  4. Audio still sounds correct (Pitch/V/Oct track; Bank/Position sweep the timbre).
Fix any placement issues by editing `panel-specs/ondes.yaml` + regenerating (Task 1) and re-syncing positions (this task), never by hand-editing the SVG.

- [ ] **Step 7: Commit**

```bash
git add src/particules/Ondes.cpp
git commit -m "feat: add morphing wavetable display and update Ondes control layout"
```

---

### Task 4: Regenerate the MetaModule faceplate and rebuild

**Files:**
- Regenerate: `metamodule/assets/Ondes.png`
- Rebuild: `metamodule/build` (`.mmplugin`)

**Interfaces:**
- Consumes: `res/Ondes.svg` (Task 1). The MetaModule build already compiles `src/particules/Ondes.cpp` (`metamodule/CMakeLists.txt:24`), so the widget/module changes from Task 3 are included automatically.

- [ ] **Step 1: Confirm Inkscape is available**

Run:
```bash
which inkscape
```
Expected: a path. If missing, stop and tell the user (the PNG export needs Inkscape); the rest of the plan is unaffected.

- [ ] **Step 2: Regenerate the faceplate PNG from the same SVG**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/add-ondes
python3 /Users/gabrielroth/Dev/metamodule-plugin-sdk/scripts/SvgToPng.py \
    --input res/Ondes.svg --output metamodule/assets/ --layer panel
```
Expected: writes `metamodule/assets/Ondes.png` (≈95×240 px scale range for 8 HP). Confirm the file's mtime updated.

- [ ] **Step 3: Rebuild the MetaModule plugin**

Run:
```bash
cmake --build metamodule/build -j 2>&1 | tail -20
```
Expected: builds without errors. If `Ondes.cpp`'s `WavetableDisplay`/NanoVG calls fail to compile under the MetaModule Rack shim, guard the display body: keep the `WavetableDisplay` struct but wrap the `drawLayer` NanoVG drawing so it still compiles (the MetaModule faceplate is a static PNG; a no-op/basic-draw fallback there is acceptable per the spec). Re-run until it builds.

- [ ] **Step 4: Commit**

```bash
git add metamodule/assets/Ondes.png
git commit -m "chore: regenerate Ondes MetaModule faceplate for 8 HP panel"
```

---

## Notes for the executor

- Tasks 1→3 are strictly sequential (Task 3 needs the SVG positions from Task 1; Task 2 is independent and could run in parallel with Task 1). Task 4 needs Task 1's SVG and builds on Task 3's source.
- The only placeholders that are intentionally deferred are the numeric coordinates (`PX_*`, `WAVE_*`, and the `zones:` rect) — they are *read from the generated SVG* in Task 1 Step 5 and substituted in-place; they are data, not undefined behavior.
- Never hand-edit `res/Ondes.svg`. All panel changes go through `panel-specs/ondes.yaml`.
