# Loooop Panel Wiring (VCV) — Design

2026-07-10

## Purpose

Wire up the two new controls on the revised Loooop panel (see
`panel-specs/loooop.yaml`): a five-state Overdub color button and a
four-position Grid snap knob, both on the bottom row. This replaces the
context-menu Overdub on/off and Write mode settings with a single panel
control. VCV Rack only — the MetaModule side (positioned elements in
`Loooop_info.hh`, state frames, faceplate PNG) is a follow-up, and Löp is
untouched.

**Patch compatibility is intentionally broken** (user decision 2026-07-10):
the param list changes shape and old patches are not migrated.

## Param change

In `src/loooop/Loooop.cpp`:

- `OVERDUB_PARAM` becomes a five-state switch:
  `configSwitch(OVERDUB_PARAM, 0.f, 4.f, 0.f, "Overdub",
  {"Layer", "Decay", "Add", "Replace", "Lock"})` — default **Layer**.
- `WRITE_MODE_PARAM` is **deleted** (enum, configSwitch, process() read,
  menu item). The enum tail becomes
  `DRYWET_PARAM, RECORD_PARAM, CLEAR_PARAM, OVERDUB_PARAM, CROSSFADE_PARAM,
  GRID_PARAM, PARAMS_LEN`.
- The enum comment about mirroring the MetaModule Elem order stays; the MM
  header is reconciled in the follow-up.

State semantics: **Lock** = the old Overdub Off (loop locked; Record does
nothing while a loop exists). The other four are the old write modes.

## Engine mapping

In `process()`, replacing the current `setOverdub`/`setWriteMode` reads:

```cpp
int od = (int)std::round(params[OVERDUB_PARAM].getValue());
engine.setOverdub(od != 4);                       // 4 = Lock
static constexpr LoopEngine::WriteMode kOverdubModes[4] = {
    LoopEngine::WriteMode::Layer, LoopEngine::WriteMode::Decay,
    LoopEngine::WriteMode::Add,   LoopEngine::WriteMode::Replace};
if (od < 4)
    engine.setWriteMode(kOverdubModes[od]);
```

When Locked, the last write mode is left in place — the engine ignores it
while overdub is off. `LoopEngine` itself is unchanged.

## OverdubButton widget

A custom widget local to `Loooop.cpp`, deriving from `app::Switch` (Rack's
stepped-param button base: click-to-cycle with wraparound, undo/history, and
the right-click value menu all come from the param's `configSwitch`).

- Drawn with NanoVG, no SVG frame assets: a `VCVButton`-sized round bezel
  (match the 9 mm footprint the panel generator reserved) whose center fills
  with the state color.
- Colors: Layer **blue `#3f8cff`**, Decay **amber `#ff9f0a`**, Add **green
  `#30d158`**, Replace **red `#ff3b30`**, Lock **purple `#bf5af2`** (matches
  the panel's existing head-color family).
- Click cycles Layer → Decay → Add → Replace → Lock → Layer.
- Placed at the SVG circle position for `OVERDUB_PARAM` (cx 68.88, cy
  116.05 as of the current `res/Loooop.svg`; use the synced coordinates).

## Grid knob

`addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(121.63, 116.05)),
module, Loooop::GRID_PARAM));` — the existing `configSwitch` (Off/4/8/16,
default Off) already provides snapping and tooltip labels. Engine and
display wiring for Grid exist from the grid-mode work; nothing else changes.

## Context menu cleanup

Remove the "Overdub" bool item, the "Write mode" submenu, and the "Grid"
submenu from `appendContextMenu` — these controls now live on the panel,
matching the panel-only convention of Record/Clear/D-W. "Crossfade loop
seams" and the per-head submenus stay.

## Testing

- Engine tests are unaffected: the change is host-level param mapping only.
- Machine-verifiable: the plugin compiles and installs
  (`make -B` in `vcv/`, widget positions already synced from the SVG).
- GUI checks go on a **user-run checklist** at the end (no agent-driven
  GUI/simulator tests): button cycles in the agreed order with the agreed
  colors; Lock actually locks (Record inert over an existing loop); Grid
  knob snaps through Off/4/8/16 and the display bars follow; right-click
  value menus show the labeled states; removed menu items are gone.

## Out of scope

- MetaModule wiring (info-header elements, LoooopCore reads, sync maps,
  faceplate PNG, state frames) — follow-up on `loooop-track`.
- Löp (`src/loooop/Lop.cpp` keeps its own Overdub/Write mode menu params).
- Any patch-migration shim.
