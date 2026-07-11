# Loooop Context-Menu Rework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Commands-first Loooop context menu with color-named playheads and a new per-head "Exclude from Grid" setting, wired through the engine and both hosts.

**Architecture:** One new per-head engine flag (`gridExclude`) gating the single grid branch in `windowBounds`; a per-head menu-only param on each host mirrors it every block (same lifecycle as trig mode / V-Oct). The VCV context menu inverts to command → playhead, with playheads named Red/Green/Blue/Yellow (matching `LoopWaveformRenderer::HEAD_COLORS`).

**Tech Stack:** C++ (Rack SDK for VCV, MetaModule SmartCoreProcessor), existing `tests/run.sh` harness.

**Spec:** `docs/superpowers/specs/2026-07-10-loooop-menu-rework-design.md`

## Global Constraints

- Löp is untouched.
- MM element short-names keep the numeric convention ("Grid N exclude"); the color-name sweep is VCV-only.
- New MM alt param must have index 0 = Off (loader zero-inits unset alt-params).
- Param-id shifts on both hosts are sanctioned (compat already broken this cycle).
- Commit messages: short, one sentence, no AI attribution.
- Work on branch `loooop-track` in `.worktrees/loooop-track`.

---

### Task 1: Engine per-head grid exclusion

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (PlayHead struct \~line 93; setter block \~line 49)
- Modify: `src/loooop/dsp/LoopEngine.cpp` (windowBounds \~line 238; setter near setSize/setLevel \~line 135)
- Test: `tests/loooop/test_loop_engine.cpp` (append after `test_grid_off_matches_ungridded`, register in `main`)

**Interfaces:**
- Produces: `void LoopEngine::setGridExclude(int head, bool exclude)` — bounds-checked per-head setter, used by both hosts in Tasks 2–3.

- [ ] **Step 1: Write the failing test**

Append after `test_grid_off_matches_ungridded()`:

```cpp
static void test_grid_exclude_head() {
    // Excluded head matches an ungridded engine sample-for-sample.
    LoopEngine a; record_ramp(a, 16);
    LoopEngine b; record_ramp(b, 16);
    b.setGrid(4); b.setGridExclude(0, true);
    a.setSize(0, 0.3f); a.setPosition(0, 0.37f);
    b.setSize(0, 0.3f); b.setPosition(0, 0.37f);
    bool same = true;
    for (int i = 0; i < 40; ++i) same = same && near(a.process(0.f), b.process(0.f));
    check(same, "grid_excl: excluded head matches ungridded engine");
    check(b.displaySnapshot().grid == 4, "grid_excl: grid still reported for display");

    // Other heads still snap while head 0 is excluded (per-head gate).
    LoopEngine c; record_ramp(c, 16);
    c.setGrid(4); c.setGridExclude(0, true);
    c.setSize(1, 0.3f); c.setPosition(1, 0.37f);   // would snap: 1 segment at [4,8)
    c.process(0.f);
    const auto s = c.displaySnapshot();
    check(near(s.winStart01[1], 0.25f) && near(s.winEnd01[1], 0.5f),
          "grid_excl: non-excluded head still snaps");

    // Re-including restores snapping.
    c.setGridExclude(0, false);
    c.setSize(0, 0.3f); c.setPosition(0, 0.37f);
    c.process(0.f);
    const auto s2 = c.displaySnapshot();
    check(near(s2.winStart01[0], 0.25f) && near(s2.winEnd01[0], 0.5f),
          "grid_excl: re-included head snaps again");
}
```

Register in `main` next to the other grid tests: `test_grid_exclude_head();`

- [ ] **Step 2: Run to verify it fails**

Run: `cd .worktrees/loooop-track/tests && ./run.sh`
Expected: compile error — `no member named 'setGridExclude' in 'LoopEngine'`.

- [ ] **Step 3: Implement**

`LoopEngine.hpp` — PlayHead gains a field:

```cpp
        float osRamp = 1.f;   // retrigger ramp-in gain (Q2)
        float levelSm = 1.f;  // one-pole-smoothed level (Q3), matches the 1.0 level default
        bool gridExclude = false;   // this head ignores Grid quantization
```

`LoopEngine.hpp` — after `void setGrid(int segments);`:

```cpp
    void setGridExclude(int head, bool exclude);   // head ignores the grid entirely
```

`LoopEngine.cpp` — next to the one-line setters (\~line 136):

```cpp
void LoopEngine::setGridExclude(int head, bool exclude) { if (head >= 0 && head < numHeads_) heads_[head].gridExclude = exclude; }
```

`LoopEngine.cpp` — the `windowBounds` gate:

```cpp
    if (grid_ >= 2 && !h.gridExclude) {
```

- [ ] **Step 4: Run tests**

Run: `cd .worktrees/loooop-track/tests && ./run.sh`
Expected: exit 0, new checks print `ok:`.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: per-head grid exclusion in LoopEngine"
```

---

### Task 2: VCV params, color naming sweep, commands-first menu

**Files:**
- Modify: `src/loooop/Loooop.cpp` (enums \~line 19; ctor config loop \~line 48; process per-head loop \~line 135; `appendContextMenu` \~line 338)

**Interfaces:**
- Consumes: `LoopEngine::setGridExclude(int, bool)` from Task 1.
- Produces: `EXCLUDE_GRID1_PARAM` in the per-head block, `HEAD_PARAMS == 9`, `kHeadNames` table — Task 3 mirrors the same element order on MM.

- [ ] **Step 1: Param enum + stride**

In `ParamId`, append `EXCLUDE_GRID1_PARAM` (etc.) to each head's block after `SPEED_VOCT`:

```cpp
    enum ParamId { RECORD_PARAM, OVERDUB_PARAM, CLEAR_PARAM, GRID_PARAM, DRYWET_PARAM, CROSSFADE_PARAM,
                   SIZE1_PARAM, POSITION1_PARAM, SPEED1_PARAM, JITTER1_PARAM, PAN1_PARAM, LEVEL1_PARAM, TRIG_MODE1_PARAM, SPEED_VOCT1_PARAM, EXCLUDE_GRID1_PARAM,
                   SIZE2_PARAM, POSITION2_PARAM, SPEED2_PARAM, JITTER2_PARAM, PAN2_PARAM, LEVEL2_PARAM, TRIG_MODE2_PARAM, SPEED_VOCT2_PARAM, EXCLUDE_GRID2_PARAM,
                   SIZE3_PARAM, POSITION3_PARAM, SPEED3_PARAM, JITTER3_PARAM, PAN3_PARAM, LEVEL3_PARAM, TRIG_MODE3_PARAM, SPEED_VOCT3_PARAM, EXCLUDE_GRID3_PARAM,
                   SIZE4_PARAM, POSITION4_PARAM, SPEED4_PARAM, JITTER4_PARAM, PAN4_PARAM, LEVEL4_PARAM, TRIG_MODE4_PARAM, SPEED_VOCT4_PARAM, EXCLUDE_GRID4_PARAM,
                   PARAMS_LEN };
```

and `static constexpr int HEAD_PARAMS = 9;` (update its trailing comment to
`Size,Pos,Speed,Jitter,Pan,Level,TrigMode,SpeedVoct,ExcludeGrid`).

- [ ] **Step 2: Color names + config sweep**

Add above the ctor (file scope, near the other tables):

```cpp
// Playhead display names follow the head colors on the panel/display
// (LoopWaveformRenderer::HEAD_COLORS): H1 red, H2 green, H3 blue, H4 yellow.
static const std::string kHeadNames[LoopEngine::NUM_HEADS] = {
    "Red playhead", "Green playhead", "Blue playhead", "Yellow playhead"};
```

In the ctor loop, replace `const std::string n = std::to_string(h + 1);` with
`const std::string& n = kHeadNames[h];` and every `"Head " + n + " size"`
pattern with `n + " size"` (same for position, speed, jitter, pan, level,
trigger, speed CV V/Oct, all CV inputs, trigger/jump inputs, and the
left/right outputs — e.g. `configOutput(HEAD1_L_OUTPUT + 2 * h, n + " left");`).
Add the new switch after the SPEED_VOCT one:

```cpp
            configSwitch(EXCLUDE_GRID1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f,
                n + " exclude from Grid", {"Off", "On"});
```

- [ ] **Step 3: process() wiring**

In the per-head loop, after the `engine.setJitter(...)` call:

```cpp
            engine.setGridExclude(h,
                params[EXCLUDE_GRID1_PARAM + HEAD_PARAMS * h].getValue() > 0.5f);
```

- [ ] **Step 4: Menu rework**

Replace the per-head submenu loop in `appendContextMenu` (keep the
separator + Crossfade item) with:

```cpp
        static const std::vector<std::string> kTrigModes = {"Loop start", "One-shot"};
        menu->addChild(createSubmenuItem("Trigger", "", [m](Menu* sub) {
            for (int h = 0; h < LoopEngine::NUM_HEADS; ++h)
                sub->addChild(createIndexSubmenuItem(kHeadNames[h], kTrigModes,
                    [m, h] { return (int)std::round(
                        m->params[Loooop::TRIG_MODE1_PARAM + Loooop::HEAD_PARAMS * h].getValue()); },
                    [m, h](int v) {
                        m->paramQuantities[Loooop::TRIG_MODE1_PARAM + Loooop::HEAD_PARAMS * h]->setValue((float)v); }));
        }));
        menu->addChild(createSubmenuItem("Speed CV is V/Oct", "", [m](Menu* sub) {
            for (int h = 0; h < LoopEngine::NUM_HEADS; ++h)
                sub->addChild(createBoolMenuItem(kHeadNames[h], "",
                    [m, h] { return m->params[Loooop::SPEED_VOCT1_PARAM + Loooop::HEAD_PARAMS * h].getValue() > 0.5f; },
                    [m, h](bool v) {
                        m->paramQuantities[Loooop::SPEED_VOCT1_PARAM + Loooop::HEAD_PARAMS * h]->setValue(v ? 1.f : 0.f); }));
        }));
        menu->addChild(createSubmenuItem("Exclude from Grid", "", [m](Menu* sub) {
            for (int h = 0; h < LoopEngine::NUM_HEADS; ++h)
                sub->addChild(createBoolMenuItem(kHeadNames[h], "",
                    [m, h] { return m->params[Loooop::EXCLUDE_GRID1_PARAM + Loooop::HEAD_PARAMS * h].getValue() > 0.5f; },
                    [m, h](bool v) {
                        m->paramQuantities[Loooop::EXCLUDE_GRID1_PARAM + Loooop::HEAD_PARAMS * h]->setValue(v ? 1.f : 0.f); }));
        }));
```

Also update the enum header comment: the VCV/MM offset note from the
globals-first commit still holds; extend the per-head stride description to
include ExcludeGrid.

- [ ] **Step 5: Build VCV**

Run: `cd .worktrees/loooop-track/vcv && make -j8`
Expected: clean build (only pre-existing Rack SDK deprecation warnings).

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: commands-first Loooop menu, color playhead names, Exclude from Grid"
```

---

### Task 3: MetaModule wiring

**Files:**
- Modify: `metamodule/loooop/QlpElements.hh` (after `QlpGridAlt`, \~line 64)
- Modify: `metamodule/loooop/Loooop_info.hh` (Elements array + Elem enum)
- Modify: `metamodule/loooop/LoooopCore.cc` (updateHead template \~line 158 and 4 call sites \~line 58)

**Interfaces:**
- Consumes: `LoopEngine::setGridExclude(int, bool)` (Task 1); element order mirroring Task 2's VCV per-head block.

- [ ] **Step 1: Element type**

`QlpElements.hh`, after `QlpGridAlt`:

```cpp
// Index 0 = Off so patches saved before this param (loader zero-inits unset
// alt-params) keep every head on the grid.
struct QlpExcludeGridAlt : AltParamChoiceLabeled {
    constexpr QlpExcludeGridAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 2, 0}, {"Off", "On"}} {}
};
```

- [ ] **Step 2: Info header**

`Loooop_info.hh`: array size `87` → `91`. In each head's param group, after
that head's `QlpVoctAlt` line:

```cpp
        QlpExcludeGridAlt{{0.f, 0.f, Center, "Grid 1 exclude", ""}},
```

(2/3/4 for the other heads). In `Elem`, each per-head param row gains
`ExcludeGrid1Alt` (etc.) after `SpeedVoct1Alt`. Add ExcludeGrid to the
menu-only list in the header comment. `bypass_routes` unchanged (jack
lists untouched).

- [ ] **Step 3: Core wiring**

`LoooopCore.cc`: add `Info::Elem XG` to the `updateHead` template parameter
list (after `VO`), add to the body next to the V-Oct read:

```cpp
        engine_.setGridExclude(h, getState<XG>() == 1);
```

and pass `ExcludeGrid1Alt`..`ExcludeGrid4Alt` at the four call sites
(after `SpeedVoct1Alt` etc.).

- [ ] **Step 4: Build MM + full lanes**

Run: `cd .worktrees/loooop-track && cmake --build metamodule/build -j8`
Expected: "All symbols found!", `.mmplugin` created.
Run: `cd tests && ./run.sh`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: MetaModule per-head Grid exclude alt-params"
```

---

## Self-review notes

- Spec coverage: engine flag (T1), VCV menu/naming/param (T2), MM alts (T3);
  manual update is deliberately outside this plan (user asked for it as a
  follow-on step after execution).
- Types consistent: `setGridExclude(int, bool)` everywhere; `HEAD_PARAMS 9`
  only in T2 (VCV); MM has no stride constant (explicit element lists).
- Snap expectations in T1's test re-derived from the existing
  `test_grid_position_snaps_to_boundaries` math (size 0.3 → 1 segment;
  centre 0.37·16 = 5.92, start 3.92 → k = 1 → [4,8) = [0.25, 0.5)).
