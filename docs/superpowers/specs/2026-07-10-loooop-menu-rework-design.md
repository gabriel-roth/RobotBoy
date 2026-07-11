# Loooop Context-Menu Rework — Design

2026-07-10

## Purpose

Three related changes, per user direction:

1. **Invert the VCV context menu:** instead of one submenu per playhead
   holding the commands, list the commands at the top level (**Trigger**,
   **Speed CV is V/Oct**, and the new **Exclude from Grid**) with the four
   playheads inside each command's submenu.
2. **Name the playheads by color** — *Red playhead*, *Green playhead*,
   *Blue playhead*, *Yellow playhead* — instead of *Head 1..4*. The order
   matches `LoopWaveformRenderer::HEAD_COLORS` and the panel group tints:
   H1 red `#ff3b30`, H2 green `#30d158`, H3 blue `#3f8cff`, H4 yellow
   `#fff70a`.
3. **New per-head setting: "Exclude from Grid"** — an excluded playhead
   ignores Grid quantization entirely (window size and position move freely,
   continuous Jitter and Position CV included) even while Grid is 4–64 for
   the other heads.

Löp is untouched: with a single head, excluding it is equivalent to setting
Grid to Off.

## Engine (`src/loooop/dsp/LoopEngine.{hpp,cpp}`)

- `PlayHead` gains `bool gridExclude = false`.
- New setter, matching the `setTrigMode`-style per-head setters:
  `void setGridExclude(int head, bool exclude)` (bounds-checked, plain
  store — it mirrors a param and is re-set every host block, like the other
  per-head settings).
- The single gate in `windowBounds(const PlayHead&, float, double&, double&)`
  changes from `if (grid_ >= 2)` to `if (grid_ >= 2 && !h.gridExclude)`.
  Everything else (min-window clamp, edge clamps) is the shared un-gridded
  tail and already correct for the excluded path.
- Display: unchanged. Grid bars still render (the loop is still gridded);
  an excluded head's window/playhead markers simply need not sit on
  boundaries. `DisplaySnapshot` needs no new field.
- `reset()`/`clear()` do not touch `gridExclude` — hosts re-assert it from
  the param every block (same lifecycle as trig mode / V-Oct).

## VCV host (`src/loooop/Loooop.cpp`)

### Params

- Per-head block gains `EXCLUDE_GRID1_PARAM` after `SPEED_VOCT1_PARAM`;
  `HEAD_PARAMS` 8 → 9. `PARAMS_LEN` grows by 4. Menu-only (no widget).
- `configSwitch(EXCLUDE_GRID1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f,
  "<Color> playhead exclude from Grid", {"Off", "On"})` — default Off.
- **Naming sweep:** every user-visible per-head VCV label switches from
  "Head N ..." to "<Color> playhead ..." (configParam/configSwitch/
  configInput/configOutput strings). A `kHeadNames[4] = {"Red", "Green",
  "Blue", "Yellow"}` table replaces the `std::to_string(h+1)` pattern;
  label style: "Red playhead size", "Red playhead trigger", "Red playhead
  left", etc.
- `process()` per-head loop adds
  `engine.setGridExclude(h, params[EXCLUDE_GRID1_PARAM + HEAD_PARAMS * h].getValue() > 0.5f);`

### Menu

`appendContextMenu` becomes:

```
──────────────
Crossfade loop seams          [bool]
Trigger                       ▸ Red playhead    ▸ Loop start / One-shot
                                Green playhead  ▸ ...
                                Blue playhead   ▸ ...
                                Yellow playhead ▸ ...
Speed CV is V/Oct             ▸ Red playhead    [bool]
                                ... (4 items)
Exclude from Grid             ▸ Red playhead    [bool]
                                ... (4 items)
```

Command order: Trigger, Speed CV is V/Oct (existing order), then the new
Exclude from Grid last.

> **DECISION 2026-07-11:** the "Trigger" command became **"One-shot"** — a
> per-playhead checkmark (bool) instead of a Loop start/One-shot index
> submenu. Unchecked (default) = restart-at-window-start, which is
> deliberately unnamed in the interface; the VCV param label is
> "<Color> playhead one-shot" {Off, On}. MetaModule and Löp keep the
> two-choice "Trig mode" (Loop start / One-shot) setting.

## MetaModule host

- `QlpElements.hh`: new `QlpExcludeGridAlt` — `AltParamChoiceLabeled`,
  2 choices `{"Off", "On"}`, default 0. Index 0 = Off so the loader's
  zero-init keeps pre-existing patches unexcluded (same rationale as
  `QlpGridAlt`).
- `Loooop_info.hh`: each per-head param group appends
  `QlpExcludeGridAlt{{0.f, 0.f, Center, "Grid N exclude", ""}}` after that
  head's `QlpVoctAlt`. `Elements` array 87 → 91; `Elem` enum gains
  `ExcludeGrid1Alt..ExcludeGrid4Alt` in the per-head groups. Update the
  header comment's menu-only list. Jack ordering is untouched, so
  `bypass_routes` indices stay `{0,0},{1,1}`.
- `LoooopCore.cc`: `updateHead` template gains an `Info::Elem XG` parameter;
  body adds `engine_.setGridExclude(h, getState<XG>() == 1);` The four call
  sites pass `ExcludeGridNAlt`.
- MM element naming keeps the existing numeric convention ("Trig 1 mode"
  style) — the color-name sweep is VCV-only this round. Renaming MM element
  short-names would ripple into the name-matched sync maps
  (`sync-map-loooop.yaml`) for zero functional gain; logged as a possible
  follow-up.

## Compatibility

Param indices shift on both hosts (VCV stride 8 → 9; MM Elements insertions).
Patch compatibility this cycle is already broken by today's globals-first
reorder (sanctioned, unreleased) — this rides the same break.

## Tests (`tests/loooop/test_loop_engine.cpp`)

New `test_grid_exclude_head()` following the existing grid-test style:

1. Excluded head ignores snapping: grid 4 on a 16-sample ramp,
   size 0.3 / position 0.37 (values that visibly snap in the existing
   tests); with `setGridExclude(0, true)` the output must match a
   grid-off engine sample-for-sample (the `test_grid_off_matches_ungridded`
   comparison pattern).
2. Non-excluded heads still snap while another head is excluded
   (`windowBounds` is per-head): head 0 excluded, head 1 gridded — head 1's
   snapshot window sits on segment bounds.
3. Re-including (`setGridExclude(0, false)`) restores snapping.

## User checks (GUI — queued for the user, not agent-run)

- VCV: menu shows Trigger / Speed CV is V/Oct / Exclude from Grid with
  color-named playhead submenus; tooltips show color names; excluding the
  red playhead frees it while others stay locked to the grid.
- MetaModule (or simulator): options list shows the four "Grid N exclude"
  items and exclusion audibly works.
