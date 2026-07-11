# Löp Overdub button, Grid alignment, value-label color, dark screws — design

Date: 2026-07-11. Branch: `loooop-track` (worktree `.worktrees/loooop-track`).
Follows the same-day commit 7f34c7f ("feat: Overdub switch and Grid knob on
the Löp panel"), which put a CKSS on/off Overdub switch and the Grid knob on
Löp's panel. User feedback on that commit drives four changes.

## 1. Löp's Overdub becomes Loooop's five-state LED button

**What.** Replace Löp's on/off CKSS Overdub switch with the exact control
Loooop has: `OverdubButton` (a non-momentary `VCVLightBezel<RedGreenBlueLight>`
that cycles with wraparound), whose param is a 5-state
`configSwitch(0..4, default 0) {"Layer", "Decay", "Add", "Replace", "Lock"}`
with `randomizeEnabled = false`. The five states absorb Löp's separate
`WRITE_MODE_PARAM` (Lock = overdub off), exactly as Loooop's absorb did.

**VCV param changes (Lop.cpp).**
- `OVERDUB_PARAM`: range 0–1 default 1 → range 0–4 default 0 (Layer), labels
  as above.
- `WRITE_MODE_PARAM`: **removed** from the enum. `GRID_PARAM` shifts from
  id 12 to id 11; `PARAMS_LEN` 13 → 12.
- `LightId` gains `OVERDUB_R_LIGHT, OVERDUB_G_LIGHT, OVERDUB_B_LIGHT` after
  `RECORD_LIGHT` (appended — light ids are not stored in patches).
- `process()`: replace the `setOverdub(bool)` + `setWriteMode(WRITE_MODE)`
  pair with Loooop's mapping — `setOverdub(od != 4)`, and for od 0–3 set the
  write mode via the table {Layer, Decay, Add, Replace}. Drive the RGB LED
  from Loooop's `kOverdubColors` table (blue/amber/green/red/purple).
- Context menu: drop the "Write mode" submenu. Menu keeps One-shot on
  trigger, Speed CV = V/Oct, Crossfade (same tail as Loooop's per-head items).

**Shared code (DRY).** Loooop.cpp currently holds the `OverdubButton` widget
struct and the two tables (`kOverdubModes`, `kOverdubColors`) privately. They
move to a new shared header `src/loooop/OverdubControl.hpp` (includes
`plugin.hpp` + `dsp/LoopEngine.hpp`) exposing:
- `loooop::kOverdubColors[5][3]`
- `loooop::applyOverdub(LoopEngine&, int od)` — the setOverdub/setWriteMode
  mapping
- `struct OverdubButton` (widget)
Loooop.cpp is refactored to use it; behavior identical.

**Patch compatibility — accepted break.** Pre-change Löp `.vcv` patches load
scrambled: old Overdub On/Off (0/1) reads as Layer/Decay; the old Write-mode
value (id 11) lands on Grid; the old Grid value (id 12) is dropped. This
mirrors the accepted break when Loooop absorbed Write mode (see the
`loooop-param-reorder-scrambles-old-patches` memory / ledger). Noted in the
user checklist: re-save any Löp patches.

**MetaModule side — no behavior change.** Loooop's MM build still uses the
old scheme (2-choice `QlpOverdubAlt` + 4-choice `QlpWriteModeAlt` menu
params); Löp's MM build stays on the same scheme for the same reason (MM
patch-loader zero-init compat). Consequence: after VCV removes
`WRITE_MODE_PARAM`, `Lop_info.hh` keeps its `QlpWriteModeAlt` as a legacy
extra, so MM ids after the globals are offset by one from the VCV enum —
exactly the documented arrangement in Loooop.cpp / Loooop_info.hh. Update
both files' mirroring comments to say so. `LopCore.cc`, `sync-map-lop.yaml`
unchanged.

## 2. Grid knob (and the new button) center-align with Size / D-W

The generator top-aligns a params row's controls on the label line, which
left the Grid knob center at cy 74.05 and the Overdub control at 73.55 while
the Size/D-W knob centers sit at 75.05. Per user: the Grid knob and its value
ring move down to center-align. The Overdub button gets the same treatment —
"like we've got on Loooop", where the bottom row's button and knobs share one
center line.

In `panel-specs/lop.yaml`:
- Overdub item becomes `control: button, widget: OverdubButton` (weight 1.09
  unchanged).
- `nudges:` gains `OVERDUB_PARAM: [0, 1.5]` and `GRID_PARAM: [0, 1.0]`
  (both land on cy 75.05; the value ring and labels travel with the knob —
  generator behavior covered by `test_value_ring_moves_with_nudge`).

## 3. Value-ring labels render dimmer (generator change, both panels)

Value-ring labels currently draw in the full text color (`#ffffff` on these
panels). Per user they should be greyer, blending toward the background —
everywhere value rings appear, now and in future. So this is a
**vcv-panel-gen default-behavior change**, not per-spec styling:

- New optional theme key `value_color` (hex, validated like `text_color`).
- New `resolve_value_color(theme)`: explicit `value_color` if set, else the
  resolved text color blended toward the background at
  `VALUE_TEXT_MIX = 0.55` (55% text, 45% background), rounded per channel.
  On these panels (#ffffff text, #3d3d3d background) that yields `#a8a8a8`.
- `svgdoc.build_svg` fills the `values` layer paths with the resolved value
  color instead of `text_color`.
- Tests: theme parse/validation, blend math, svgdoc layer fill.
- No change to `~/.config/vcv-panel-gen/theme.yaml` — the default blend does
  the job; the key exists for future tuning.

Both `res/Loooop.svg` and `res/Lop.svg` are regenerated so the two modules
pick up the color (Loooop's panel gets no geometry change; position sync must
report no element moves for it).

## 4. Dark screws in VCV Rack

The regenerated SVGs already draw dark (#333333) screws per the personal
theme, but both modules' `ModuleWidget` constructors stack four `ScrewSilver`
widgets on top, so Rack still shows silver. Fix: delete the four
`addChild(createWidget<ScrewSilver>(...))` lines from **both** Loooop.cpp and
Lop.cpp and let the panel art's dark screws show. (MF-20 and Particules are
out of scope.)

## Regeneration / downstream pipeline (unchanged mechanics)

After spec + generator changes: regenerate both SVGs → render-inspect →
`metamodule/loooop/sync_info_positions.py` (expect: Loooop no moves; Lop no
element moves — Overdub/Grid are in the sync map's `ignore` list) → hand-carry
the two changed Lop.cpp widget coords (Overdub 37.968/75.05, Grid
51.708/75.05) → regenerate both MM faceplate PNGs via the SDK's `SvgToPng.py
--layer panel` (values layer excluded, so only Lop's moved GRID label changes
its PNG) → `make -C vcv` → `tests/run.sh` → install dylib/json/res into
`~/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy/`.

## Docs

- `Loooop.md` Löp section: now states Löp has the Overdub button (same five
  modes) and Grid knob on-panel; menu carries One-shot, Speed CV = V/Oct,
  Crossfade. The `screenshots/Lop.png` image is stale — user checklist item.
- User checklist: `docs/superpowers/plans/2026-07-11-lop-overdub-grid-polish-user-checklist.md`.
- Ledger: `.superpowers/sdd/progress.md` per-task entries.

## Out of scope

- MM `.mmplugin` rebuild (no MM source change; PNGs land for the next build).
- MF-20 / Particules screws.
- Migrating old Löp patches in code (accepted break, as with Loooop).
