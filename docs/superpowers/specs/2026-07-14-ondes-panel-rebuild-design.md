# Ondes panel rebuild — design

**Date:** 2026-07-14
**Branch/worktree:** `add-ondes`
**Goal:** Rebuild the Ondes panel with `vcv-panel-gen`, widen it by 2 HP, and
add a live wavetable display under the title.

## Summary

Ondes is currently a hand-built 6 HP panel (`res/Ondes.svg`). This rebuild
regenerates it from a declarative `vcv-panel-gen` spec at **8 HP** (2 HP wider),
adds a morphing wavetable **display** below the title, and puts a **cyan
background plate** behind the control area (but not the Out jack). The DSP and
control set are unchanged; only the panel and a new display widget change.

## Control set (unchanged from `src/particules/Ondes.cpp`)

Params: `PITCH_PARAM`, `POSITION_PARAM`, `POSITION_AMT_PARAM`, `BANK_PARAM`,
`BANK_AMT_PARAM`.
Inputs: `VOCT_INPUT`, `POSITION_INPUT`, `BANK_INPUT`.
Output: `OUT_OUTPUT`.

No params/inputs/outputs are added or removed. The attenuverters
(`*_AMT_PARAM`) stay.

## Panel layout (8 HP, top → bottom)

```
┌──────────────────┐
│      ONDES       │  title wordmark (house theme)
│ ┌──────────────┐ │
│ │ ~ wavetable ~ │ │  SCREEN row — live morphing waveform trace
│ └──────────────┘ │
│   (Pitch) [V/Oct]│░  Pitch knob (regular RoundBlackKnob) + V/Oct jack beside it
│░ Bank   Position ░│  two knobs
│░ [cv]     [cv]   ░│  two CV jacks (unlabeled)
│░  (o)     (o)    ░│  two attenuverter trimpots (unlabeled)
│░░░░░░░░░░░░░░░░░░│  cyan rect: covers Pitch + Bank + Position
│      [Out]       │  Out — OUTSIDE the cyan rect
└──────────────────┘
```

### Rows

1. **Title** — "ONDES" wordmark, house theme (Shuttleblock title font, dark
   `#3d3d3d` panel, Futura labels, uppercase, dark screws). Title size tuned to
   fit 8 HP at generation time.
2. **Screen** — a `screen` row reserving the display region below the title.
   The C++ widget (below) draws into it.
3. **Pitch + V/Oct** — horizontal-companion idiom: `PITCH_PARAM` as a regular
   `RoundBlackKnob` with `VOCT_INPUT` immediately to its right under one shared
   "Pitch" label. (Regular size — not the current `RoundLargeBlackKnob`.)
4. **Bank / Position** — two aligned columns. Each column, top→bottom, keeps its
   current internal stack: **knob → CV jack → attenuverter trimpot**. The knob
   carries the column label ("Bank" / "Position"); the CV jack and attenuverter
   are unlabeled (`label: "{blank}"`), matching the current panel. Realized as
   three 2-item rows (knobs / CV inputs / small Trimpot params) so the columns
   line up vertically.
5. **Out** — `OUT_OUTPUT` centered at the bottom, outside the cyan plate.

### Cyan background plate

A single `zones:` rect (cyan fill, ~0.14 opacity, rounded corners) spanning the
control area from just below the display to just above the Out jack, covering
**Pitch, Bank, and Position**. The Out jack sits below the rect. Exact
`x/y/w/h` derived from the generated component positions after a first pass;
tuned with the `picking-panel-rect-colors` skill for the cyan value.

## Wavetable display widget

New `WavetableDisplay` widget in `Ondes.cpp`, modeled on Fundamental's
`WTDisplay` (`~/Dev/Fundamental/src/Wavetable.hpp`):

- Subclass `LedDisplay`; override `drawLayer(args, layer)` and draw on
  `layer == 1` (the glowing overlay layer).
- **Bare trace only** — no filename/readout text. Ondes uses a fixed built-in
  wavetable set (`WavetableData`: 24 banks × 8 waveforms × 256 samples), so
  there is no filename to show.
- Draws the **current bilinearly-interpolated 256-sample frame** as a **cyan**
  stroke. The interpolation mirrors the DSP: `bank` (0–1) maps across
  `kNumWavetableBanks`, `wave`/position (0–1) across `kWaveformsPerBank`; the
  four neighbouring waveforms are bilinearly blended, same as
  `WavetableOscillator::Process`. Repaints as bank/position change, so the trace
  morphs live.
- **Module → widget data:** `Ondes` exposes its last-computed post-CV `bank`
  and `wave` values as public members (e.g. `float lastBank`, `float lastWave`),
  updated each `process()` call — analogous to Fundamental's `lastPos`. The
  widget reads these (guarding `module == nullptr` for the browser/library
  case, where it can draw a default frame).
- Wired in `OndesWidget` with `createWidget<WavetableDisplay>(...)` positioned
  and sized from the SVG screen rect (carried over by `build-install.sh`'s SVG
  position sync).

## Build / sync flow

1. Write spec `panel-specs/ondes.yaml` (house style, matching Loooop/Löp specs).
2. Generate: `panel_gen.py panel-specs/ondes.yaml --out res/Ondes.svg`.
3. Preview with `preview.py res/Ondes.svg --open`; tune spec (never hand-edit
   the SVG) and the cyan zone geometry; regenerate.
4. Implement `WavetableDisplay` + the `lastBank`/`lastWave` members in
   `Ondes.cpp`; wire the widget.
5. Build/install with the RobotBoy VCV flow (`make -C vcv`, copy artifacts into
   the Rack2 plugins dir — see `robotboy-vcv-install` memory; there is no
   `build-install.sh` in this repo, so SVG→widget position sync is done by the
   normal `helper.py`/build path or carried manually).
6. **MetaModule faceplate:** Ondes is in the MetaModule build. Regenerate
   `metamodule/assets/Ondes.png` from the same SVG with `SvgToPng.py --layer
   panel`, and sync any hand-maintained `_info.hh` positions with `mm_sync.py`.
   The MetaModule side has no live display widget (static faceplate only).

## Out of scope

- No DSP/algorithm changes.
- No new params, inputs, or outputs.
- MetaModule gets the new faceplate art but not the live display.

## Open items to resolve during implementation

- Exact title size that fits "ONDES" at 8 HP.
- Exact screen height and cyan-zone geometry (after first generation pass).
- Confirm the RobotBoy install path lacks `build-install.sh` and use the
  documented `make -C vcv` + copy flow.
