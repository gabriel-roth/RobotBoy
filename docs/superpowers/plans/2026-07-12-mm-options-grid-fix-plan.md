# MetaModule Options-section + Grid-knob fixes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Fix two MetaModule plugin bugs so Loooop/Löp match VCV: (1) the Grid knob steps through all six positions, and (2) the menu-only params (Crossfade, per-head One-shot / Speed-V-Oct / Exclude-from-Grid) appear in an "Options" roller section instead of interleaved among the panel params.

**Architecture:** Both are declaration/ordering fixes in the native MetaModule element headers. No core `.cc` logic changes, no VCV changes, no DSP changes.

**Tech Stack:** C++20. MetaModule build: `cmake --build metamodule/build -j8` (arm-none-eabi + `METAMODULE_SDK_DIR`, default `~/Dev/metamodule-plugin-sdk`), output `metamodule/metamodule-plugins/RobotBoy.mmplugin`. Regression guards: `tests/run.sh`, `make -C vcv -j8`. SDK element defs: `~/Dev/metamodule-plugin-sdk/core-interface/CoreModules/elements/base_element.hh`.

## Global Constraints

- Commit messages: short, one sentence, ≤15 words. **No `Co-Authored-By` / AI attribution.**
- Working dir: `/Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-fixes` (branch `mm-panel-fixes`).
- `SmartCoreProcessor` maps the `Elem` enum → element array BY INDEX; the array order and enum order MUST stay identical.
- Cores reference elements by enum NAME (`getState<X>()`, `updateHead<…>` template args), so reordering array+enum in lockstep needs NO `.cc` change. Do not edit `LoooopCore.cc`/`LopCore.cc`.
- Pre-release: index shifts are a clean break; no patch migration.
- kMidi out of scope. Do not touch VCV files or `LoopEngine`.
- Spec: `docs/superpowers/specs/2026-07-12-mm-options-grid-fix-design.md`.

---

## Task 1: Grid knob steps through all six positions (`max_value` fix)

**Files:**
- Modify: `metamodule/loooop/QlpElements.hh` (`QlpGridKnob`)

**Context:** `KnobSnapped` is a `Pot`; the firmware's manual-adjust arc range comes from `min_value..max_value`. Every shipping 4ms `KnobSnapped` sets `max_value = num_pos-1`. Our `QlpGridKnob` set `num_pos=6` but left `min_value/max_value` at the `Pot` defaults `0.0/1.0` → only 2 arc stops → Off/64 only. Setting `max_value=5` (=num_pos-1) restores all six stops. Stored value stays normalized 0..1 (manual-adjust stores `k/5`), so the cores' `lround(getState<GridKnob>()*5)` is unchanged. This struct is shared, so both Loooop and Löp are fixed at once.

**Interfaces:** No API change. `QlpGridKnob` now declares `min_value=0, max_value=5`.

- [ ] **Step 1: Confirm the `Pot`/`Knob` field order**

Read `~/Dev/metamodule-plugin-sdk/core-interface/CoreModules/elements/base_element.hh` for `Pot` (fields after the `ImageElement` base: `default_value`, `min_value`, `max_value`) and `Knob`/`KnobSnapped`. Confirm the aggregate slot order so the new values land in `min_value`/`max_value` (not angles). Compare against a 4ms example if available (e.g. `OctaveKnob` in the SDK/firmware `4ms_elements.hh`: `KnobSnapped{{{{b, img}, defaultValue, 0, 8}}}; num_pos=9;`).

- [ ] **Step 2: Edit `QlpGridKnob`**

Change (current):
```cpp
struct QlpGridKnob : KnobSnapped {
    constexpr QlpGridKnob(BaseElement b)
        : KnobSnapped{{{{b, "4ms/comp/knob9mm_x.png"}, 0.f}}, 6,
                      {"Off", "4", "8", "16", "32", "64"}} {}
};
```
to add `min_value=0, max_value=5` on the `Pot` base (after `default_value`):
```cpp
struct QlpGridKnob : KnobSnapped {
    constexpr QlpGridKnob(BaseElement b)
        : KnobSnapped{{{{b, "4ms/comp/knob9mm_x.png"}, 0.f, 0.f, 5.f}}, 6,
                      {"Off", "4", "8", "16", "32", "64"}} {}
};
```
(If the brace/field order from Step 1 differs, adjust so `default_value=0`, `min_value=0`, `max_value=5`, `num_pos=6`, `pos_names` unchanged. Do NOT change `LoooopCore.cc`/`LopCore.cc`.)

- [ ] **Step 3: Build the MetaModule plugin**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-fixes
cmake -S metamodule -B metamodule/build      # only if metamodule/build not already configured
cmake --build metamodule/build -j8
```
Expected: compiles + links + "All symbols found", produces `metamodule/metamodule-plugins/RobotBoy.mmplugin`. (GUI stepping is a user check; can't be verified offline.)

- [ ] **Step 4: Commit**

```bash
git add metamodule/loooop/QlpElements.hh
git commit -m "fix: Grid knob max_value=num_pos-1 so MetaModule steps through all six positions"
```

---

## Task 2: Menu params grouped into an "Options" roller section (reorder)

**Files:**
- Modify: `metamodule/loooop/Loooop_info.hh` (element array + `Elem` enum + comment)
- Modify: `metamodule/loooop/Lop_info.hh` (element array + `Elem` enum + comment)

**Context:** The firmware roller headers a param group that follows the jacks as **"Options:"** and a param group before the jacks as **"Params:"** (`append_header`). Currently the menu-only alt-params sit before the jacks, so they read as "Params" and interleave with the panel knobs. Move ALL menu-only alt-params into one contiguous block placed AFTER the output jacks (before `QlpDisplay`), grouped by command to mirror VCV's command-first menu. Panel knobs/buttons stay in the leading Params region; jacks stay in the middle.

**The alt-param element KINDS to move** (leave every other element where it is): `QlpCrossfadeAlt`, `QlpTrigModeAlt`, `QlpVoctAlt`, `QlpExcludeGridAlt`.

**Interfaces:** No API change; element names unchanged, only their array position and matching enum position. `SmartCoreProcessor` index mapping updates automatically; cores unchanged.

- [ ] **Step 1: Reorder `Loooop_info.hh`**

In the `Elements` array (size stays 90 — nothing added/removed, only moved):
- **Params region (before jacks):** keep `Record`, `OverdubButton`, `Clear`, `GridKnob`, `Dry/Wet`, then per head the six knobs `Size/Position/Speed/Jitter/Pan/Level` (×4). Remove `QlpCrossfadeAlt` from the global params, and remove `QlpTrigModeAlt`/`QlpVoctAlt`/`QlpExcludeGridAlt` from each per-head block.
- **Jacks region:** unchanged (global jacks, per-head input jacks, output jacks).
- **New Options block — insert immediately AFTER the last output jack (`Head4OutR`) and BEFORE `QlpDisplay`**, grouped by command (mirrors VCV):
  ```
  QlpCrossfadeAlt{{0.f,0.f,Center,"Crossfade",""}, 0},
  QlpTrigModeAlt{{0.f,0.f,Center,"Trig 1 mode",""}},  ... Trig 2/3/4 mode,
  QlpVoctAlt{{0.f,0.f,Center,"Speed 1 V/Oct",""}},    ... Speed 2/3/4 V/Oct,
  QlpExcludeGridAlt{{0.f,0.f,Center,"Grid 1 exclude",""}}, ... Grid 2/3/4 exclude,
  ```
  (Preserve each element's exact existing `short_name` strings — copy them from the current per-head entries: "Trig 1 mode", "Speed 1 V/Oct", "Grid 1 exclude", etc. `QlpCrossfadeAlt` keeps its `, 0` default arg.)
- **`Elem` enum:** reorder identically. Global params become `RecordButton, OverdubButton, ClearButton, GridKnob, DryWetKnob`; per-head params become just `Size{n}Knob, Position{n}Knob, Speed{n}Knob, Jitter{n}Knob, Pan{n}Knob, Level{n}Knob`; the jack enumerators stay; then the new Options enumerators in the SAME order as the array block: `CrossfadeSwitch, TrigMode1Alt, TrigMode2Alt, TrigMode3Alt, TrigMode4Alt, SpeedVoct1Alt, SpeedVoct2Alt, SpeedVoct3Alt, SpeedVoct4Alt, ExcludeGrid1Alt, ExcludeGrid2Alt, ExcludeGrid3Alt, ExcludeGrid4Alt`; then `Display`.
- Keep every enumerator NAME exactly as today (e.g. `TrigMode1Alt`, `SpeedVoct1Alt`, `ExcludeGrid1Alt`, `CrossfadeSwitch`) so the cores' `getState<…>()` / `updateHead<…>` references still resolve.
- Update the header comment block (~lines 28-36) to describe the new layout: panel knobs per head in the Params region; alt-params grouped after the jacks as the Options section.

- [ ] **Step 2: Reorder `Lop_info.hh`** (size stays 26)

Löp's alt-params are `QlpTrigModeAlt` ("Trigger"), `QlpVoctAlt` ("Speed CV V/Oct"), `QlpCrossfadeAlt` ("Crossfade"). Currently `TrigModeAlt`/`SpeedVoctAlt` are in the leading params block and `CrossfadeSwitch` is among the global params.
- **Params region:** keep `Size, Position, Speed, Jitter` knobs, then `Dry/Wet, Record, Clear, OverdubButton, GridKnob`. Remove `TrigModeAlt`, `SpeedVoctAlt` from the params block and `CrossfadeSwitch` from the global params.
- **Jacks region:** unchanged.
- **Options block — after the last output jack (`OutR`), before `QlpDisplay`:** `QlpCrossfadeAlt{…"Crossfade"…, 0}`, `QlpTrigModeAlt{…"Trigger"…}`, `QlpVoctAlt{…"Speed CV V/Oct"…}` (preserve exact existing short_names).
- **`Elem` enum:** reorder identically — params `SizeKnob, PositionKnob, SpeedKnob, JitterKnob, DryWetKnob, RecordButton, ClearButton, OverdubButton, GridKnob`; jacks unchanged; Options `CrossfadeSwitch, TrigModeAlt, SpeedVoctAlt`; then `Display`. Keep enumerator names exactly as today.
- Update the header comment to match.

- [ ] **Step 3: Build the MetaModule plugin**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-fixes
cmake --build metamodule/build -j8
```
Expected: compiles + links + "All symbols found", `RobotBoy.mmplugin` produced. A build error about a missing/duplicate enumerator or size mismatch means the array and enum drifted — fix so they match index-for-index. Verify the array element count is unchanged (Loooop 90, Löp 26) — nothing was added or removed, only moved.

- [ ] **Step 4: Regression guard (VCV + lanes unaffected, confirm)**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-fixes
( cd tests && ./run.sh ) && make -C vcv -j8
```
Expected: both green (this task touches only MM headers, but confirm no accidental breakage).

- [ ] **Step 5: Commit**

```bash
git add metamodule/loooop/Loooop_info.hh metamodule/loooop/Lop_info.hh
git commit -m "fix: group MetaModule menu params into an Options roller section after the jacks"
```

---

## Completion

1. Final whole-branch review per superpowers:requesting-code-review — focus on `Elem`↔array index alignment in both modules (the main risk) and that only element positions changed (no name/logic changes, no core edits).
2. Rebuild `RobotBoy.mmplugin` from the final commit and confirm it's present + "All symbols found".
3. Do NOT merge — leave for the user, with the built `.mmplugin` ready to install and a note of the pending GUI user check (Options section shows the four command groups; Grid knob steps through all six).
