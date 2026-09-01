# Loooop / Löp Speed Knob Notch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every Speed knob on Loooop (4 per-head) and Löp (1) hard-locks to exactly 1.0 or -1.0 within a small window around each, on both VCV and MetaModule, giving MetaModule users (who have no double-click-to-default gesture) an easy way back to normal speed.

**Architecture:** One new stateless function, `loooop::applySpeedNotch(float rawKnob)`, added to the header already shared by all four hosts (`LooperModuleDSP.hpp`). It is inserted at each host's existing raw-knob read, before CV is summed and before the V/Oct branch, so CV modulation is never affected. A companion `SpeedParamQuantity` in VCV's `plugin.hpp` (mirroring the existing `PitchParamQuantity`) keeps the VCV tooltip in sync with the locked value. No new params, no engine changes, no patch-persistence changes.

**Tech Stack:** C++ (shared VCV/MetaModule DSP header), lightweight test framework in `tests/loooop/` (see `tests/run.sh`, plain `check(near(...), "description")` assertions, no external framework).

**Spec:** `docs/superpowers/specs/2026-08-27-loooop-speed-knob-notch-design.md`

## Global Constraints

- No new params, enums, or element entries — this plan only wraps an existing value read, so there is nothing to append or reorder.
- CV modulation must remain fully continuous through 1.0/-1.0 — the notch applies only to the manual knob term, never to the CV-summed result.
- `kSpeedNotchWidth` is a single named constant (±0.05 raw units, on the -2..2 scale) — do not hardcode the window elsewhere.
- Commit messages: one short sentence, ≤15 words, no AI attribution lines.
- Another agent may be active on `main` in this working copy concurrently — confirm with the user whether to work in an isolated worktree (`superpowers:using-git-worktrees`) before starting Task 1, rather than assuming main is free.
- Test suite command: `bash tests/run.sh` — must end with no FAIL lines. Build check: `make -C vcv 2>&1 | tail -5` (VCV) and the MetaModule cmake flow from the `build-robotboy-plugin` skill (MM) — both must build clean.

---

### Task 1: `applySpeedNotch` + unit tests (TDD)

**Files:**
- Modify: `src/loooop/LooperModuleDSP.hpp` (add the function near `speedFromControls`, per the spec's sketch)
- Modify: `tests/loooop/test_module_dsp.cpp` (add test cases alongside the existing `speedFromControls`/`speedFromVOct` checks at lines 16-19)

**Interfaces:**
- Produces: `loooop::applySpeedNotch(float rawKnob) -> float` — pure function, no state. Later tasks call this at each of the four raw-knob read sites, wrapping the existing `spKnob` expression.

- [ ] **Step 1: Add the failing test cases to `test_module_dsp.cpp`**, next to the existing speed checks:

```cpp
    check(near(loooop::applySpeedNotch(1.03f), 1.0f), "notch snaps just inside +1 window");
    check(near(loooop::applySpeedNotch(-0.97f), -1.0f), "notch snaps just inside -1 window");
    check(near(loooop::applySpeedNotch(1.05f), 1.0f), "notch snaps exactly at +1 boundary");
    check(near(loooop::applySpeedNotch(-1.05f), -1.0f), "notch snaps exactly at -1 boundary");
    check(near(loooop::applySpeedNotch(1.06f), 1.06f), "notch passes through just outside +1 window");
    check(near(loooop::applySpeedNotch(-1.06f), -1.06f), "notch passes through just outside -1 window");
    check(near(loooop::applySpeedNotch(0.0f), 0.0f), "notch leaves freeze untouched");
    check(near(loooop::applySpeedNotch(0.4f), 0.4f), "notch leaves mid-range value untouched");
    check(near(loooop::applySpeedNotch(2.0f), 2.0f), "notch leaves max speed untouched");
    check(near(loooop::applySpeedNotch(-2.0f), -2.0f), "notch leaves min speed untouched");
```

- [ ] **Step 2: Run the test to verify it fails to compile** (function does not exist yet)

Run: `bash tests/run.sh 2>&1 | grep -A3 test_module_dsp`
Expected: build failure, `applySpeedNotch` not declared in namespace `loooop`

- [ ] **Step 3: Implement the function in `LooperModuleDSP.hpp`**, placed after `speedFromControls`/`speedFromVOct`/`VOctSpeedMemo` (around line 40, still inside `namespace loooop`):

```cpp
constexpr float kSpeedNotchTargets[2] = {1.f, -1.f};
constexpr float kSpeedNotchWidth = 0.05f;  // raw units each side; range is -2..2

// Hard-locks rawKnob to exactly 1.0 or -1.0 when it falls within
// kSpeedNotchWidth of either target; passes through unchanged otherwise.
// Stateless -- no hysteresis needed, since the two windows are isolated
// and everything outside them is plain identity (see spec's Edge Cases).
inline float applySpeedNotch(float rawKnob) {
    for (float target : kSpeedNotchTargets)
        if (std::fabs(rawKnob - target) <= kSpeedNotchWidth)
            return target;
    return rawKnob;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `bash tests/run.sh 2>&1 | tail -20`
Expected: no FAIL lines; `test_module_dsp` reports all checks passed

- [ ] **Step 5: Commit**

```bash
git add src/loooop/LooperModuleDSP.hpp tests/loooop/test_module_dsp.cpp
git commit -m "Loooop: add speed knob notch function with tests"
```

---

### Task 2: Wire the four engine call sites

**Files:**
- Modify: `src/loooop/Loooop.cpp:202` (VCV, 4-head loop)
- Modify: `src/loooop/Lop.cpp:155` (VCV, single head)
- Modify: `metamodule/loooop/LoooopCore.cc:225` (MM, `updateHead` template)
- Modify: `metamodule/loooop/LopCore.cc:103` (MM, single head)

**Interfaces:**
- Consumes: `loooop::applySpeedNotch(float) -> float` from Task 1.
- Produces: no new interface — this task only changes what value flows into the existing `loooop::speedFromControls`/`loooop::speedFromVOct` calls at each site.

- [ ] **Step 1: Wire VCV's Loooop.cpp.** Read the loop at `src/loooop/Loooop.cpp:200-206` first to confirm the surrounding lines haven't shifted, then change the `spKnob` read:

```cpp
// Before:
float spKnob = params[SPEED1_PARAM + HEAD_PARAMS * h].getValue();
// After:
float spKnob = loooop::applySpeedNotch(params[SPEED1_PARAM + HEAD_PARAMS * h].getValue());
```

- [ ] **Step 2: Wire VCV's Lop.cpp.** Read `src/loooop/Lop.cpp:150-160` first to confirm line numbers, then change:

```cpp
// Before:
float spKnob = params[SPEED_PARAM].getValue();
// After:
float spKnob = loooop::applySpeedNotch(params[SPEED_PARAM].getValue());
```

- [ ] **Step 3: Build VCV and confirm it compiles clean**

Run: `make -C vcv 2>&1 | tail -5`
Expected: build succeeds, no errors

- [ ] **Step 4: Wire MetaModule's LoooopCore.cc.** Read `metamodule/loooop/LoooopCore.cc:215-230` first to confirm the `updateHead` template body hasn't shifted, then change:

```cpp
// Before:
float spKnob = (getState<S>() - 0.5f) * 4.f;
// After:
float spKnob = loooop::applySpeedNotch((getState<S>() - 0.5f) * 4.f);
```

- [ ] **Step 5: Wire MetaModule's LopCore.cc.** Read `metamodule/loooop/LopCore.cc:95-110` first to confirm line numbers, then change:

```cpp
// Before:
float spKnob = (getState<SpeedKnob>() - 0.5f) * 4.f;
// After:
float spKnob = loooop::applySpeedNotch((getState<SpeedKnob>() - 0.5f) * 4.f);
```

- [ ] **Step 6: Build MetaModule and confirm it compiles clean.** Use the `build-robotboy-plugin` skill's MetaModule cmake flow.

Expected: build succeeds, "All symbols found!" (or equivalent clean-link confirmation used by that skill)

- [ ] **Step 7: Run the full test suite**

Run: `bash tests/run.sh 2>&1 | tail -20`
Expected: no FAIL lines

- [ ] **Step 8: Commit**

```bash
git add src/loooop/Loooop.cpp src/loooop/Lop.cpp metamodule/loooop/LoooopCore.cc metamodule/loooop/LopCore.cc
git commit -m "Loooop/Lop: apply speed knob notch on all four hosts"
```

---

### Task 3: VCV tooltip parity

**Files:**
- Modify: `src/plugin.hpp` (add `SpeedParamQuantity`, near the existing `PitchParamQuantity` at lines 27-36)
- Modify: `src/loooop/Loooop.cpp:72` (4x `configParam` → `configParam<SpeedParamQuantity>` for the Speed params)
- Modify: `src/loooop/Lop.cpp:42` (1x `configParam` → `configParam<SpeedParamQuantity>`)

**Interfaces:**
- Consumes: `loooop::applySpeedNotch(float) -> float` from Task 1.
- Produces: `SpeedParamQuantity`, a `rack::ParamQuantity` subclass, used only via `configParam<SpeedParamQuantity>(...)` — no other code depends on it directly.

- [ ] **Step 1: Add `SpeedParamQuantity` to `src/plugin.hpp`**, directly after the existing `PitchParamQuantity` struct (after line 36):

```cpp
// Keeps the tooltip in sync with the engine's speed-knob notch (see
// loooop::applySpeedNotch): display-only, doesn't change the stored
// value or the knob's drag feel.
struct SpeedParamQuantity : ParamQuantity {
	float getDisplayValue() override { return loooop::applySpeedNotch(getValue()); }
	void setDisplayValue(float v) override { setValue(v); }
};
```

Note: `src/plugin.hpp` does not currently include `loooop/LooperModuleDSP.hpp` — add that `#include` near the top of the file (alongside the existing `#include "particules/pitch_notch_map.hpp"` at line 4) so `loooop::applySpeedNotch` is visible here.

- [ ] **Step 2: Read `src/loooop/Loooop.cpp` around line 72** to confirm the exact current text, then switch the four Speed `configParam` calls to use the new quantity type:

```cpp
// Before:
configParam(SPEED1_PARAM + HEAD_PARAMS * h, -2.f, 2.f, 1.f, n + " speed");
// After:
configParam<SpeedParamQuantity>(SPEED1_PARAM + HEAD_PARAMS * h, -2.f, 2.f, 1.f, n + " speed");
```

- [ ] **Step 3: Read `src/loooop/Lop.cpp` around line 42** to confirm the exact current text, then switch its Speed `configParam` call the same way:

```cpp
// Before:
configParam(SPEED_PARAM, -2.f, 2.f, 1.f, "Speed");
// After:
configParam<SpeedParamQuantity>(SPEED_PARAM, -2.f, 2.f, 1.f, "Speed");
```

- [ ] **Step 4: Build VCV and confirm it compiles clean**

Run: `make -C vcv 2>&1 | tail -5`
Expected: build succeeds, no errors

- [ ] **Step 5: Run the full test suite** (this task touches no DSP logic, but confirms nothing else broke)

Run: `bash tests/run.sh 2>&1 | tail -20`
Expected: no FAIL lines

- [ ] **Step 6: Commit**

```bash
git add src/plugin.hpp src/loooop/Loooop.cpp src/loooop/Lop.cpp
git commit -m "Loooop/Lop: VCV speed tooltip matches the notch-locked value"
```

---

### Task 4: Manual updates

**Files:**
- Modify: `Loooop.md` only — confirmed by review: `Lop.md` does not exist, Löp is documented as a `## Löp` section inside `Loooop.md` (around line 120).

- [ ] **Step 1: Add a short note to each module's Speed knob description in `Loooop.md`** (wording to match the surrounding manual's style — keep it to one sentence): turning the knob near normal speed (1x) or reverse-normal (-1x) snaps to exactly that speed. Escape any literal tildes as `\~` per this project's Markdown convention — the file already uses this (e.g. "\~5 ms"), so keep new prose consistent.

- [ ] **Step 2: Commit**

```bash
git add Loooop.md
git commit -m "Loooop manual: document the speed knob notch"
```

---

### Task 5: Verification gate

**Files:** none (verification only)

- [ ] **Step 1: Full test suite, must be clean**

Run: `bash tests/run.sh 2>&1 | tail -30`
Expected: no FAIL lines

- [ ] **Step 2: VCV clean rebuild**

Run: `make -C vcv -B 2>&1 | tail -10`
Expected: build succeeds, no errors or warnings about the modified files

- [ ] **Step 3: MetaModule clean rebuild.** Use the `build-robotboy-plugin` skill's MetaModule cmake flow with a clean build directory.

Expected: build succeeds, no unresolved symbols

- [ ] **Step 4: Confirm no unrelated changes**

Run: `git diff main --stat` (or the appropriate base branch/worktree comparison) and eyeball that only the files listed in Tasks 1-4 changed.

- [ ] **Step 5: Add to the user's manual GUI/hardware checklist** (per project convention — an agent cannot verify feel, tooltip rendering, or real-pot behavior): confirm on a real VCV instance that dragging near 1x/-1x snaps and the tooltip reads "1.00"/"-1.00" while locked, that double-click-to-default still works, and on real MetaModule hardware that the pot lock feels right with no flutter at the window edges. This step produces no commit — it's a note for the user, not code.
