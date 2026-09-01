# Loooop / Löp Empty-Buffer Wet Monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While Loooop or Löp is recording its very first pass (no loop exists yet), the Mix output's Wet component monitors the live Dry signal instead of silence, on both VCV and MetaModule builds of both modules.

**Architecture:** Add one small, Rack-free, MetaModule-free predicate `loooop::monitorDryWhileEmpty(bool recording, bool hasLoop)` to the already-shared `src/loooop/LooperModuleDSP.hpp`. Each of the four host files (`Loooop.cpp`, `LoooopCore.cc`, `Lop.cpp`, `LopCore.cc`) calls it immediately before its existing `dryWet()` call and substitutes the dry input for the wet operand when it returns true. No `LoopEngine` (native-core) changes — `isRecording()` and `hasLoop()` already exist.

**Tech Stack:** C++20. VCV Rack SDK (`make -C vcv`). MetaModule Plugin SDK (`cmake --build` under `metamodule/build/`, ARM cross-compiler on `PATH`). Offline `g++` unit tests under `tests/loooop/`, run via `tests/run.sh`.

**Spec:** `docs/superpowers/specs/2026-08-27-loooop-lop-empty-buffer-monitor-design.md`

## Global Constraints

- No changes to `src/loooop/dsp/LoopEngine.{hpp,cpp}` (native-core) — the fix is host-layer only.
- Scope is the Mix/Wet bus only. Loooop's 4 individual `HEAD1–4` L/R outputs stay silent while the buffer is empty; do not touch them.
- Idle-with-empty-buffer (not recording, no loop) stays silent — the passthrough only triggers when actively recording AND no loop exists yet.
- All four host call sites (`Loooop.cpp`, `LoooopCore.cc`, `Lop.cpp`, `LopCore.cc`) must call the same `loooop::monitorDryWhileEmpty()` symbol — no duplicated inline boolean logic, so VCV and MetaModule can't drift out of sync.
- Commit messages: short, one sentence, no AI attribution.

---

### Task 1: `monitorDryWhileEmpty()` helper + unit tests (TDD)

**Files:**
- Modify: `src/loooop/LooperModuleDSP.hpp:60-63` (insert new function immediately after `dryWet()`, before `panLeftGain()`)
- Modify: `tests/loooop/test_module_dsp.cpp:39-41` (insert new assertions immediately after the existing "dry wet mix" check, before the pan-balance checks)

**Interfaces:**
- Produces: `bool loooop::monitorDryWhileEmpty(bool recording, bool hasLoop)` — used by Task 2's four host call sites. Returns `true` only when `recording == true && hasLoop == false`.

- [ ] **Step 1: Write the failing test**

In `tests/loooop/test_module_dsp.cpp`, find this existing block:

```cpp
    check(near(loooop::dryWet(0.25f, 0.75f, 0.4f), 0.45f), "dry wet mix");
    check(near(loooop::panLeftGain(-0.5f), 1.0f) && near(loooop::panRightGain(-0.5f), 0.5f),
          "left pan balance");
```

Insert new assertions between the two lines so the block reads:

```cpp
    check(near(loooop::dryWet(0.25f, 0.75f, 0.4f), 0.45f), "dry wet mix");

    // Wet bus monitors Dry only while actively recording the very first
    // pass (no loop exists yet); idle-empty and post-loop states are
    // unaffected, including a later overdub pass on an existing loop.
    check(loooop::monitorDryWhileEmpty(false, false) == false,
          "idle, empty buffer stays silent");
    check(loooop::monitorDryWhileEmpty(true, false) == true,
          "recording the first pass monitors dry");
    check(loooop::monitorDryWhileEmpty(false, true) == false,
          "loop closed, not recording reads the buffer");
    check(loooop::monitorDryWhileEmpty(true, true) == false,
          "overdubbing an existing loop reads the buffer, not dry");

    check(near(loooop::panLeftGain(-0.5f), 1.0f) && near(loooop::panRightGain(-0.5f), 0.5f),
          "left pan balance");
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd ~/Dev/RobotBoy/tests
g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
    -I../src/particules/dsp/include -o ../build/tests/test_module_dsp loooop/test_module_dsp.cpp
```

Expected: compile error — `'monitorDryWhileEmpty' is not a member of 'loooop'` (the function doesn't exist yet).

- [ ] **Step 3: Implement the helper**

In `src/loooop/LooperModuleDSP.hpp`, find:

```cpp
inline float dryWet(float dry, float wet, float mix) {
    return dry * (1.0f - mix) + wet * mix;
}

inline float panLeftGain(float pan) {
```

Change to:

```cpp
inline float dryWet(float dry, float wet, float mix) {
    return dry * (1.0f - mix) + wet * mix;
}

// True while actively recording the very first pass (no loop exists yet):
// the Wet bus has nothing to read back, so hosts should monitor the dry
// signal instead of the (currently silent) head output. False once a loop
// exists, even during a later overdub pass -- "first time" only.
inline bool monitorDryWhileEmpty(bool recording, bool hasLoop) {
    return recording && !hasLoop;
}

inline float panLeftGain(float pan) {
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd ~/Dev/RobotBoy/tests
g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
    -I../src/particules/dsp/include -o ../build/tests/test_module_dsp loooop/test_module_dsp.cpp \
    && ../build/tests/test_module_dsp
```

Expected: every line prints `ok:`, including the four new assertions, exit code 0.

- [ ] **Step 5: Commit**

```bash
cd ~/Dev/RobotBoy
git add src/loooop/LooperModuleDSP.hpp tests/loooop/test_module_dsp.cpp
git commit -m "Loooop: add monitorDryWhileEmpty helper with tests"
```

---

### Task 2: Wire the four hosts

**Files:**
- Modify: `src/loooop/Loooop.cpp:258-262`
- Modify: `metamodule/loooop/LoooopCore.cc:134-139`
- Modify: `src/loooop/Lop.cpp:186-191`
- Modify: `metamodule/loooop/LopCore.cc:136-141`

**Interfaces:**
- Consumes: `bool loooop::monitorDryWhileEmpty(bool recording, bool hasLoop)` from Task 1.

- [ ] **Step 1: Wire `src/loooop/Loooop.cpp` (VCV, 4-head)**

Find:

```cpp
        const float w = mixSm.process(loooop::normalizedControl(
            params[DRYWET_PARAM].getValue(), inputs[DRYWET_CV_INPUT].getVoltage()));
        outputs[MIX_L_OUTPUT].setVoltage(loooop::dryWet(inL, wetL, w) * 5.f);
        outputs[MIX_R_OUTPUT].setVoltage(loooop::dryWet(inR, wetR, w) * 5.f);
```

Change to:

```cpp
        if (loooop::monitorDryWhileEmpty(engine.isRecording(), engine.hasLoop())) {
            wetL = inL;
            wetR = inR;
        }
        const float w = mixSm.process(loooop::normalizedControl(
            params[DRYWET_PARAM].getValue(), inputs[DRYWET_CV_INPUT].getVoltage()));
        outputs[MIX_L_OUTPUT].setVoltage(loooop::dryWet(inL, wetL, w) * 5.f);
        outputs[MIX_R_OUTPUT].setVoltage(loooop::dryWet(inR, wetR, w) * 5.f);
```

(`wetL`/`wetR` are declared `float wetL = 0.f, wetR = 0.f;` earlier in the same `process()` and accumulated in the per-head loop just above this block — they are non-`const`, so this reassignment compiles as-is.)

- [ ] **Step 2: Wire `metamodule/loooop/LoooopCore.cc` (MetaModule, 4-head)**

Find:

```cpp
        float wetL = hs[0].l*g0.l*lv0 + hs[1].l*g1.l*lv1 + hs[2].l*g2.l*lv2 + hs[3].l*g3.l*lv3;
        float wetR = hs[0].r*g0.r*lv0 + hs[1].r*g1.r*lv1 + hs[2].r*g2.r*lv2 + hs[3].r*g3.r*lv3;
        float w = mixSm_.process(loooop::normalizedControl(
            getState<DryWetKnob>(), getInput<DryWetCvIn>().value_or(0.f)));
        setOutput<MixOutL>(loooop::dryWet(inL, wetL, w) * 5.f);
        setOutput<MixOutR>(loooop::dryWet(inR, wetR, w) * 5.f);
```

Change to:

```cpp
        float wetL = hs[0].l*g0.l*lv0 + hs[1].l*g1.l*lv1 + hs[2].l*g2.l*lv2 + hs[3].l*g3.l*lv3;
        float wetR = hs[0].r*g0.r*lv0 + hs[1].r*g1.r*lv1 + hs[2].r*g2.r*lv2 + hs[3].r*g3.r*lv3;
        if (loooop::monitorDryWhileEmpty(engine_.isRecording(), engine_.hasLoop())) {
            wetL = inL;
            wetR = inR;
        }
        float w = mixSm_.process(loooop::normalizedControl(
            getState<DryWetKnob>(), getInput<DryWetCvIn>().value_or(0.f)));
        setOutput<MixOutL>(loooop::dryWet(inL, wetL, w) * 5.f);
        setOutput<MixOutR>(loooop::dryWet(inR, wetR, w) * 5.f);
```

- [ ] **Step 3: Wire `src/loooop/Lop.cpp` (VCV, single-head)**

Find:

```cpp
        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine.process(inL, inR, hs);
        const float w = mixSm.process(loooop::normalizedControl(
            params[DRYWET_PARAM].getValue(), inputs[DRYWET_CV_INPUT].getVoltage()));
        outputs[OUT_L_OUTPUT].setVoltage(loooop::dryWet(inL, hs[0].l, w) * 5.f);
        outputs[OUT_R_OUTPUT].setVoltage(loooop::dryWet(inR, hs[0].r, w) * 5.f);
```

Change to:

```cpp
        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine.process(inL, inR, hs);
        const bool monitorDry = loooop::monitorDryWhileEmpty(engine.isRecording(), engine.hasLoop());
        const float wetL = monitorDry ? inL : hs[0].l;
        const float wetR = monitorDry ? inR : hs[0].r;
        const float w = mixSm.process(loooop::normalizedControl(
            params[DRYWET_PARAM].getValue(), inputs[DRYWET_CV_INPUT].getVoltage()));
        outputs[OUT_L_OUTPUT].setVoltage(loooop::dryWet(inL, wetL, w) * 5.f);
        outputs[OUT_R_OUTPUT].setVoltage(loooop::dryWet(inR, wetR, w) * 5.f);
```

- [ ] **Step 4: Wire `metamodule/loooop/LopCore.cc` (MetaModule, single-head)**

Find:

```cpp
        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine_.process(inL, inR, hs);
        float w = mixSm_.process(loooop::normalizedControl(
            getState<DryWetKnob>(), getInput<DryWetCvIn>().value_or(0.f)));
        setOutput<OutL>(loooop::dryWet(inL, hs[0].l, w) * 5.f);
        setOutput<OutR>(loooop::dryWet(inR, hs[0].r, w) * 5.f);
```

Change to:

```cpp
        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine_.process(inL, inR, hs);
        const bool monitorDry = loooop::monitorDryWhileEmpty(engine_.isRecording(), engine_.hasLoop());
        const float wetL = monitorDry ? inL : hs[0].l;
        const float wetR = monitorDry ? inR : hs[0].r;
        float w = mixSm_.process(loooop::normalizedControl(
            getState<DryWetKnob>(), getInput<DryWetCvIn>().value_or(0.f)));
        setOutput<OutL>(loooop::dryWet(inL, wetL, w) * 5.f);
        setOutput<OutR>(loooop::dryWet(inR, wetR, w) * 5.f);
```

- [ ] **Step 5: Build VCV and verify it compiles clean**

```bash
cd ~/Dev/RobotBoy
make -C vcv -B -j4 2>&1 | tail -20
```

Expected: build succeeds (deprecation warnings from Rack's `helpers.hpp` are expected and harmless; no errors). If it fails, stop and fix before continuing — do not proceed to the MetaModule build with a broken VCV build.

- [ ] **Step 6: Build MetaModule and verify it compiles clean**

```bash
export PATH="/Users/gabrielroth/Dev/opt/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
cd ~/Dev/RobotBoy/metamodule/build && cmake --build . 2>&1 | tail -25
```

Expected: `All symbols found!` followed by `Creating plugin at .../metamodule-plugins/RobotBoy.mmplugin`. If cmake fails with a stale-cache error, delete `CMakeCache.txt` and re-run `cmake ..` (same `PATH`) before building again — do not touch panel assets or run any panel-generation script, no panel changed in this task.

- [ ] **Step 7: Commit**

```bash
cd ~/Dev/RobotBoy
git add src/loooop/Loooop.cpp src/loooop/Lop.cpp metamodule/loooop/LoooopCore.cc metamodule/loooop/LopCore.cc
git commit -m "Loooop/Lop: Wet monitors Dry while the buffer is empty"
```

---

### Task 3: Manual update

**Files:**
- Modify: `Loooop.md:65`

- [ ] **Step 1: Update the Dry/Wet bullet**

Find:

```markdown
- **Dry/Wet** — Sets the blend between incoming audio (dry) and loop playback (wet) at the Mix output. Fully clockwise = loop only.
```

Change to:

```markdown
- **Dry/Wet** — Sets the blend between incoming audio (dry) and loop playback (wet) at the Mix output. Fully clockwise = loop only. While recording the very first pass (before any loop exists), Wet monitors the incoming audio too, so Mix always lets you hear what's being recorded, regardless of this knob's position.
```

This single bullet covers Löp as well — the manual's Löp section (`## Löp`, near the end of the file) states Löp "works like one head of Loooop, minus the mixing controls" and has no separate Dry/Wet entry.

- [ ] **Step 2: Commit**

```bash
cd ~/Dev/RobotBoy
git add Loooop.md
git commit -m "Loooop manual: note Wet monitors Dry during first recording"
```

---

### Task 4: Verification gate

No files modified. Confirms the full change set is consistent before calling this done.

- [ ] **Step 1: Run the offline test suite**

```bash
cd ~/Dev/RobotBoy/tests && ./run.sh
```

Expected: exit code 0, all `ok:`/`PASS` lines, including the four new `monitorDryWhileEmpty` assertions from Task 1.

- [ ] **Step 2: Rebuild both hosts clean**

```bash
cd ~/Dev/RobotBoy
make -C vcv -B -j4 2>&1 | tail -20
export PATH="/Users/gabrielroth/Dev/opt/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH"
cd ~/Dev/RobotBoy/metamodule/build && cmake --build . 2>&1 | tail -25
```

Expected: both succeed with no errors (same success criteria as Task 2 Steps 5–6).

- [ ] **Step 3: Confirm scope with a diff review**

```bash
cd ~/Dev/RobotBoy
git diff HEAD~3 --stat
```

Expected: only `src/loooop/LooperModuleDSP.hpp`, `tests/loooop/test_module_dsp.cpp`, `src/loooop/Loooop.cpp`, `src/loooop/Lop.cpp`, `metamodule/loooop/LoooopCore.cc`, `metamodule/loooop/LopCore.cc`, and `Loooop.md` changed — no `src/loooop/dsp/LoopEngine.{hpp,cpp}`, no `HEAD1–4` output lines touched in `Loooop.cpp`/`LoooopCore.cc`.

No commit for this task (fix forward with a new commit if Steps 1–3 surface a problem).

---

## User verification checklist (not agent-run)

GUI/audio confirmation in the live VCV and MetaModule-simulator builds is the user's checklist item, not an agent-driven simulator test:

- Load Loooop (and Löp), turn Dry/Wet fully clockwise (Wet only), hit Record on an empty buffer, and confirm the input is audible at Mix while the first pass is being recorded.
- Confirm Mix still transitions cleanly to loop playback once the first pass closes (existing snap behavior, not a new click).
- Confirm overdubbing an *existing* loop still monitors the loop, not dry, at Wet.
- Confirm idle with an empty buffer (fresh patch, or right after Clear, before pressing Record) is still silent at Wet.
