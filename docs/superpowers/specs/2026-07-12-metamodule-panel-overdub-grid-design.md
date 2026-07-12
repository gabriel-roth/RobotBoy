# MetaModule Loooop/Löp — panel Overdub button + Grid knob (match VCV)

**Date:** 2026-07-12
**Branch:** `mm-panel-controls` (worktree `.worktrees/mm-panel-controls`, off `main`)

## Goal

Make the MetaModule native cores expose **Overdub** as a click-cycling 5-state
RGB button and **Grid** as a stepped knob — real panel controls that function
exactly like the VCV modules — replacing today's menu "alt-params". Applies to
**both** Loooop and Löp.

## Background: current state

- **VCV** (`src/loooop/Loooop.cpp`, `Lop.cpp`, `OverdubControl.hpp`): Overdub is a
  5-state switch param `OVERDUB_PARAM` (0..4 = `Layer/Decay/Add/Replace/Lock`),
  bound to `OverdubButton : VCVLightBezel<RedGreenBlueLight>` (`momentary=false`,
  so clicks cycle the param), with RGB light ids `OVERDUB_R/G/B_LIGHT`. In
  `process()`: `int od = round(params[OVERDUB_PARAM]); applyOverdub(engine, od);`
  and `setOverdubLED(lights, OVERDUB_R_LIGHT, od)`. Grid is `GRID_PARAM` (0..5 =
  `Off/4/8/16/32/64`) on a `RoundSmallBlackSnapKnob`, read as
  `engine.setGrid(gridSegments(round(params[GRID_PARAM])))`.
- **MetaModule native cores** (`metamodule/loooop/LoooopCore.cc`, `LopCore.cc`,
  `Loooop_info.hh`, `Lop_info.hh`, `QlpElements.hh`): Overdub/Grid/Write mode are
  menu-only `AltParamChoiceLabeled` elements — `QlpOverdubAlt` (Off/On),
  `QlpWriteModeAlt` (4 modes), `QlpGridAlt` (6 pos). The core reads them with
  `getState<...>()` and there is **no panel button/knob and no RGB light** for
  these. The panel SVGs (`res/Loooop.svg`, `res/Lop.svg`) already carry the
  reserved component markers `OVERDUB_PARAM#OverdubButton` and
  `GRID_PARAM#RoundSmallBlackSnapKnob`, whose coordinates the current alt-params
  discard.

## Feasibility (verified against the MM SDK)

- **RGB button:** `MomentaryButtonRGB` (`base_element.hh`, `MomentaryButton` +
  `NumLights=3`). The core reads presses via
  `getState<Elem>() == MomentaryButton::State_t::PRESSED` and sets arbitrary
  color via `setLED<Elem>(std::array<float,3>{r,g,b})` (generic 3-channel
  `convertLED` fallback). No SDK element auto-cycles on click — the 5-state cycle
  is core logic (rising-edge → `(od+1)%5`), exactly as Particules' Quality button
  already does on the VCV-adapter side.
- **Stepped knob:** `KnobSnapped` (`base_element.hh`, `Knob` + `num_pos` +
  `pos_names`). `getState<Elem>()` returns a raw 0..1 float; the core rounds it
  (`round(v*(num_pos-1))`), same as VCV's snap-knob.
- **Persistence:** native cores have `save_state()` / `load_state()` on
  `CoreProcessor` (the `dataToJson`/`dataFromJson` equivalent), not overridden by
  `SmartCoreProcessor`. The Overdub index `od_` persists by overriding those.
  Grid persists automatically as a knob param value. (This mirrors how Particules
  persists `quality_state_` via `dataToJson`.)

## Design

### 1. Shared-code refactor (Rack-free)

Move the two Rack-free pieces out of `OverdubControl.hpp` (which includes
`plugin.hpp`) into the Rack-free `src/loooop/LooperModuleDSP.hpp` that the MM
cores already include:

- `kOverdubColors[5][3]` (the five state colors)
- `applyOverdub(LoopEngine&, int od)` (maps 0..4 → `setOverdub(od!=4)` +
  `setWriteMode(overdubWriteMode(od))`)

`OverdubControl.hpp` then includes `LooperModuleDSP.hpp` and keeps only the
VCV-only `OverdubButton` widget and the VCV `setOverdubLED(Lights&, …)` helper
(which indexes a Rack light array and is not usable by a native core). VCV
behavior must stay **bit-identical** — this is a pure move, no logic change.

### 2. Element structs (`QlpElements.hh`, in both/shared as today)

Add:

```cpp
struct QlpOverdubButton : MomentaryButtonRGB { constexpr QlpOverdubButton(BaseElement b) : MomentaryButtonRGB{{b}} {} };
struct QlpGridKnob : KnobSnapped { constexpr QlpGridKnob(BaseElement b)
    : KnobSnapped{{{b}}, 6, {"Off","4","8","16","32","64"}} {} };
```

(Exact field layout to match the SDK's `MomentaryButtonRGB` / `KnobSnapped`
constructors — the implementer confirms against `base_element.hh`.)

Remove `QlpOverdubAlt`, `QlpWriteModeAlt`, `QlpGridAlt`.

### 3. Element arrays + enums (`Loooop_info.hh`, `Lop_info.hh`)

- Replace the `QlpOverdubAlt` / `QlpWriteModeAlt` / `QlpGridAlt` entries with
  `QlpOverdubButton` (named `OverdubButton`) and `QlpGridKnob` (named `GridKnob`),
  positioned at the SVG's `OVERDUB_PARAM` / `GRID_PARAM` coordinates.
- Run `metamodule/loooop/sync_info_positions.py` so the button/knob get real
  coordinates from the SVG (they were `{0,0}` as menu-only params). Update the
  `sync-map-loooop.yaml` / `sync-map-lop.yaml` if the new element names need
  mapping entries.
- Update the element-count `std::array<Element, N>` sizes and the `Elem` enum
  (remove the three old names, add `OverdubButton`, `GridKnob`).
- Update the "menu-only" comment block to reflect that Overdub and Grid are now
  panel controls (Crossfade, Trig mode, Speed V/Oct, Grid-exclude remain
  menu-only and are untouched).

### 4. Core wiring (`LoooopCore.cc`, `LopCore.cc`)

- Add `int od_ = 0;` member (default = Layer).
- In `update()`:
  - Overdub: edge-detect the button press (same idiom as `RecordButton` /
    `ClearButton`); on rising edge `od_ = (od_ + 1) % 5;`. Every frame:
    `loooop::applyOverdub(engine_, od_);` and
    `setLED<OverdubButton>(std::array<float,3>{loooop::kOverdubColors[od_][0],
    kOverdubColors[od_][1], kOverdubColors[od_][2]});`.
  - Grid: `engine_.setGrid(loooop::gridSegments(
    (int)std::lround(getState<GridKnob>() * 5.f)));`.
  - Remove the old `getState<OverdubSwitch>()`, `getState<WriteModeAlt>()`,
    `getState<GridAlt>()` reads (and the `engine_.setOverdub/​setWriteMode/​setGrid`
    calls they fed — now driven by `applyOverdub` + the grid line above).
- Override persistence:
  ```cpp
  std::string save_state() override;                 // serialize od_ (e.g. std::to_string)
  void load_state(std::string_view data) override;   // parse od_; empty -> 0; clamp 0..4
  ```
- Keep `set_samplerate` (loop-preserving), Record/Clear, Crossfade, and all
  per-head handling exactly as-is.

### 5. Faceplate PNGs

The SVGs already render the Overdub button and Grid knob. Verify the MM faceplate
PNGs (`metamodule/assets/Loooop/Loooop.png`, `Lop.png`) show them; if stale,
regenerate via the SVG→PNG step (per the `vcv-to-metamodule` skill). No new art.

## Testing / verification

- **Refactor safety:** the VCV build (`make -C vcv -j8`) and both test lanes
  (`tests/run.sh`, `tests/particules_dsp/run.sh`) stay green — proof the
  `kOverdubColors`/`applyOverdub` move is behavior-preserving. No DSP changes, so
  no new engine tests are required (the mapping helpers are already exercised).
- **MM build:** `cmake -S metamodule -B metamodule/build && cmake --build
  metamodule/build -j8` compiles clean and produces
  `metamodule/metamodule-plugins/RobotBoy.mmplugin` with all symbols resolved.
- **GUI behavior (USER CHECK — not agent-testable):** in the MetaModule simulator
  or device: the Overdub panel button cycles Layer→Decay→Add→Replace→Lock with
  the matching RGB colors; the Grid knob snaps through Off/4/8/16/32/64 and
  quantizes audibly; both survive patch save/load; Löp behaves the same. The
  old menu entries (Overdub/Write mode/Grid) are gone.

## Patch compatibility

Pre-release (no shipped MetaModule patches), so removing the alt-params is a clean
break: any local MM test patch loads with Overdub = Layer and Grid = Off. No
migration needed.

## Out of scope

- Crossfade (stays a menu item on both hosts), per-head Grid-exclude, Trig mode,
  Speed V/Oct — untouched.
- VCV side — no changes beyond the pure shared-header move.
- No change to `LoopEngine` DSP.

## Files touched

- `src/loooop/LooperModuleDSP.hpp` — receives `kOverdubColors` + `applyOverdub`.
- `src/loooop/OverdubControl.hpp` — loses those two, includes the shared header.
- `metamodule/loooop/QlpElements.hh` — new button/knob structs; drop 3 alt-params.
- `metamodule/loooop/Loooop_info.hh`, `Lop_info.hh` — element arrays, enums, counts, comment.
- `metamodule/loooop/LoooopCore.cc`, `LopCore.cc` — `od_`, wiring, `save_state`/`load_state`.
- `metamodule/loooop/sync-map-loooop.yaml`, `sync-map-lop.yaml` — if new names need mapping.
- `metamodule/assets/Loooop/Loooop.png`, `Lop.png` — regenerate if stale.
