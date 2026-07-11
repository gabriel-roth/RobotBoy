# Löp Overdub button / Grid alignment / dim value labels / dark screws — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Löp's panel Overdub becomes Loooop's five-state LED mode button (absorbing Write mode), the Grid knob center-aligns with the Size/D-W knobs, value-ring labels render dimmed grey on both modules, and both modules show dark screws in VCV Rack.

**Architecture:** One vcv-panel-gen change (dimmed value-label color, new `value_color` theme key), one panel-spec + regeneration pass, one C++ pass that moves Loooop's Overdub control into a shared header and wires Löp to it, one screw-widget removal, then docs. Spec: `docs/superpowers/specs/2026-07-11-lop-overdub-grid-polish-design.md`.

**Tech Stack:** Python 3 (vcv-panel-gen, pytest), C++20 (VCV Rack plugin), Rack-SDK make build.

## Global Constraints

- All RobotBoy work happens in the worktree `/Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track` on branch `loooop-track`. Never touch the primary checkout or `main`.
- Generator work happens in `/Users/gabrielroth/Dev/vcv-panel-gen` (its own git repo; commit there separately).
- Python: always `~/Dev/python-scripts/.venv/bin/python` (has fonttools, PyYAML, pytest).
- Commit messages: short (≤15 words), no "Co-Authored-By"/AI attribution lines.
- Do not run GUI/simulator tests; GUI verification goes on the user checklist.
- `metamodule/loooop/Lop_info.hh` is HAND-MAINTAINED: element names/order are a contract with LopCore.cc. Only its comments change in this plan; positions are written only by `sync_info_positions.py`.
- MetaModule behavior does NOT change in this plan (MM keeps the 2-choice Overdub + WriteMode menu params; see spec §1).

---

### Task 1: vcv-panel-gen — dimmed value-ring labels

**Files:**
- Modify: `/Users/gabrielroth/Dev/vcv-panel-gen/constants.py` (add one constant)
- Modify: `/Users/gabrielroth/Dev/vcv-panel-gen/theme.py` (Theme field, merge, validation, `resolve_value_color`)
- Modify: `/Users/gabrielroth/Dev/vcv-panel-gen/svgdoc.py` (values layer fill)
- Test: `/Users/gabrielroth/Dev/vcv-panel-gen/tests/test_theme.py`, `tests/test_svgdoc.py`

**Interfaces:**
- Produces: `theme.resolve_value_color(theme) -> "#rrggbb"` — explicit `theme.value_color` if set, else resolved text color blended toward `theme.background` at `constants.VALUE_TEXT_MIX = 0.55` (55% text), rounded per channel. `svgdoc.build_svg` fills every path in the `values` layer with this color. Task 2 relies on regenerated panels having `fill="#a8a8a8"` ring labels (given background `#3d3d3d`, text `#ffffff`).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_theme.py`:

```python
def test_from_mapping_value_color_and_rejects_bad():
    t = T.theme_from_mapping({"value_color": "#a8a8a8"}, "src")
    assert t.value_color == "#a8a8a8"
    with pytest.raises(T.ThemeError, match="value_color"):
        T.theme_from_mapping({"value_color": "grey"}, "src")


def test_resolve_value_color_blends_text_toward_background():
    # 0.55 * 255 + 0.45 * 0x3d(61) = 167.7 -> 168 = 0xa8 per channel
    t = T.Theme(background="#3d3d3d", text_color="#ffffff")
    assert T.resolve_value_color(t) == "#a8a8a8"


def test_resolve_value_color_explicit_override_wins():
    t = T.Theme(background="#3d3d3d", text_color="#ffffff", value_color="#808080")
    assert T.resolve_value_color(t) == "#808080"


def test_resolve_value_color_default_theme():
    # Auto text on the default light background resolves black -> dim grey:
    # 0.45 * 0xe8(232) = 104.4 -> 104 = 0x68 per channel.
    assert T.resolve_value_color(T.DEFAULT_THEME) == "#686868"


def test_merge_inherits_and_overrides_value_color():
    base = T.Theme(value_color="#aaaaaa")
    assert T.merge(base, T.Theme()).value_color == "#aaaaaa"
    assert T.merge(base, T.Theme(value_color="#bbbbbb")).value_color == "#bbbbbb"
```

Append to `tests/test_svgdoc.py`:

```python
def test_values_layer_uses_dimmed_color():
    spec = PanelSpec("M", "M", 20, [Row(type="knobs", items=[
        Item("Mode", "MODE", values=["A", "B", "C"])])])
    th = T.Theme(background="#3d3d3d", text_color="#ffffff")
    svg = build_svg(spec, layout_panel(spec, TR, th), TR, theme=th)
    m = re.search(r'<g inkscape:label="values"[^>]*>', svg)
    values_part = svg.split(m.group(0))[1].split("</g>")[0]
    # All three ring labels dimmed; control labels elsewhere keep full white.
    assert values_part.count('fill="#a8a8a8"') == 3
    assert 'fill="#ffffff"' in svg
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd ~/Dev/vcv-panel-gen && ~/Dev/python-scripts/.venv/bin/python -m pytest tests/test_theme.py tests/test_svgdoc.py -q`
Expected: FAIL — `Theme.__init__() got an unexpected keyword argument 'value_color'` / `AttributeError: module 'theme' has no attribute 'resolve_value_color'`.

- [ ] **Step 3: Implement**

`constants.py` — next to the other VALUE_ constants (near `VALUE_FONT_MM`):

```python
VALUE_TEXT_MIX = 0.55  # value-ring label color: this share of the text color, rest background
```

`theme.py`:
1. Import `VALUE_TEXT_MIX` in the existing `from constants import (...)` line.
2. `Theme` dataclass — add field: `value_color: str | None = None` (leave `DEFAULT_THEME` without it: None means "blend").
3. `merge()` — add `value_color=pick("value_color")` to the returned `Theme(...)`.
4. `_ALLOWED_FIELDS` — add `"value_color"`.
5. `theme_from_mapping()` — next to the `text_color` line add:
   ```python
   vc = _validate_hex(data["value_color"], "value_color", source) if "value_color" in data else None
   ```
   and pass `value_color=vc` in the returned `Theme(...)`.
6. After `resolve_text_color` add:
   ```python
   def resolve_value_color(theme):
       """Value-ring labels draw quieter than control labels: an explicit
       theme.value_color wins; otherwise the resolved text color blended
       toward the background (VALUE_TEXT_MIX of the text color remains)."""
       if theme.value_color is not None:
           return theme.value_color
       t = _hex_to_rgb(resolve_text_color(theme))
       b = _hex_to_rgb(theme.background)
       return "#" + "".join(
           f"{round(tc * VALUE_TEXT_MIX + bc * (1.0 - VALUE_TEXT_MIX)):02x}"
           for tc, bc in zip(t, b))
   ```

`svgdoc.py`:
1. Extend the theme import: `from theme import DEFAULT_THEME, resolve_text_color, resolve_screw_color, resolve_value_color`.
2. In `build_svg`, next to `text_color = resolve_text_color(theme)` add `value_color = resolve_value_color(theme)`.
3. In the values-layer loop change `out.append(_path(d, fill=text_color))` to `out.append(_path(d, fill=value_color))` (ONLY the one inside the `values` layer block).

- [ ] **Step 4: Run the full generator suite**

Run: `cd ~/Dev/vcv-panel-gen && ~/Dev/python-scripts/.venv/bin/python -m pytest tests/ -q`
Expected: all pass (296 before this task; 302 after).

- [ ] **Step 5: Commit (in vcv-panel-gen)**

```bash
cd ~/Dev/vcv-panel-gen && git add -A && git commit -m "value-ring labels draw dimmed; optional value_color theme key"
```

---

### Task 2: Löp panel spec — OverdubButton, center-aligned Grid; regenerate both panels

**Files:**
- Modify: `panel-specs/lop.yaml` (worktree root; all Task 2–5 paths relative to `/Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track`)
- Regenerate: `res/Lop.svg`, `res/Loooop.svg`
- Regenerate: `metamodule/assets/Loooop/Lop.png` (and re-run for `Loooop.png`; it may come out byte-identical)
- Verify no diff: `metamodule/loooop/Lop_info.hh`, `metamodule/loooop/Loooop_info.hh`

**Interfaces:**
- Consumes: Task 1's dimmed value-label rendering.
- Produces: `res/Lop.svg` with component `OVERDUB_PARAM#OverdubButton` at cx 37.9683 cy 75.05 and `GRID_PARAM#RoundSmallBlackSnapKnob` at cx 51.7083 cy 75.05 (all other components unmoved). Task 3's widget coordinates depend on these values.

- [ ] **Step 1: Edit `panel-specs/lop.yaml`**

Three edits:

1. In the row-2 items, replace the Overdub line:
```yaml
      - {label: Overdub, name: OVERDUB_PARAM, control: button, weight: 1.09,
         widget: OverdubButton}
```
(replaces `- {label: Overdub, name: OVERDUB_PARAM, control: switch, weight: 1.09}`)

2. In `nudges:` add two entries:
```yaml
  OVERDUB_PARAM: [0, 1.5]
  GRID_PARAM: [0, 1.0]
```

3. Update the comments: in the header comment, change the row-2 sentence to say "the Overdub mode button and Grid knob on the right, nudged down to center-align with the knobs"; above `nudges:` add a line explaining "Overdub/Grid drop 1.5/1.0mm from the label-line default so their centers sit on the knob row's center line (cy 75.05)".

- [ ] **Step 2: Regenerate both panels**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track
~/Dev/python-scripts/.venv/bin/python ~/Dev/vcv-panel-gen/panel_gen.py panel-specs/lop.yaml --out res/Lop.svg
~/Dev/python-scripts/.venv/bin/python ~/Dev/vcv-panel-gen/panel_gen.py panel-specs/loooop.yaml --out res/Loooop.svg
```
Expected: `Wrote res/Lop.svg: 9 params, 11 inputs, 2 outputs, 0 lights, 1 screens.` and `Wrote res/Loooop.svg: ...` with no errors.

- [ ] **Step 3: Verify geometry**

```bash
grep -o 'id="OVERDUB_PARAM#[^"]*" cx="[0-9.]*" cy="[0-9.]*"' res/Lop.svg
grep -o 'id="GRID_PARAM#[^"]*" cx="[0-9.]*" cy="[0-9.]*"' res/Lop.svg
git diff res/Loooop.svg | grep -E '^[+-].*c[xy]=' | head
```
Expected: `OVERDUB_PARAM#OverdubButton` cx 37.9683 cy 75.05; `GRID_PARAM#RoundSmallBlackSnapKnob` cx 51.7083 cy 75.05; the Loooop diff shows **no** cx/cy lines — i.e. no component circles moved. (Fill-color changes in its values layer are the point; small `<path d=...>` drift of ring labels is acceptable if the generator's ring-gap tweaks postdate Loooop's last regen.)

- [ ] **Step 4: Render and inspect**

```bash
inkscape res/Lop.svg --export-type=png --export-filename=/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/37c3791f-f1dd-4896-9475-94b4ff1ecc3e/scratchpad/Lop-check.png --export-dpi=300
```
Read the PNG. Check: Grid value ring labels are grey (not white), the GRID/ring cluster moved down \~1mm, no label collisions with the Record/Clear labels below, everything else unchanged.

- [ ] **Step 5: Position sync must be a no-op**

```bash
~/Dev/python-scripts/.venv/bin/python metamodule/loooop/sync_info_positions.py
git diff --stat metamodule/loooop/
```
Expected: script exits 0; no diff under `metamodule/loooop/` (Overdub/Grid are in `sync-map-lop.yaml`'s `ignore:` list; nothing else moved).

- [ ] **Step 6: Regenerate MM faceplate PNGs**

```bash
~/Dev/python-scripts/.venv/bin/python /Users/gabrielroth/Dev/metamodule-plugin-sdk/scripts/SvgToPng.py --input res/Lop.svg --output metamodule/assets/Loooop/ --layer panel
~/Dev/python-scripts/.venv/bin/python /Users/gabrielroth/Dev/metamodule-plugin-sdk/scripts/SvgToPng.py --input res/Loooop.svg --output metamodule/assets/Loooop/ --layer panel
```
Expected: both convert at 47.44 dpi. `git status` may show only `Lop.png` changed (Loooop's panel layer is unchanged; a byte-identical rewrite is fine either way).

- [ ] **Step 7: Commit**

```bash
git add panel-specs/lop.yaml res/Lop.svg res/Loooop.svg metamodule/assets/
git commit -m "panel: Löp Overdub button, Grid on knob center line, dim value labels"
```

---

### Task 3: C++ — shared OverdubControl, Löp five-state Overdub

**Files:**
- Create: `src/loooop/OverdubControl.hpp`
- Modify: `src/loooop/Loooop.cpp` (process() overdub block \~lines 125–133, lights block \~lines 215–224, `OverdubButton` struct def \~lines 232–241, add include)
- Modify: `src/loooop/Lop.cpp` (enums, config, process, widget, menu, add include)
- Modify: `metamodule/loooop/Lop_info.hh` (COMMENTS ONLY)

**Interfaces:**
- Consumes: `res/Lop.svg` coords from Task 2 (Overdub 37.968/75.05, Grid 51.708/75.05); `LoopEngine::{setOverdub,setWriteMode,WriteMode}` (existing).
- Produces: `loooop::applyOverdub(LoopEngine&, int)`, `loooop::kOverdubColors[5][3]`, `struct OverdubButton` in `src/loooop/OverdubControl.hpp`, used by both modules.

- [ ] **Step 1: Create `src/loooop/OverdubControl.hpp`**

```cpp
#pragma once
#include "plugin.hpp"
#include "dsp/LoopEngine.hpp"

namespace loooop {

// Overdub is one 5-state control shared by Loooop and Löp: four write modes
// + Lock (= overdub off, loop untouchable). While Locked the last write mode
// stays set; the engine ignores it with overdub off.
static constexpr LoopEngine::WriteMode kOverdubWriteModes[4] = {
    LoopEngine::WriteMode::Layer, LoopEngine::WriteMode::Decay,
    LoopEngine::WriteMode::Add,   LoopEngine::WriteMode::Replace};

// Overdub state color (Layer/Decay/Add/Replace/Lock), Quality-button style:
// an RGB LED in the bezel, driven from this table in each module's process().
static constexpr float kOverdubColors[5][3] = {
    {0.247f, 0.549f, 1.f},      // Layer   - blue   #3f8cff
    {1.f,    0.624f, 0.039f},   // Decay   - amber  #ff9f0a
    {0.188f, 0.820f, 0.345f},   // Add     - green  #30d158
    {1.f,    0.231f, 0.188f},   // Replace - red    #ff3b30
    {0.749f, 0.353f, 0.949f},   // Lock    - purple #bf5af2
};

inline void applyOverdub(LoopEngine& engine, int od) {
    engine.setOverdub(od != 4);   // 4 = Lock
    if (od >= 0 && od < 4)
        engine.setWriteMode(kOverdubWriteModes[od]);
}

} // namespace loooop

// Five-state overdub button, Quality-button style (see Particules): the
// stock light bezel with an RGB LED, made non-momentary so a click cycles
// the stepped param Layer/Decay/Add/Replace/Lock with wraparound.
struct OverdubButton : VCVLightBezel<RedGreenBlueLight> {
    OverdubButton() {
        momentary = false;
    }
};
```

- [ ] **Step 2: Refactor `src/loooop/Loooop.cpp` onto the shared header**

1. Add `#include "OverdubControl.hpp"` after the existing `#include "LooperModuleDSP.hpp"`.
2. In `process()`, replace the overdub block (the comment, the local `static constexpr LoopEngine::WriteMode kOverdubModes[4]`, and the three lines using it) with:
```cpp
        int od = (int)std::round(params[OVERDUB_PARAM].getValue());
        loooop::applyOverdub(engine, od);
```
3. At the end of `process()`, delete the local `static constexpr float kOverdubColors[5][3] = {...};` table (and its comment) and change the three light lines to:
```cpp
        lights[OVERDUB_R_LIGHT].setBrightness(loooop::kOverdubColors[od][0]);
        lights[OVERDUB_G_LIGHT].setBrightness(loooop::kOverdubColors[od][1]);
        lights[OVERDUB_B_LIGHT].setBrightness(loooop::kOverdubColors[od][2]);
```
4. Delete the whole `struct OverdubButton {...};` definition above `LoooopWidget` (it moved to the header). The widget creation line that uses `OverdubButton` stays unchanged.

- [ ] **Step 3: Rework `src/loooop/Lop.cpp`**

1. Add `#include "OverdubControl.hpp"` after `#include "LooperModuleDSP.hpp"`.
2. `ParamId`: delete `WRITE_MODE_PARAM` (GRID_PARAM follows CROSSFADE_PARAM directly). Update the comment above the enum to:
```cpp
    // Param/jack order mirrors Loooop's per-head block (minus the pan/level it
    // lacks) so the two modules' MetaModule menus read the same. Exception:
    // the MM build (Lop_info.hh) keeps an extra menu-only WriteModeAlt between
    // CrossfadeSwitch and GridAlt (MM patch compat; VCV absorbed Write mode
    // into the 5-state Overdub button), so MM ids after Crossfade are offset
    // by one — the same arrangement as Loooop.
```
3. `LightId`: `enum LightId { RECORD_LIGHT, OVERDUB_R_LIGHT, OVERDUB_G_LIGHT, OVERDUB_B_LIGHT, LIGHTS_LEN };`
4. In the constructor, replace the two configSwitch calls for OVERDUB_PARAM and WRITE_MODE_PARAM with one (keep the "Mode switches… opt out of Randomize" comment; delete the crossed write-mode call entirely):
```cpp
        configSwitch(OVERDUB_PARAM, 0.f, 4.f, 0.f, "Overdub",
            {"Layer", "Decay", "Add", "Replace", "Lock"})->randomizeEnabled = false;
```
5. In `process()`, replace
```cpp
        engine.setOverdub(params[OVERDUB_PARAM].getValue() > 0.5f);
```
and the `engine.setWriteMode(...)` call (two statements) with:
```cpp
        int od = (int)std::round(params[OVERDUB_PARAM].getValue());
        loooop::applyOverdub(engine, od);
```
(keep `engine.setCrossfade(...)` and `engine.setGrid(...)` as they are), and at the end of `process()` after the RECORD_LIGHT line add:
```cpp
        lights[OVERDUB_R_LIGHT].setBrightness(loooop::kOverdubColors[od][0]);
        lights[OVERDUB_G_LIGHT].setBrightness(loooop::kOverdubColors[od][1]);
        lights[OVERDUB_B_LIGHT].setBrightness(loooop::kOverdubColors[od][2]);
```
6. Widget: replace the CKSS line with
```cpp
        addParam(createLightParamCentered<OverdubButton>(mm2px(Vec(37.968, 75.05)), module, Lop::OVERDUB_PARAM, Lop::OVERDUB_R_LIGHT));
```
and change the GRID line's Vec to `Vec(51.708, 75.05)`.
7. `appendContextMenu`: delete the "Write mode" submenu block and the `kWriteModes` vector; change the comment to:
```cpp
        // Overdub (incl. write modes) and Grid are panel controls; only the
        // modes with no panel control live in the menu.
```

- [ ] **Step 4: Update `metamodule/loooop/Lop_info.hh` comments only**

In the comment above the Elements array (currently "Order mirrors Loooop's per-head block ... mirroring the VCV enums in src/loooop/Lop.cpp"), append:
```cpp
    // Exception: WriteModeAlt below is a menu-only extra with no VCV
    // counterpart (VCV absorbed Write mode into its 5-state Overdub button;
    // this build keeps the alt-param for MM patch compat), so ids after
    // CrossfadeSwitch are offset by one from the VCV enums — the same
    // arrangement as Loooop_info.hh.
```
Do NOT touch the Elements array, Elem enum, or LopCore.cc.

- [ ] **Step 5: Build and test**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track && make -C vcv -j8 2>&1 | tail -3
cd tests && ./run.sh 2>&1 | tail -4; cd ..
```
Expected: `plugin.dylib` links; test suite ends `OK` / all assertions passed.

- [ ] **Step 6: Commit**

```bash
git add src/ metamodule/loooop/Lop_info.hh
git commit -m "feat: Löp gets Loooop's five-state Overdub button, Write mode absorbed"
```

---

### Task 4: Dark screws in VCV (both modules)

**Files:**
- Modify: `src/loooop/Loooop.cpp` (delete 4 lines, currently \~251–254)
- Modify: `src/loooop/Lop.cpp` (delete 4 lines, currently \~161–164)

**Interfaces:** none (pure deletion; the SVG panel layer already draws dark screws at the same corners).

- [ ] **Step 1: Delete the ScrewSilver widgets**

In each of the two files, delete the four consecutive lines of the form:
```cpp
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
```
MF-20 and Particules keep theirs — do not touch any other file.

- [ ] **Step 2: Verify no stragglers and build**

```bash
grep -rn "ScrewSilver" src/loooop/ ; make -C vcv -j8 2>&1 | tail -2
```
Expected: grep finds nothing under src/loooop/; build links.

- [ ] **Step 3: Commit**

```bash
git add src/loooop/
git commit -m "Loooop and Löp show the panel's dark screws in VCV"
```

---

### Task 5: Docs, install, final verification

**Files:**
- Modify: `Loooop.md` (Löp section, currently line \~132)
- Create: `docs/superpowers/plans/2026-07-11-lop-overdub-grid-polish-user-checklist.md`
- Install (not committed): `~/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy/`

**Interfaces:** consumes the finished build from Task 4.

- [ ] **Step 1: Update `Loooop.md`'s Löp section**

Replace the sentence beginning "Löp has no Overdub button or Grid knob on its panel; instead its context menu carries…" so the paragraph reads:

```markdown
**Löp** is Loooop with a **single playhead** instead of four. It works exactly like one head of Loooop — including the note above: turn **Size** down before **Position** and **Jitter** have anything to do. Löp has Loooop's **Overdub** button (same five modes: Layer, Decay, Add, Replace, Lock) and **Grid** knob on its panel; its context menu carries its playhead's **One-shot on trigger**, **Speed CV = V/Oct**, and **Crossfade** — all with the same behavior as Loooop's.
```
(Keep the surrounding lines, including the screenshot img tag, untouched. If the current text differs slightly from the quoted sentence, replace the whole paragraph with the block above.)

- [ ] **Step 2: Write the user checklist**

Create `docs/superpowers/plans/2026-07-11-lop-overdub-grid-polish-user-checklist.md`:

```markdown
# Löp Overdub/Grid polish — user checklist (GUI checks Claude can't run)

Build installed to Rack2 plugins dir on 2026-07-11. In VCV Rack:

- [ ] Löp panel: Overdub is now an LED button above Record, on the Size/D-W
      knob center line; Grid knob + grey value ring sit on the same line.
- [ ] Clicking Overdub cycles Layer (blue) → Decay (amber) → Add (green) →
      Replace (red) → Lock (purple) → wraps to Layer. Colors match Loooop's.
- [ ] With a loop recorded on Löp: each mode writes like Loooop's same mode;
      Lock makes the Record button/trigger a no-op on the loop contents.
- [ ] Löp menu shows only One-shot on trigger / Speed CV = V/Oct / Crossfade
      (Overdub, Write mode, and Grid entries are gone).
- [ ] Both Loooop and Löp show dark screws (no silver) in Rack.
- [ ] Loooop's Grid value ring is dimmer grey now too.
- [ ] PATCH COMPAT: pre-2026-07-11 patches/presets that used Löp load with
      Overdub/Grid scrambled (Write mode was absorbed; ids shifted, like
      Loooop's earlier absorb). Old "Overdub Off" loads as Layer; the old
      Write-mode value lands on Grid. Re-save any Löp patches you keep.
- [ ] screenshots/Lop.png in the repo is stale — retake when convenient.
- [ ] MetaModule: no behavior change (Overdub/Write mode/Grid stay menu
      params there); rebuild the .mmplugin whenever you next cut one so the
      updated faceplate PNG ships.
```

- [ ] **Step 3: Install the plugin**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track
DEST=~/Library/Application\ Support/Rack2/plugins-mac-arm64/RobotBoy
cp vcv/plugin.dylib "$DEST/plugin.dylib" && cp plugin.json "$DEST/plugin.json" && rsync -a --delete res/ "$DEST/res/"
```
Expected: exits 0.

- [ ] **Step 4: Final render for the user**

```bash
inkscape res/Lop.svg --export-type=png --export-filename=/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/37c3791f-f1dd-4896-9475-94b4ff1ecc3e/scratchpad/Lop-final.png --export-dpi=300
```
Read the PNG; confirm the four spec items visually where possible (button placeholder position, Grid alignment, grey ring labels, dark screws).

- [ ] **Step 5: Commit**

```bash
git add Loooop.md docs/superpowers/plans/2026-07-11-lop-overdub-grid-polish-user-checklist.md
git commit -m "docs: Löp manual section and user checklist for Overdub/Grid polish"
```
