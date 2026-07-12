# MetaModule panel Overdub button + Grid knob — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the MetaModule native cores expose Overdub as a click-cycling 5-state RGB panel button and Grid as a stepped panel knob — matching VCV — replacing today's menu alt-params, on both Loooop and Löp.

**Architecture:** Move the two Rack-free control pieces (`kOverdubColors`, `applyOverdub`) into the shared `LooperModuleDSP.hpp` so both hosts use them; declare `MomentaryButtonRGB` + `KnobSnapped` panel elements in the native `_info.hh` files; drive them from `LoooopCore.cc`/`LopCore.cc` with a core-side click-cycle (like Particules' Quality button) + RGB LED, persisting the Overdub index via the `save_state()`/`load_state()` hooks.

**Tech Stack:** C++20. VCV build `make -C vcv -j8` (Rack SDK at `~/Dev/Rack-SDK`). MetaModule build `cmake -S metamodule -B metamodule/build && cmake --build metamodule/build -j8` (arm-none-eabi + `METAMODULE_SDK_DIR`, default `~/Dev/metamodule-plugin-sdk`). Test lanes `tests/run.sh` (g++ + python) and `tests/particules_dsp/run.sh` (Catch2). MM SDK element types in `~/Dev/metamodule-plugin-sdk/core-interface/CoreModules/elements/base_element.hh`.

## Global Constraints

- Commit messages: short, one sentence, ≤15 words. **No `Co-Authored-By` / AI attribution.**
- Working dir: `/Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-controls` (branch `mm-panel-controls`).
- The Task 1 shared-header move is **behavior-preserving** — VCV output stays bit-identical; the proof is the existing test lanes + VCV build staying green (no logic change).
- Pre-release: removing the old alt-params is a clean break; no MM patch migration.
- Do not touch `LoopEngine` DSP, Crossfade, per-head Grid-exclude, Trig mode, or Speed V/Oct. kMidi is out of scope.
- Spec: `docs/superpowers/specs/2026-07-12-metamodule-panel-overdub-grid-design.md`.

---

## Task 1: Move `kOverdubColors` + `applyOverdub` into the Rack-free shared header

**Files:**
- Modify: `src/loooop/LooperModuleDSP.hpp` (add the two symbols)
- Modify: `src/loooop/OverdubControl.hpp` (remove the two symbols; keep the VCV widget + `setOverdubLED`)

**Context:** `OverdubControl.hpp` currently defines `kOverdubColors[5][3]` and `applyOverdub(LoopEngine&, int)`, but it `#include "plugin.hpp"` (Rack), so the native MM cores can't use them. `LooperModuleDSP.hpp` is Rack-free (`#include <cmath>` + `dsp/LoopEngine.hpp` only) and already holds the sibling helpers `overdubWriteMode`/`gridSegments`. `OverdubControl.hpp` already `#include "LooperModuleDSP.hpp"`, so after the move it needs no new include — and because both live in `namespace loooop`, `setOverdubLED`'s unqualified `kOverdubColors` reference keeps resolving.

**Interfaces:**
- Produces (now in `namespace loooop`, `LooperModuleDSP.hpp`): `constexpr float kOverdubColors[5][3]` and `inline void applyOverdub(LoopEngine& engine, int od)` — identical definitions, just relocated.

- [ ] **Step 1: Add the two symbols to `LooperModuleDSP.hpp`**

In `src/loooop/LooperModuleDSP.hpp`, immediately after the `overdubWriteMode(...)` function (ends ~line 78, before `OnePoleSmoother`), add:

```cpp
// Overdub state colors (Layer/Decay/Add/Replace/Lock), shared by VCV's Overdub
// LED bezel and the MetaModule cores' RGB button. Kept here (Rack-free) so both
// hosts index the same table.
static constexpr float kOverdubColors[5][3] = {
    {0.247f, 0.549f, 1.f},      // Layer   - blue   #3f8cff
    {1.f,    0.624f, 0.039f},   // Decay   - amber  #ff9f0a
    {0.188f, 0.820f, 0.345f},   // Add     - green  #30d158
    {1.f,    0.231f, 0.188f},   // Replace - red    #ff3b30
    {0.749f, 0.353f, 0.949f},   // Lock    - purple #bf5af2
};

// Apply the 5-state Overdub index to the engine: 0..3 = write modes
// (Layer/Decay/Add/Replace), 4 = Lock (overdub off, loop untouchable).
inline void applyOverdub(LoopEngine& engine, int od) {
    engine.setOverdub(od != 4);   // 4 = Lock
    if (od >= 0 && od < 4)
        engine.setWriteMode(overdubWriteMode(od));
}
```

- [ ] **Step 2: Remove them from `OverdubControl.hpp`**

In `src/loooop/OverdubControl.hpp`, delete the `kOverdubColors` table (the `static constexpr float kOverdubColors[5][3] = {…};` block) and the `applyOverdub` function. Leave `setOverdubLED` (it uses `kOverdubColors`, now resolved from the shared header via `namespace loooop`), the `OverdubButton` widget, and the includes. Confirm `#include "LooperModuleDSP.hpp"` is present (it is) so the symbols resolve.

- [ ] **Step 3: Verify VCV build + test lanes are green (behavior-preserving proof)**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-controls
make -C vcv -j8
( cd tests && ./run.sh )
```
Expected: VCV `plugin.dylib` builds with no new warnings; `tests/run.sh` exits 0, all suites pass (Loooop's write-mode/overdub behavior unchanged — the move is pure relocation).

- [ ] **Step 4: Commit**

```bash
git add src/loooop/LooperModuleDSP.hpp src/loooop/OverdubControl.hpp
git commit -m "refactor: move overdub color table and applyOverdub into the Rack-free shared header"
```

---

## Task 2: MetaModule panel Overdub button + Grid knob (both native cores)

**Files:**
- Modify: `metamodule/loooop/QlpElements.hh` (new element structs; drop 3 alt-params)
- Modify: `metamodule/loooop/Loooop_info.hh` (element array, enum, size, comment)
- Modify: `metamodule/loooop/Lop_info.hh` (element array, enum, size, comment)
- Modify: `metamodule/loooop/LoooopCore.cc` (wiring + persistence)
- Modify: `metamodule/loooop/LopCore.cc` (wiring + persistence)

**Context:** This is one atomic change — the element arrays and the cores must change together to compile. `SmartCoreProcessor` maps the `Elem` enum to the element array by index, so the array order and the enum order must stay identical. The panel SVGs already carry the button/knob coordinates (below), so positions are hardcoded like the other panel elements (no sync script needed). The Overdub 5-state index is core-owned (`od_`) and persisted via `save_state`/`load_state` (the SDK's `dataToJson` equivalent); Grid persists automatically as a knob param.

**SDK element shapes** (verify brace nesting against `base_element.hh` — `MomentaryButtonRGB : MomentaryButton { NumLights=3 }`, `KnobSnapped : Knob { unsigned num_pos; array<const char*,30> pos_names; }`; mirror the existing `QlpButton`/`QlpKnob` wrappers).

**Interfaces:**
- Consumes: `loooop::applyOverdub`, `loooop::kOverdubColors`, `loooop::gridSegments` (from Task 1 / existing `LooperModuleDSP.hpp`); `MomentaryButton::State_t::PRESSED`; `getState<Elem>()`, `setLED<Elem>(std::array<float,3>)`.
- Produces: no external interface; the `.mmplugin` gains a panel Overdub button + Grid knob.

- [ ] **Step 1: Add element structs, remove old alt-params (`QlpElements.hh`)**

In `metamodule/loooop/QlpElements.hh`, add after `QlpButton` (line ~31):

```cpp
struct QlpOverdubButton : MomentaryButtonRGB {
    constexpr QlpOverdubButton(BaseElement b)
        : MomentaryButtonRGB{{b, "4ms/comp/button_x.png"}, "4ms/comp/button_x.png"} {}
};
struct QlpGridKnob : KnobSnapped {
    constexpr QlpGridKnob(BaseElement b)
        : KnobSnapped{{{{b, "4ms/comp/knob9mm_x.png"}, 0.f}}, 6,
                      {"Off", "4", "8", "16", "32", "64"}} {}
};
```

Delete the `QlpOverdubAlt`, `QlpWriteModeAlt`, and `QlpGridAlt` struct definitions (lines ~32-35, ~52-59, ~60-65) and their comment blocks. Keep `QlpCrossfadeAlt`, `QlpTrigModeAlt`, `QlpVoctAlt`, `QlpExcludeGridAlt`.

- [ ] **Step 2: Swap elements in `Loooop_info.hh`**

In `metamodule/loooop/Loooop_info.hh`:
- Change array size `std::array<Element, 91>` → `std::array<Element, 90>` (line 37).
- Replace line 40 `QlpOverdubAlt{{0.f, 0.f, Center, "Overdub", ""}, 1},` with:
  `QlpOverdubButton{{69.543f, 116.050f, Center, "Overdub", "", 7.f, 7.f}},`
- Replace line 42 `QlpGridAlt{{0.f, 0.f, Center, "Grid", ""}},` with:
  `QlpGridKnob{{122.687f, 116.050f, Center, "Grid", "", 9.f, 9.f}},`
- Delete line 45 `QlpWriteModeAlt{{0.f, 0.f, Center, "Write mode", ""}},` entirely.
- In the `Elem` enum (line 149), change `RecordButton, OverdubSwitch, ClearButton, GridAlt, DryWetKnob, CrossfadeSwitch, WriteModeAlt,` to:
  `RecordButton, OverdubButton, ClearButton, GridKnob, DryWetKnob, CrossfadeSwitch,`
- Update the comment block (lines 33-36): remove "Write mode" and "Overdub … Grid" from the menu-only list (Overdub and Grid are now panel controls; Crossfade, Trig-mode, Speed V/Oct, Grid-exclude remain menu-only).

- [ ] **Step 3: Swap elements in `Lop_info.hh`**

In `metamodule/loooop/Lop_info.hh`:
- Change array size `std::array<Element, 27>` → `std::array<Element, 26>` (line 32).
- Replace line 44 `QlpOverdubAlt{{0.f, 0.f, Center, "Overdub", "", 0.f, 0.f}, 1},` with:
  `QlpOverdubButton{{37.968f, 102.150f, Center, "Overdub", "", 5.f, 5.f}},`
- Delete line 46 `QlpWriteModeAlt{{0.f, 0.f, Center, "Write mode", "", 0.f, 0.f}},` entirely.
- Replace line 47 `QlpGridAlt{{0.f, 0.f, Center, "Grid", "", 0.f, 0.f}},` with:
  `QlpGridKnob{{51.708f, 102.150f, Center, "Grid", "", 9.f, 9.f}},`
- In the `Elem` enum (line 69), change `DryWetKnob, RecordButton, ClearButton, OverdubSwitch, CrossfadeSwitch, WriteModeAlt, GridAlt,` to:
  `DryWetKnob, RecordButton, ClearButton, OverdubButton, CrossfadeSwitch, GridKnob,`
- Update the enum's WriteMode/offset comment (lines 27-32) to drop the "WriteModeAlt menu-only extra" note (it's gone).

**Adaptation note:** confirm the array order and enum order stay index-for-index identical after the edits (Overdub, Crossfade, Grid appear in the same relative order in both the array and enum). If Löp's array order around these three differs from the enum, match them.

- [ ] **Step 4: Wire `LoooopCore.cc`**

In `metamodule/loooop/LoooopCore.cc`:

(a) Add includes near the top (after line 9): `#include <charconv>` and `#include <string>`.

(b) In `update()`, replace lines 37, 39, 40. Delete line 37 (`engine_.setOverdub(getState<OverdubSwitch>() == 1);`) and line 39 (`engine_.setWriteMode(loooop::overdubWriteMode((int)getState<WriteModeAlt>()));`). Keep line 38 (Crossfade). Change line 40 to read the Grid **knob**:

```cpp
        engine_.setCrossfade(getState<CrossfadeSwitch>() == 0);   // index 0 = On (see QlpCrossfadeAlt)

        // Overdub: momentary button cycles the 5-state control
        // (Layer/Decay/Add/Replace/Lock), matching VCV; index persists via save_state.
        bool odPressed = getState<OverdubButton>() == MomentaryButton::State_t::PRESSED;
        if (odPressed && !odPrev_) od_ = (od_ + 1) % 5;
        odPrev_ = odPressed;
        loooop::applyOverdub(engine_, od_);

        engine_.setGrid(loooop::gridSegments((int)std::lround(getState<GridKnob>() * 5.f)));
```

(c) After the existing `setLED<RecordButton>(...)` line (line 89), add the Overdub RGB LED:

```cpp
        setLED<OverdubButton>(std::array<float, 3>{
            loooop::kOverdubColors[od_][0], loooop::kOverdubColors[od_][1],
            loooop::kOverdubColors[od_][2]});
```

(d) Add the persistence overrides as public members (e.g. right after `set_samplerate`):

```cpp
    std::string save_state() override { return std::to_string(od_); }
    void load_state(std::string_view state_data) override {
        int v = 0;
        auto [ptr, ec] = std::from_chars(state_data.data(),
                                         state_data.data() + state_data.size(), v);
        od_ = (ec == std::errc{} && v >= 0 && v <= 4) ? v : 0;
    }
```

(e) Add the members (in the private block near `recPrev_`, line ~198):

```cpp
    int od_ = 0;          // 0 = Layer (matches VCV Overdub default)
    bool odPrev_ = false;
```

- [ ] **Step 5: Wire `LopCore.cc` (mirror Step 4)**

Apply the identical changes to `metamodule/loooop/LopCore.cc`: add `<charconv>`/`<string>` includes; in its `update()` remove the `getState<OverdubSwitch>()` and `getState<WriteModeAlt>()` reads, add the same Overdub button-cycle block + `applyOverdub`, change the Grid read to `gridSegments((int)std::lround(getState<GridKnob>() * 5.f))`; add the `setLED<OverdubButton>({…})` after its `setLED<RecordButton>`; add the same `save_state`/`load_state` overrides and `od_`/`odPrev_` members. (LopCore.cc's reads are around lines 34-37; the LED around line 94.)

- [ ] **Step 6: Build the MetaModule plugin**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-controls
cmake -S metamodule -B metamodule/build          # first configure (skip if metamodule/build already configured)
cmake --build metamodule/build -j8
```
Expected: compiles clean, links, and produces `metamodule/metamodule-plugins/RobotBoy.mmplugin` with "All symbols found" (the SDK's post-link check). If a brace-init error appears on `QlpOverdubButton`/`QlpGridKnob`, adjust the nesting to match `base_element.hh`'s `MomentaryButton`/`KnobSnapped` aggregates (mirror `QlpButton`/`QlpKnob`).

- [ ] **Step 7: Commit**

```bash
git add metamodule/loooop/QlpElements.hh metamodule/loooop/Loooop_info.hh metamodule/loooop/Lop_info.hh metamodule/loooop/LoooopCore.cc metamodule/loooop/LopCore.cc
git commit -m "feat: MetaModule Loooop/Löp get panel Overdub button and Grid knob"
```

---

## Task 3: Faceplate confirmation + full cross-lane verification

**Files:**
- Verify only: `metamodule/assets/Loooop/Loooop.png`, `metamodule/assets/Loooop/Lop.png`

**Context:** The panel SVGs already render the Overdub button and Grid knob (unchanged on this branch — the panel rework predates it), and the faceplate PNGs were regenerated during that rework, so no new art is expected. This task confirms the whole plugin still builds/tests across all lanes and that the `.mmplugin` packages the faceplates.

- [ ] **Step 1: Confirm faceplates already show the controls**

The SVGs (`res/Loooop.svg`, `res/Lop.svg`) already contain the `OVERDUB_PARAM`/`GRID_PARAM` components; the assets were built from them. Confirm the PNGs exist and are non-empty:
```bash
ls -la metamodule/assets/Loooop/Loooop.png metamodule/assets/Loooop/Lop.png
```
If (and only if) they are missing or predate the panel rework and lack the controls, regenerate them from the SVGs via the project's SVG→PNG step (see the `vcv-to-metamodule` skill) and re-run the MM build. Otherwise, no action — they are current (art was not changed on this branch).

- [ ] **Step 2: Full verification from a clean state**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/mm-panel-controls
( cd tests && ./run.sh )                 # g++ lane + python guards — exit 0
./tests/particules_dsp/run.sh            # Catch2 lane — 100% pass
make -C vcv -j8                          # VCV dylib builds
cmake --build metamodule/build -j8       # RobotBoy.mmplugin, all symbols found
```
Expected: all four green. This confirms the shared-header move (Task 1) didn't regress Particules/MF-20, the VCV plugin still builds, and the MM plugin builds with the new panel controls.

- [ ] **Step 3: Record the GUI user check (not agent-testable)**

MM-core GUI behavior cannot be verified in the offline lanes. Note in the task report that the following must be checked by the user in the MetaModule simulator/device:
- Loooop and Löp show a panel Overdub button whose clicks cycle Layer→Decay→Add→Replace→Lock with the matching RGB colors (blue/amber/green/red/purple).
- The Grid knob snaps through Off/4/8/16/32/64 and quantizes the loop audibly.
- Both survive patch save/load (Overdub index restored; Grid restored).
- The old menu entries (Overdub On/Off, Write mode, Grid) are gone.

- [ ] **Step 4: Commit (only if faceplates were regenerated in Step 1)**

```bash
git add metamodule/assets/Loooop/Loooop.png metamodule/assets/Loooop/Lop.png
git commit -m "assets: regenerate MetaModule faceplates with Overdub button and Grid knob"
```
(If no asset change was needed, skip this commit.)

---

## Completion

1. Final whole-branch review per superpowers:requesting-code-review (scope: this branch's commits).
2. Do NOT merge — the user decides, and it gates on the GUI user check above.
3. Report the pending GUI user check to the user.
