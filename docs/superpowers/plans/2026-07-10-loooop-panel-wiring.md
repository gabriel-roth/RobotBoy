# Loooop Panel Wiring (VCV) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the revised Loooop panel's two new controls — a five-state Overdub color button and a four-position Grid snap knob — into the VCV module.

**Architecture:** All changes live in `src/loooop/Loooop.cpp`: the two old menu params (`OVERDUB_PARAM` on/off + `WRITE_MODE_PARAM`) collapse into one five-state `OVERDUB_PARAM` (Layer/Decay/Add/Replace/Lock, default Layer); a custom NanoVG-drawn `OverdubButton` (subclass of `app::Switch`) cycles it; `GRID_PARAM` gets a stock `RoundBlackSnapKnob`. Menu entries for Overdub/Write mode/Grid are removed.

**Tech Stack:** VCV Rack SDK (Rack-SDK at `~/Dev/Rack-SDK`), C++20, NanoVG. Build via `make -B` in `vcv/`.

## Global Constraints

- Work in the `loooop-track` worktree at `/Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track` — never on `main` (spec + repo convention).
- Do NOT touch `src/loooop/Lop.cpp` — Löp keeps its own Overdub/Write mode menu params.
- Do NOT touch `src/loooop/dsp/LoopEngine.*` — engine is unchanged.
- Do NOT touch `metamodule/` — MetaModule wiring is a separate follow-up.
- Patch compatibility is intentionally broken (approved 2026-07-10); no migration shims.
- UI state order (fixed): 0 Layer, 1 Decay, 2 Add, 3 Replace, 4 Lock. Default 0 (Layer).
- State colors (fixed): Layer `#3f8cff`, Decay `#ff9f0a`, Add `#30d158`, Replace `#ff3b30`, Lock `#bf5af2`.
- No agent-driven GUI/simulator tests: machine verification is compile + install; GUI behavior goes on a user-run checklist at the end.
- Commit messages: short, one sentence, no AI attribution.

---

### Task 1: Merge the params and clean up the menu

**Files:**
- Modify: `src/loooop/Loooop.cpp` (enum ~line 20, configSwitch block ~lines 72-78, process() ~lines 107-111, appendContextMenu ~lines 296-313)

**Interfaces:**
- Produces: `Loooop::OVERDUB_PARAM` is a 0–4 switch (0 Layer … 4 Lock, default 0); `WRITE_MODE_PARAM` no longer exists. Task 2's widget attaches to `OVERDUB_PARAM`.

- [ ] **Step 1: Shrink the enum**

In the `ParamId` enum (~line 20), delete `WRITE_MODE_PARAM`:

```cpp
                   DRYWET_PARAM, RECORD_PARAM, CLEAR_PARAM, OVERDUB_PARAM, CROSSFADE_PARAM, GRID_PARAM,
                   PARAMS_LEN };
```

- [ ] **Step 2: Replace the two configSwitch calls**

Replace (~lines 72-77):

```cpp
        configSwitch(OVERDUB_PARAM, 0.f, 1.f, 1.f, "Overdub", {"Off", "On"});
        // Value 0 = On (default): kept inverted to match the MetaModule alt-param,
        // whose loader zero-inits unset params, so 0 must mean crossfade-on.
        configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade", {"On", "Off"});
        configSwitch(WRITE_MODE_PARAM, 0.f, 3.f, 0.f, "Write mode",
            {"Add", "Replace", "Layer", "Decay"});
```

with:

```cpp
        configSwitch(OVERDUB_PARAM, 0.f, 4.f, 0.f, "Overdub",
            {"Layer", "Decay", "Add", "Replace", "Lock"});
        // Value 0 = On (default): kept inverted to match the MetaModule alt-param,
        // whose loader zero-inits unset params, so 0 must mean crossfade-on.
        configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade", {"On", "Off"});
```

- [ ] **Step 3: Replace the process() reads**

Replace (~lines 107-111):

```cpp
        engine.setOverdub(params[OVERDUB_PARAM].getValue() > 0.5f);
        engine.setCrossfade(params[CROSSFADE_PARAM].getValue() < 0.5f);   // 0 = On
        engine.setWriteMode(static_cast<LoopEngine::WriteMode>(
            (int)std::round(params[WRITE_MODE_PARAM].getValue())));
```

with:

```cpp
        // Overdub is one 5-state control: four write modes + Lock (= overdub
        // off, loop untouchable). While Locked the last write mode stays set;
        // the engine ignores it with overdub off.
        static constexpr LoopEngine::WriteMode kOverdubModes[4] = {
            LoopEngine::WriteMode::Layer, LoopEngine::WriteMode::Decay,
            LoopEngine::WriteMode::Add,   LoopEngine::WriteMode::Replace};
        int od = (int)std::round(params[OVERDUB_PARAM].getValue());
        engine.setOverdub(od != 4);   // 4 = Lock
        if (od >= 0 && od < 4)
            engine.setWriteMode(kOverdubModes[od]);
        engine.setCrossfade(params[CROSSFADE_PARAM].getValue() < 0.5f);   // 0 = On
```

- [ ] **Step 4: Remove the three menu items**

In `appendContextMenu` (~lines 300-313), delete the `createBoolMenuItem("Overdub", ...)` call, the `kWriteModes` vector + its `createIndexSubmenuItem("Write mode", ...)` call, and the `kGridLabels` vector + its `createIndexSubmenuItem("Grid", ...)` call. Keep "Crossfade loop seams" and the per-head submenus.

- [ ] **Step 5: Build to verify**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/vcv && make -j8 2>&1 | tail -3`
Expected: links `plugin.dylib` with no errors (any remaining `WRITE_MODE_PARAM` reference is a compile error — that's the test).

- [ ] **Step 6: Commit**

```bash
git add src/loooop/Loooop.cpp
git commit -m "feat: merge Loooop overdub and write mode into one 5-state param"
```

---

### Task 2: OverdubButton widget and Grid knob

**Files:**
- Modify: `src/loooop/Loooop.cpp` (new widget struct above `struct LoooopWidget` ~line 188; two `addParam` calls in the bottom-row block ~line 287)

**Interfaces:**
- Consumes: `Loooop::OVERDUB_PARAM` (0–4, from Task 1), `Loooop::GRID_PARAM` (0–3, existing).

- [ ] **Step 1: Add the OverdubButton widget**

Insert immediately above `struct LoooopWidget : ModuleWidget` (~line 188):

```cpp
// Five-state overdub button: click cycles Layer/Decay/Add/Replace/Lock
// (app::Switch wraps at max), drawn as a bezel whose center lights in the
// state's color. Colors echo the panel's head-color family.
struct OverdubButton : app::Switch {
    static constexpr float kDiamMM = 9.f;
    OverdubButton() {
        momentary = false;
        box.size = mm2px(Vec(kDiamMM, kDiamMM));
    }

    void draw(const DrawArgs& args) override {
        static const NVGcolor kStateColors[5] = {
            nvgRGB(0x3f, 0x8c, 0xff),   // Layer  - blue
            nvgRGB(0xff, 0x9f, 0x0a),   // Decay  - amber
            nvgRGB(0x30, 0xd1, 0x58),   // Add    - green
            nvgRGB(0xff, 0x3b, 0x30),   // Replace - red
            nvgRGB(0xbf, 0x5a, 0xf2),   // Lock   - purple
        };
        ParamQuantity* pq = getParamQuantity();
        int state = pq ? math::clamp((int)std::round(pq->getValue()), 0, 4) : 0;

        Vec c = box.size.div(2.f);
        float r = c.x;

        // Bezel: dark face with a darker rim, matching the stock button look.
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, r);
        nvgFillColor(args.vg, nvgRGB(0x1a, 0x1a, 0x1a));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 1.f);
        nvgStrokeColor(args.vg, nvgRGB(0x00, 0x00, 0x00));
        nvgStroke(args.vg);

        // State light: filled center circle in the state color.
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, r * 0.55f);
        nvgFillColor(args.vg, kStateColors[state]);
        nvgFill(args.vg);

        Switch::draw(args);
    }
};
```

- [ ] **Step 2: Add the two panel controls**

In the bottom-row block of `LoooopWidget`'s constructor (after the `RECORD_TRIG_INPUT` line, ~line 289), add:

```cpp
        addParam(createParamCentered<OverdubButton>(mm2px(Vec(68.877, 116.05)), module, Loooop::OVERDUB_PARAM));
        addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(121.63, 116.05)), module, Loooop::GRID_PARAM));
```

(Positions are the SVG circle centers: `OVERDUB_PARAM` cx 68.87672851562499, `GRID_PARAM` cx 121.62738281249999 — the sync script rounds with `%.5g`.)

- [ ] **Step 3: Build to verify**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/vcv && make -j8 2>&1 | tail -3`
Expected: links `plugin.dylib` with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/loooop/Loooop.cpp
git commit -m "feat: panel Overdub 5-state button and Grid snap knob on Loooop"
```

---

### Task 3: Install and hand off GUI checks

**Files:**
- None modified; build artifacts installed to `$HOME/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy/`.

**Interfaces:**
- Consumes: `vcv/plugin.dylib` from Task 2's build.

- [ ] **Step 1: Full rebuild and install**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/vcv
make -B -j8 2>&1 | tail -3
INSTALL_DIR="$HOME/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy"
cp plugin.dylib "$INSTALL_DIR/plugin.dylib"
cp plugin.json "$INSTALL_DIR/plugin.json"
cp -r res "$INSTALL_DIR/"
```

Expected: clean link; files land in the install dir.

- [ ] **Step 2: Present the user-run GUI checklist**

Remind the user to restart VCV Rack, then verify:
1. Overdub button cycles Layer → Decay → Add → Replace → Lock → Layer, colored blue/amber/green/red/purple in that order, defaulting to blue (Layer).
2. Right-click on the button shows the five labeled values.
3. With a loop recorded and Lock selected, pressing Record does nothing; switching off Lock re-enables overdubbing in the selected mode.
4. Grid knob snaps through Off/4/8/16; display grid bars follow (with a frozen loop).
5. Context menu no longer shows Overdub / Write mode / Grid; Crossfade and per-head submenus remain.
6. Record/Clear/D-W and all jacks sit centered on their panel art (positions were re-synced this session).
