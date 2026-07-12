# Fix MetaModule Loooop/Löp — "Options" roller section + Grid-knob stepping

**Date:** 2026-07-12
**Branch:** `mm-panel-fixes` (worktree `.worktrees/mm-panel-fixes`, off `main` @ 7cb01ce)

## Goal

Fix two plugin bugs introduced by the panel Overdub/Grid change, so the native
MetaModule Loooop and Löp arrange **exactly like VCV**:
1. The menu-only params (Crossfade, per-head One-shot / Speed-CV-is-V/Oct /
   Exclude-from-Grid) must appear grouped under an **"Options"** section in the
   module-view roller — not interleaved among the on-panel params.
2. The **Grid knob** must step through all six positions (Off/4/8/16/32/64) on
   the device, not jump straight from Off to 64.

Both are confirmed plugin bugs (root causes below); no firmware change, no core
DSP change.

## Root causes (confirmed against firmware v2.2.x + SDK)

### Bug 1 — no "Options" section (roller sectioning is order-based)
The firmware builds the module-view roller in element-array order and emits a
section header on type transitions (`firmware .../module_view/roller_helpers.hh`,
`append_header`): a param group that follows jacks/outputs/lights is headed
**"Options:"**; a param group at the start is headed **"Params:"**. In
`Loooop_info.hh`/`Lop_info.hh` the menu-only alt-params sit **before** the jacks
(interleaved with the panel knobs, grouped per head), so they render as
"Params". Particules (menu params after the jacks) gets an "Options" section.

### Bug 2 — Grid knob only reaches Off/64 (missing `max_value`)
`KnobSnapped` is a `Pot`; the firmware's manual-adjust arc range for it comes
from `min_value..max_value` (`manual_control_popup.hh` `set_snapped_range`). The
4ms convention — every shipping 4ms `KnobSnapped` (OctaveKnob `max_value=8/num_pos=9`,
Knob_1_10 `max_value=9/num_pos=10`, the DivMult knobs, …) — is to set
`max_value = num_pos - 1`. Our `QlpGridKnob` set `num_pos=6` but left
`min_value/max_value` at the `Pot` defaults `0.0/1.0`, so the arc got only 2
stops (0 and 1) → stored value 0.0 or 1.0 → `lround(getState()*5)` = 0 or 5 =
Off or 64. The knob element type is correct (`KnobSnapped` is what 4ms uses for
stepped panel knobs); only the declaration is missing `max_value`.

## Design

### Fix 2 (Grid knob) — `metamodule/loooop/QlpElements.hh`
Change `QlpGridKnob` to set `min_value=0, max_value=5` (= `num_pos-1`), matching
the 4ms `KnobSnapped` convention:
```cpp
struct QlpGridKnob : KnobSnapped {
    constexpr QlpGridKnob(BaseElement b)
        : KnobSnapped{{{{b, "4ms/comp/knob9mm_x.png"}, 0.f, 0.f, 5.f}}, 6,
                      {"Off", "4", "8", "16", "32", "64"}} {}
};
```
(`0.f, 0.f, 5.f` = default_value, min_value, max_value on the `Pot` base; confirm
the exact `Pot`/`Knob` field order against `base_element.hh` — `Pot` is
`{ImageElement, default_value, min_value, max_value}`.) The stored param stays
normalized 0..1 (Pot `convertState` returns the raw val; manual-adjust stores
`k/5`), so **`LoooopCore.cc`/`LopCore.cc` `gridSegments((int)std::lround(getState<GridKnob>()*5.f))`
is unchanged** — this matches how 4ms cores read their snapped knobs. Shared
struct, so both modules are fixed at once.

### Fix 1 (Options section) — reorder `Loooop_info.hh` + `Lop_info.hh`
Move **all** menu-only alt-params into one contiguous block placed **after** the
jacks (so `append_header` heads them "Options:"). The panel controls (Record,
Overdub button, Clear, Grid knob, Dry/Wet, and all per-head Size/Pos/Speed/
Jitter/Pan/Level knobs) stay in the leading "Params" region; the jacks stay in
the middle; the alt-params form the trailing "Options" block; the Display stays
after that.

- **Loooop:** remove `QlpCrossfadeAlt` from the global-params region and the
  `QlpTrigModeAlt`/`QlpVoctAlt`/`QlpExcludeGridAlt` from each per-head params
  block; re-add them all as one contiguous block immediately after the last
  output jack (before `QlpDisplay`). Keep per-head grouping *within* the Options
  block (Crossfade, then head1 Trig/Voct/Exclude, head2…, head4) so each head's
  options stay together.
- **Löp:** same, single head (Crossfade + one Trig/Voct/Exclude set) moved after
  its jacks.
- The `Elem` enum must be reordered **in lockstep** with the array (identical
  order) — `SmartCoreProcessor` maps enum→element by index. The cores reference
  every element by enum **name** (`getState<TrigMode1Alt>()`, the
  `updateHead<…>` template args, `getState<CrossfadeSwitch>()`), so reordering
  updates the name→index mapping automatically and **no `.cc` changes are
  needed**.
- Update the header comment that says params/alt-params are "grouped PER HEAD …
  so the mapping menu lists everything for one head together" to reflect the new
  layout (panel knobs per head in Params; alt-params grouped in Options after
  the jacks).

## Testing / verification

- **MM build:** `cmake --build metamodule/build -j8` compiles + links + "All
  symbols found" and produces `metamodule/metamodule-plugins/RobotBoy.mmplugin`.
  Confirm array/enum counts stay consistent (net zero element change — nothing
  added or removed, only reordered for Fix 1; Fix 2 changes no count).
- **VCV + lanes:** unaffected (no VCV or core-DSP files touched), but run
  `tests/run.sh` + `make -C vcv -j8` once as a regression guard.
- **GUI user check (not agent-testable):** on the device/simulator — the roller
  shows an "Options" section containing Crossfade + per-head One-shot / Speed
  V/Oct / Exclude-from-Grid (grouped at the bottom like Particules), and the
  Grid knob steps through Off/4/8/16/32/64.

## Patch compatibility
Pre-release; the reorder shifts alt-param indices, but there are no shipped MM
patches to migrate.

## Out of scope
- Overdub button and Grid knob stay on the panel (unchanged placement).
- No VCV changes, no `LoopEngine` DSP changes, no core `.cc` logic changes.
- Crossfade/One-shot/V-Oct/Grid-exclude remain the SAME alt-param elements —
  only their position in the array (hence roller section) changes.

## Files touched
- `metamodule/loooop/QlpElements.hh` — Fix 2 (Grid knob `max_value`).
- `metamodule/loooop/Loooop_info.hh`, `Lop_info.hh` — Fix 1 (reorder array + enum).
