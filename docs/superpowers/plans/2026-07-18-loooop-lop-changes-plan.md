# Loooop & Löp changes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Trigger when recording" menu setting, make one-shot Size/Position track on the display, swap Löp's Clear/Record, rename the Magenta head to Purple, and relabel the Overdub button "Dub Mode" — across VCV and MetaModule.

**Architecture:** Behavior changes live in the shared `LoopEngine` (both modules use it) and are TDD'd in `tests/loooop/test_loop_engine.cpp`. Host wiring is duplicated in the VCV widgets (`Loooop.cpp`, `Lop.cpp`) and the MetaModule cores/info headers. Panel changes flow through `panel-specs/*.yaml` → panel-gen → `res/*.svg`, then hand-synced `mm2px` coords in the `.cpp` and `sync_info_positions.py` for the MetaModule info + PNG regen.

**Tech Stack:** C++20, VCV Rack SDK, MetaModule SDK (`SmartCoreProcessor`), Python panel-gen (vcv-panel skill), g++ test harness (`tests/run.sh`).

## Global Constraints

- Pre-release: param/element **ordering is free** (no saved-patch back-compat), BUT VCV `ParamId`/`InputId`/`OutputId` order MUST still mirror MetaModule `Elements`/`Elem` order element-for-element (`metamodule/loooop/*_info.hh`) for VCV↔MM patch portability within this release.
- New menu-only alt-params append at the **end of the options block** (before `Display` in MetaModule; before `PARAMS_LEN` in VCV) in all four files, keeping the mirror obvious. Default index 0 = legacy behavior (loader zero-inits MM alt-params).
- Menu-switch params set `->randomizeEnabled = false` (matches existing mode switches).
- Panel labels are stored **title-case** in the spec; panel-gen uppercases on render. Panels are vectorized (labels are `<path>`), so label/position edits require regeneration, never hand-editing the SVG text.
- Do NOT touch `kMidi` or the Lock-LED "magenta" comment in `LooperModuleDSP.hpp` (that is a different, genuine magenta).
- Run the test suite with `bash tests/run.sh`. Build VCV with `make -C vcv`; build MetaModule with its cmake flow (build-robotboy-plugin skill).

---

### Task 1: Engine — `toggleRecord(bool continueOverdub)` continues into overdub on loop close

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (declaration ~line 29)
- Modify: `src/loooop/dsp/LoopEngine.cpp` (`toggleRecord`, lines 58–101)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Produces: `void LoopEngine::toggleRecord(bool continueOverdub = false)`. When the call closes the initial recording pass (`recording_ && loopLen_ == 0`) and `continueOverdub && overdubEnabled_`, the engine freezes `loopLen_` and stays recording as an overdub pass; otherwise it stops as before.

- [ ] **Step 1: Write the failing tests** — add these two functions to `tests/loooop/test_loop_engine.cpp` (before `main`), and add `test_continue_overdub_on_close(); test_continue_overdub_lock_stops();` to the `main()` call list.

```cpp
// "Trigger when recording = Starts overdubbing": the record toggle that closes
// the initial pass freezes the loop AND keeps recording as an overdub pass.
static void test_continue_overdub_on_close() {
    LoopEngine e; e.reset(48000.f);
    soloHead0(e);
    e.setWriteMode(LoopEngine::WriteMode::Replace);   // overdub enabled (not Lock)
    e.toggleRecord();                                  // start initial pass
    for (int i = 0; i < 100; ++i) e.process(0.5f, 0.5f, *(new std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS>));
    // close with continueOverdub = true
    e.toggleRecord(true);
    check(e.hasLoop(), "continue_overdub: loop frozen on close");
    check(e.loopLength() == 100, "continue_overdub: loop length = frames recorded");
    check(e.isRecording(), "continue_overdub: still recording after close");
}

// With Overdub = Lock (overdub disabled), the setting is overridden: closing the
// initial pass stops recording regardless of continueOverdub.
static void test_continue_overdub_lock_stops() {
    LoopEngine e; e.reset(48000.f);
    soloHead0(e);
    e.setOverdub(false);                               // Lock: overdub disabled
    e.toggleRecord();
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    for (int i = 0; i < 100; ++i) e.process(0.5f, 0.5f, hs);
    e.toggleRecord(true);
    check(e.hasLoop(), "lock_stops: loop frozen on close");
    check(!e.isRecording(), "lock_stops: recording stopped despite continueOverdub");
}
```

(Note: replace the leaked `*(new ...)` in the first test with a local `std::array` like the second — inline it: declare `std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;` and pass `hs`.)

- [ ] **Step 2: Run to verify failure**

Run: `bash tests/run.sh 2>&1 | grep -A2 continue_overdub`
Expected: compile error — `toggleRecord` takes no argument.

- [ ] **Step 3: Update the declaration** in `LoopEngine.hpp` (~line 29):

```cpp
    void toggleRecord(bool continueOverdub = false);   // momentary toggle: record <-> stop/overdub; continueOverdub keeps recording as an overdub pass when closing the initial loop
```

- [ ] **Step 4: Implement** in `LoopEngine.cpp`. Change the signature and the `loopLen_ == 0` close branch (lines 79–89). Replace:

```cpp
void LoopEngine::toggleRecord(bool continueOverdub) {
    if (!recording_ && loopLen_ > 0 && !overdubEnabled_) return;
    if (!recording_) {
        recording_ = true;
        writeIdx_ = 0;
        if (loopLen_ == 0) {
            odGain_ = 1.f; odGainStep_ = 0.f;
        } else {
            odGain_ = xfadeSamples_ ? 0.f : 1.f;
            odGainStep_ = xfadeSamples_ ? 1.f / static_cast<float>(xfadeSamples_) : 0.f;
            stopPending_ = false;
            if (writeMode_ == WriteMode::Decay) {
                decayLpL_ = bufL_[0];
                decayLpR_ = bufR_[0];
            }
        }
        dispRecording_.store(true, std::memory_order_relaxed);
        dispRecLen_.store(0, std::memory_order_relaxed);
    } else if (loopLen_ == 0) {
        // Closing the initial pass: freeze the loop length now.
        loopLen_ = writeIdx_;
        dispLoopLen_.store(static_cast<std::uint32_t>(loopLen_), std::memory_order_relaxed);
        if (continueOverdub && overdubEnabled_) {
            // "Starts overdubbing": keep recording as an overdub pass from the
            // loop start, with the same declick ramp the overdub-start branch uses.
            writeIdx_ = 0;
            odGain_ = xfadeSamples_ ? 0.f : 1.f;
            odGainStep_ = xfadeSamples_ ? 1.f / static_cast<float>(xfadeSamples_) : 0.f;
            stopPending_ = false;
            if (writeMode_ == WriteMode::Decay) {
                decayLpL_ = bufL_[0];
                decayLpR_ = bufR_[0];
            }
            // recording_ and dispRecording_ stay true.
        } else {
            recording_ = false;
            dispRecording_.store(false, std::memory_order_relaxed);
        }
        // The freeze changes what the waveform shows (grid bars over a frozen
        // loop) even when continuing, so invalidate the display cache.
        bumpWaveformRevision();
    } else if (odGainStep_ > 0.f) {
        stopPending_ = !stopPending_;
    } else {
        recording_ = false;
        dispRecording_.store(false, std::memory_order_relaxed);
        dispLoopLen_.store(static_cast<std::uint32_t>(loopLen_), std::memory_order_relaxed);
    }
}
```

- [ ] **Step 5: Run to verify pass**

Run: `bash tests/run.sh 2>&1 | grep -E 'continue_overdub|lock_stops|FAIL'`
Expected: `ok:` for all four checks; no `FAIL`.

- [ ] **Step 6: Commit**

```bash
git add src/loooop/dsp/LoopEngine.hpp src/loooop/dsp/LoopEngine.cpp tests/loooop/test_loop_engine.cpp
git commit -m "LoopEngine: continueOverdub closes loop and keeps recording"
```

---

### Task 2: Engine — armed one-shot publishes live window (Size/Position on display)

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp` (`process`, the `loopLen_ > 0` head loop, lines 510–521)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Produces: while `loopLen_ > 0`, a non-playing head still republishes `dispWinStart01_/dispWinEnd01_/dispPos01_` from its current window each sample; no audio is produced.

- [ ] **Step 1: Write the failing test** — add to `tests/loooop/test_loop_engine.cpp` and register `test_armed_oneshot_window_tracks_size();` in `main()`.

```cpp
// An armed one-shot head (silent, waiting for a trigger) must still reflect
// Size/Position changes on the display window.
static void test_armed_oneshot_window_tracks_size() {
    LoopEngine e; e.reset(48000.f);
    soloHead0(e);
    e.toggleRecord();
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    for (int i = 0; i < 480; ++i) e.process(0.2f, 0.2f, hs);
    e.toggleRecord();                 // freeze loop, head 0 loops
    e.setOneShot(0, true);            // arm: head 0 goes silent
    e.setSize(0, 0.5f);
    e.setPosition(0, 0.5f);
    e.process(0.f, 0.f, hs);          // one idle sample
    auto s1 = e.displaySnapshot();
    const float halfWin = s1.winEnd01[0] - s1.winStart01[0];
    e.setSize(0, 1.0f);               // grow the window
    e.process(0.f, 0.f, hs);
    auto s2 = e.displaySnapshot();
    const float fullWin = s2.winEnd01[0] - s2.winStart01[0];
    check(fullWin > halfWin + 0.1f, "armed one-shot: window grows with Size");
    check(!s2.playing[0], "armed one-shot: head still not playing (silent)");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `bash tests/run.sh 2>&1 | grep -E 'armed one-shot|FAIL'`
Expected: FAIL on "window grows with Size" (window stays frozen).

- [ ] **Step 3: Implement** — in `LoopEngine::process`, replace the `if (!h.playing) continue;` block (line 513) inside the `loopLen_ > 0` loop with:

```cpp
            if (!h.playing) {
                // Armed / finished one-shot: no audio, but keep the displayed
                // window in sync with Size/Position so the panel tracks the
                // knob. Park the head marker at the window start (direction-
                // aware) so it stays inside the window bar.
                double ws, wl;
                windowBounds(h, ws, wl);
                const float invL = 1.f / static_cast<float>(loopLen_);
                const double hp = h.speed < 0.f ? ws + wl - 1.0 : ws;
                dispPos01_[i].store(static_cast<float>(hp) * invL, std::memory_order_relaxed);
                dispWinStart01_[i].store(static_cast<float>(ws) * invL, std::memory_order_relaxed);
                dispWinEnd01_[i].store(static_cast<float>(ws + wl) * invL, std::memory_order_relaxed);
                continue;
            }
```

- [ ] **Step 4: Run to verify pass**

Run: `bash tests/run.sh 2>&1 | grep -E 'armed one-shot|FAIL'`
Expected: both checks `ok:`; no `FAIL`.

- [ ] **Step 5: Commit**

```bash
git add src/loooop/dsp/LoopEngine.cpp tests/loooop/test_loop_engine.cpp
git commit -m "LoopEngine: armed one-shot heads publish live window bounds"
```

---

### Task 3: VCV wiring — "Trigger when recording" param + menu, pass flag to engine (Loooop + Löp)

**Files:**
- Modify: `src/loooop/Loooop.cpp` (ParamId enum, `configSwitch`, `process`, `appendContextMenu`)
- Modify: `src/loooop/Lop.cpp` (ParamId enum, `configSwitch`, `process`, `appendContextMenu`)

**Interfaces:**
- Consumes: `LoopEngine::toggleRecord(bool)` from Task 1.
- Produces: a `TRIG_WHEN_REC_PARAM` on each module (0 = Stops recording, 1 = Starts overdubbing).

- [ ] **Step 1: Loooop.cpp — add the param id.** In the `ParamId` enum, append `TRIG_WHEN_REC_PARAM` immediately before `PARAMS_LEN` (after `EXCLUDE_GRID4_PARAM`):

```cpp
                   EXCLUDE_GRID1_PARAM, EXCLUDE_GRID2_PARAM, EXCLUDE_GRID3_PARAM, EXCLUDE_GRID4_PARAM,
                   TRIG_WHEN_REC_PARAM,
                   PARAMS_LEN };
```

- [ ] **Step 2: Loooop.cpp — configure it.** In the constructor, after the `GRID_PARAM` configSwitch (~line 109), add:

```cpp
        configSwitch(TRIG_WHEN_REC_PARAM, 0.f, 1.f, 0.f, "Trigger when recording",
            {"Stops recording", "Starts overdubbing"})->randomizeEnabled = false;
```

- [ ] **Step 3: Loooop.cpp — pass the flag.** Replace the record-toggle block in `process` (lines 146–149):

```cpp
        bool recBtn  = recordBtn.process(params[RECORD_PARAM].getValue());
        bool recTrig = recordTrig.process(inputs[RECORD_TRIG_INPUT].getVoltage(), 0.1f, 2.f);
        if (recBtn || recTrig)
            engine.toggleRecord(params[TRIG_WHEN_REC_PARAM].getValue() > 0.5f);
```

- [ ] **Step 4: Loooop.cpp — add the menu item.** In `appendContextMenu`, after the `MenuSeparator` (line 336), before the "Exclude from Grid" submenu, add:

```cpp
        menu->addChild(createIndexSubmenuItem("Trigger when recording",
            {"Stops recording", "Starts overdubbing"},
            [m] { return (int)std::round(m->params[Loooop::TRIG_WHEN_REC_PARAM].getValue()); },
            [m](int i) { m->paramQuantities[Loooop::TRIG_WHEN_REC_PARAM]->setValue((float)i); }));
```

- [ ] **Step 5: Lop.cpp — mirror all four edits.** Append `TRIG_WHEN_REC_PARAM` before `PARAMS_LEN` (after `SPEED_VOCT_PARAM`):

```cpp
                   CROSSFADE_PARAM, TRIG_MODE_PARAM, SPEED_VOCT_PARAM, TRIG_WHEN_REC_PARAM, PARAMS_LEN };
```

configSwitch after the `GRID_PARAM` switch (~line 56):

```cpp
        configSwitch(TRIG_WHEN_REC_PARAM, 0.f, 1.f, 0.f, "Trigger when recording",
            {"Stops recording", "Starts overdubbing"})->randomizeEnabled = false;
```

record toggle in `process` (lines 97–100):

```cpp
        bool recBtn  = recordBtn.process(params[RECORD_PARAM].getValue());
        bool recTrig = recordTrig.process(inputs[RECORD_TRIG_INPUT].getVoltage(), 0.1f, 2.f);
        if (recBtn || recTrig)
            engine.toggleRecord(params[TRIG_WHEN_REC_PARAM].getValue() > 0.5f);
```

menu item in `appendContextMenu`, after the `MenuSeparator` (line 203), before the "One-shot on trigger" item:

```cpp
        menu->addChild(createIndexSubmenuItem("Trigger when recording",
            {"Stops recording", "Starts overdubbing"},
            [m] { return (int)std::round(m->params[Lop::TRIG_WHEN_REC_PARAM].getValue()); },
            [m](int i) { m->paramQuantities[Lop::TRIG_WHEN_REC_PARAM]->setValue((float)i); }));
```

- [ ] **Step 6: Build VCV to verify it compiles.**

Run: `make -C vcv 2>&1 | tail -5`
Expected: builds with no errors (`createIndexSubmenuItem` and `std::round` are already available — `<cmath>` is included in both files).

- [ ] **Step 7: Commit**

```bash
git add src/loooop/Loooop.cpp src/loooop/Lop.cpp
git commit -m "Loooop/Löp: Trigger-when-recording menu setting (VCV)"
```

---

### Task 4: MetaModule wiring — alt-param + core for "Trigger when recording" (Loooop + Löp)

**Files:**
- Modify: `metamodule/loooop/QlpElements.hh` (new `QlpTrigWhenRecAlt`)
- Modify: `metamodule/loooop/Lop_info.hh` (Elements array size 26→27, add element; Elem enum)
- Modify: `metamodule/loooop/Loooop_info.hh` (Elements array size 90→91, add element; Elem enum)
- Modify: `metamodule/loooop/LopCore.cc` (read alt-param, pass to `toggleRecord`)
- Modify: `metamodule/loooop/LoooopCore.cc` (read alt-param, pass to `toggleRecord`)
- Modify: `metamodule/loooop/sync-map-lop.yaml` and `sync-map-loooop.yaml` (mark new enum as menu-only `null`)

**Interfaces:**
- Consumes: `LoopEngine::toggleRecord(bool)` from Task 1.
- Produces: `Elem::TrigWhenRecAlt` on both modules; index 1 = Starts overdubbing.

- [ ] **Step 1: Add the element type** to `QlpElements.hh`, after `QlpCrossfadeAlt` (default 0 = "Stops recording", the legacy behavior the loader zero-inits to):

```cpp
// Index 0 = "Stops recording" (legacy): loader zero-inits unset alt-params, so
// fresh modules/patches keep the record toggle stopping instead of overdubbing.
struct QlpTrigWhenRecAlt : AltParamChoiceLabeled {
    constexpr QlpTrigWhenRecAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 2, 0}, {"Stops recording", "Starts overdubbing"}} {}
};
```

- [ ] **Step 2: Lop_info.hh — add the element.** Change `std::array<Element, 26>` to `std::array<Element, 27>`. Add after the `QlpVoctAlt{... "Speed CV V/Oct" ...}` line (line 69), before `QlpDisplay`:

```cpp
        QlpTrigWhenRecAlt{{0.f, 0.f, Center, "Trigger when recording", "", 0.f, 0.f}},
```

Add `TrigWhenRecAlt` to the `Elem` enum after `SpeedVoctAlt`, before `Display`:

```cpp
        CrossfadeSwitch, TrigModeAlt, SpeedVoctAlt, TrigWhenRecAlt,
        Display,
```

- [ ] **Step 3: Loooop_info.hh — add the element.** Change `std::array<Element, 90>` to `std::array<Element, 91>`. Add after the last `QlpExcludeGridAlt{... "Grid 4 exclude" ...}` line, before `QlpDisplay`:

```cpp
        QlpTrigWhenRecAlt{{0.f, 0.f, Center, "Trigger when recording", ""}},
```

Add `TrigWhenRecAlt` to the `Elem` enum after `ExcludeGrid4Alt`, before `Display`:

```cpp
        ExcludeGrid1Alt, ExcludeGrid2Alt, ExcludeGrid3Alt, ExcludeGrid4Alt,
        TrigWhenRecAlt,
        Display,
```

- [ ] **Step 4: sync-maps — mark the new enum menu-only.** Append to both `sync-map-lop.yaml` and `sync-map-loooop.yaml`:

```yaml
TrigWhenRecAlt: null
```

- [ ] **Step 5: LopCore.cc — pass the flag.** Replace the record-edge block (lines 46–50):

```cpp
        bool recPressed = getState<RecordButton>() == MomentaryButton::State_t::PRESSED;
        bool recTrig = getInput<RecTrigIn>().value_or(0.f) > 1.0f;
        bool recEdge = (recPressed || recTrig) && !recPrev_;
        recPrev_ = recPressed || recTrig;
        if (recEdge) engine_.toggleRecord(getState<TrigWhenRecAlt>() == 1);
```

- [ ] **Step 6: LoooopCore.cc — pass the flag.** Change the record-edge line (`if (recEdge) engine_.toggleRecord();`) to:

```cpp
        if (recEdge) engine_.toggleRecord(getState<TrigWhenRecAlt>() == 1);
```

- [ ] **Step 7: Build MetaModule to verify.** Use the build-robotboy-plugin skill's MetaModule cmake flow.

Expected: compiles; `getState<TrigWhenRecAlt>()` resolves against the new `Elem`.

- [ ] **Step 8: Commit**

```bash
git add metamodule/loooop/QlpElements.hh metamodule/loooop/Lop_info.hh metamodule/loooop/Loooop_info.hh metamodule/loooop/LopCore.cc metamodule/loooop/LoooopCore.cc metamodule/loooop/sync-map-lop.yaml metamodule/loooop/sync-map-loooop.yaml
git commit -m "Loooop/Löp: Trigger-when-recording alt-param (MetaModule)"
```

---

### Task 5: Rename Magenta → Purple (rename only)

**Files:**
- Modify: `src/loooop/HeadColors.hpp` (line 16)
- Modify (comments only): `src/loooop/Loooop.cpp` (line 14), `src/loooop/display/LoopWaveformRenderer.hpp` (line 24), `panel-specs/loooop.yaml` (lines 38–39)
- Check: `tests/test_head_colors.py`

**Interfaces:**
- Produces: head-4 menu label "Purple playhead".

- [ ] **Step 1: Confirm the test keys on RGB, not the name.**

Run: `grep -n -i 'magenta\|purple\|name' tests/test_head_colors.py`
Expected: assertions reference RGB hex/values. If any assert the string `"Magenta"`, note it for Step 3.

- [ ] **Step 2: Rename the color name** in `HeadColors.hpp` line 16:

```cpp
    {0xFF, 0x5A, 0xF0, "Purple"},
```

- [ ] **Step 3: Update descriptive comments** (color name only; keep them accurate):
  - `Loooop.cpp` line 14: `H3 blue, H4 magenta.` → `H3 blue, H4 purple.`
  - `LoopWaveformRenderer.hpp` line 24: `H3 blue, H4 magenta.` → `H3 blue, H4 purple.`
  - `panel-specs/loooop.yaml` lines 38–39: change the `H4 magenta` / `red/blue/magenta` wording to `purple` correspondingly.
  - If Step 1 found a name assertion in `test_head_colors.py`, update `"Magenta"` → `"Purple"` there.
  - Do NOT change `LooperModuleDSP.hpp:91` (`Lock - magenta`) — that is the Lock LED color, genuinely magenta.

- [ ] **Step 4: Run the test suite.**

Run: `bash tests/run.sh 2>&1 | grep -iE 'head_color|FAIL|purple|magenta'`
Expected: head-color test passes; no `FAIL`.

- [ ] **Step 5: Commit**

```bash
git add src/loooop/HeadColors.hpp src/loooop/Loooop.cpp src/loooop/display/LoopWaveformRenderer.hpp panel-specs/loooop.yaml tests/test_head_colors.py
git commit -m "Loooop: rename head 4 Magenta -> Purple (label only)"
```

---

### Task 6: Löp panel — swap Clear/Record (Record on the left)

**Files:**
- Modify: `panel-specs/lop.yaml` (row-3 controls: swap `col:` for RECORD/CLEAR button, trig, label)
- Regenerate: `res/Lop.svg` (panel-gen)
- Modify: `src/loooop/Lop.cpp` (swap `mm2px` x-coords for RECORD/CLEAR button + trig)
- Regenerate: `metamodule/loooop/Lop_info.hh` positions (`sync_info_positions.py`) + MetaModule PNG `metamodule/assets/Loooop/Lop.png`

**Interfaces:**
- Consumes: nothing. Purely positional; param/Elem order unchanged.

- [ ] **Step 1: Edit `panel-specs/lop.yaml`.** In the row-3 block, swap columns so Record is col 2 and Clear is col 3 (button, trig jack, and label):

```yaml
  - {text: Clear,   grid: row23, col: 3, row: row3_label}
  - {text: Record,  grid: row23, col: 2, row: row3_label}
  - {name: DRYWET_PARAM,     grid: row23, col: 1, row: row3_top}
  - {name: RECORD_PARAM,     widget: VCVButton, grid: row23, col: 2, row: row3_top}
  - {name: CLEAR_PARAM,      widget: VCVButton, grid: row23, col: 3, row: row3_top}
  - {name: DRYWET_CV_INPUT,   grid: row23, col: 1, row: row3_bot}
  - {name: RECORD_TRIG_INPUT, grid: row23, col: 2, row: row3_bot}
  - {name: CLEAR_TRIG_INPUT,  grid: row23, col: 3, row: row3_bot}
```

(Keep the existing `Clear` label line — currently the file lists a `Record` label; ensure both a `Clear` and a `Record` label exist at their new columns. Verify against the current file and adjust so both labels are present.)

- [ ] **Step 2: Regenerate the panel SVG** via the vcv-panel skill (panel-gen for `lop.yaml`). Confirm the components layer in `res/Lop.svg` now has `RECORD_PARAM` at the middle column x (was 48.8 → 30.48) and `CLEAR_PARAM` at the right column x (was 30.48 → 48.8), with the trig jacks matching.

Run: `grep -n 'RECORD_PARAM\|CLEAR_PARAM\|RECORD_TRIG\|CLEAR_TRIG' res/Lop.svg`
Expected: RECORD_PARAM cx≈30.48, CLEAR_PARAM cx≈48.8 (and trig jacks likewise).

- [ ] **Step 3: Sync `Lop.cpp` widget coords by hand.** Swap the x in these four lines (lines 185–189) so Record uses 30.48 and Clear uses 48.8, matching the SVG:

```cpp
        addParam(createParamCentered<VCVButton>(mm2px(Vec(48.8, 92.95)), module, Lop::CLEAR_PARAM));
        addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(30.48, 92.95)), module, Lop::RECORD_PARAM, Lop::RECORD_LIGHT));
        ...
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.8, 104.9)), module, Lop::CLEAR_TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 104.9)), module, Lop::RECORD_TRIG_INPUT));
```

(Use the exact regenerated coords from Step 2 if panel-gen produced values other than 30.48/48.8.)

- [ ] **Step 4: Sync MetaModule info positions.**

Run: `python3 metamodule/loooop/sync_info_positions.py`
Then confirm `Lop_info.hh` `RecordButton`/`ClearButton`/`RecTrigIn`/`ClearTrigIn` x values swapped. If the script does not cover these, hand-edit the four x values in `Lop_info.hh` to match the SVG.

- [ ] **Step 5: Regenerate the MetaModule PNG** `metamodule/assets/Loooop/Lop.png` from the new `res/Lop.svg` using the project's SVG→PNG step (build-robotboy-plugin / vcv-to-metamodule skill).

- [ ] **Step 6: Build both targets and run tests.**

Run: `make -C vcv 2>&1 | tail -3 && bash tests/run.sh 2>&1 | grep -iE 'FAIL|panel'`
Expected: VCV builds; panel parity/overlap tests (if any for Lop) pass; no `FAIL`.

- [ ] **Step 7: Commit**

```bash
git add panel-specs/lop.yaml res/Lop.svg src/loooop/Lop.cpp metamodule/loooop/Lop_info.hh metamodule/assets/Loooop/Lop.png
git commit -m "Löp: swap Clear/Record so Record is on the left"
```

---

### Task 7: Panels — relabel Overdub → "Dub Mode" (Loooop + Löp)

**Files:**
- Modify: `panel-specs/loooop.yaml` (label `text: Overdub`), `panel-specs/lop.yaml` (label `text: Overdub`)
- Regenerate: `res/Loooop.svg`, `res/Lop.svg`
- Regenerate: `metamodule/assets/Loooop/Loooop.png`, `metamodule/assets/Loooop/Lop.png`

**Interfaces:**
- Consumes: nothing. Label text only; no control/param change.

- [ ] **Step 1: Edit the label text** in both specs:
  - `panel-specs/loooop.yaml` line ~179: `- {text: Overdub, ...}` → `- {text: Dub Mode, ...}`
  - `panel-specs/lop.yaml` line ~117: `- {text: Overdub, x: 30.48, y: 124.55}` → `- {text: Dub Mode, x: 30.48, y: 124.55}`

- [ ] **Step 2: Regenerate both SVGs** via the vcv-panel skill (panel-gen for both `loooop.yaml` and `lop.yaml`). This also picks up Task 6's `lop.yaml` swap if regenerated together.

- [ ] **Step 3: Regenerate both MetaModule PNGs** (`Loooop.png`, `Lop.png`) from the new SVGs via the SVG→PNG step.

- [ ] **Step 4: Build VCV and eyeball nothing broke.**

Run: `make -C vcv 2>&1 | tail -3 && bash tests/run.sh 2>&1 | grep -iE 'FAIL'`
Expected: builds; no `FAIL` (label change shouldn't affect any test; if a panel-text parity test exists, update its expected string to "Dub Mode").

- [ ] **Step 5: Commit**

```bash
git add panel-specs/loooop.yaml panel-specs/lop.yaml res/Loooop.svg res/Lop.svg metamodule/assets/Loooop/Loooop.png metamodule/assets/Loooop/Lop.png
git commit -m "Loooop/Löp: relabel Overdub button as Dub Mode"
```

---

### Task 8: Final verification + install + user checklist

**Files:**
- Create: `docs/superpowers/plans/2026-07-18-loooop-lop-changes-user-checklist.md`

- [ ] **Step 1: Full test suite.**

Run: `bash tests/run.sh`
Expected: all C++ and python guard tests pass.

- [ ] **Step 2: Build + install VCV** (build-robotboy-plugin skill): `make -C vcv` then copy dylib/json/res into the Rack2 plugins dir.

- [ ] **Step 3: Build MetaModule** plugin (cmake flow).

- [ ] **Step 4: Write the user GUI checklist** covering (per the no-agent-GUI-sim rule):
  - Löp: Record button/jack/label is now left of Clear; both trigger correctly.
  - Both panels read "DUB MODE" under the overdub button.
  - Loooop context menu shows head 4 as "Purple playhead".
  - "Trigger when recording" submenu present on both, both options; "Starts overdubbing" closes the loop and keeps recording (record LED stays lit) in the current write mode; with Overdub=Lock it stops.
  - Arm a one-shot head (Loooop per-head / Löp), then turn Size and Position — the armed lane's window follows live while the lane stays "asleep".

- [ ] **Step 5: Commit the checklist.**

```bash
git add docs/superpowers/plans/2026-07-18-loooop-lop-changes-user-checklist.md
git commit -m "Docs: Loooop/Löp changes user GUI checklist"
```

---

## Self-review notes

- **Spec coverage:** Item 1 → Tasks 1,3,4. Item 2 → Tasks 2 (both modules via shared engine; VCV/MM already call `process` which now publishes). Item 3 → Task 6. Item 4 → Task 5. Item 5 → Task 7. All covered.
- **Type consistency:** `toggleRecord(bool continueOverdub = false)` used identically in Tasks 1/3/4. `TRIG_WHEN_REC_PARAM` (VCV) ↔ `TrigWhenRecAlt` (MM) named consistently. `QlpTrigWhenRecAlt` defined in Task 4 Step 1 before use.
- **Ordering invariant:** new param appended at end of options block in VCV and MM in the same relative slot; array sizes bumped (26→27, 90→91).
- **Panels:** vectorized, so all label/position changes regenerate the SVG then sync to `.cpp` (hand) and `Lop_info.hh` (script) and PNG. Tasks 6 & 7 both regenerate `res/Lop.svg`; if executed together, regenerate once after both spec edits.
