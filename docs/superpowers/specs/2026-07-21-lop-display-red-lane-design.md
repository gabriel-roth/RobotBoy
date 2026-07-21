# Löp display: standard-height red playhead lane

**Date:** 2026-07-21
**Status:** Approved (user confirmed red choice; "work autonomously" from there)

## Goal

Change the Löp panel display so its single playhead lane matches Loooop's
lane height (height/8 instead of height/4) and draws in red instead of
purple. The waveform region absorbs the freed vertical space.

## Decisions

- **Red = Loooop head-1 red (`#FF3B30`, `loooop::kHeadColors[0]`).** User
  chose reusing the exact H1 red over a distinct red. Löp's single lane vs
  Loooop's four is already enough to tell the displays apart, so the old
  "Löp's lane color must not be a Loooop head color" rule is retired.
- **Remove the override mechanism, don't retune it.** With the standard
  lane height and `HEAD_COLORS[0]` red, Löp's display style is exactly the
  default single-head style. So instead of setting `LOP_LANE_DIV = 8` and a
  red `LOP_LANE_COLOR`, delete both constants and every place that plumbs
  them through:
  - `LoopWaveformRenderer.hpp`: drop `LOP_LANE_COLOR`, `LOP_LANE_DIV`, the
    `laneDiv` parameter on `laneHeight()`/`geometry()`, and the
    `headColors` parameter on `renderLanes()` (no caller passes non-default
    values anymore).
  - `LoopDisplay.hpp` (VCV widget): drop the `laneDiv`/`laneColors` member
    overrides.
  - `Lop.cpp` (VCV): drop the two override assignments.
  - `metamodule/loooop/LopCore.cc` (MM): drop the explicit
    `LOP_LANE_DIV`/`LOP_LANE_COLOR` arguments — the calls become identical
    in shape to `LoooopCore.cc`'s.

## What changes visually

Only proportions and color inside the existing display rect; the widget
box (`Lop.cpp` / `Lop_info.hh` screen rect) is untouched, so no SVG or
position-sync work. Lane band: height/4 purple → height/8 red. Waveform:
gains the freed height/8.

## Testing

- `tests/loooop/test_display_renderer.cpp`: the Löp-specific test
  (`test_lop_style_double_purple_lane`) is superseded — Löp now renders the
  default single-head style already covered by the existing single-head
  test — and is deleted along with its call.
- Run `tests/run.sh`; build VCV (`make -C vcv`) and install per the usual
  copy step. MM core shares the same renderer constants, so no separate MM
  change beyond `LopCore.cc`'s dropped arguments.
- GUI checks (lane is red, standard height, taller waveform) go to the
  user-run checklist per project convention.
