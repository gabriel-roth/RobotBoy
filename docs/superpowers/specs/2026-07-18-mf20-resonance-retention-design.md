# MF-20 Resonance Retention — design

## Problem

On the MF-20, turning up Drive audibly squashes the resonant "squelch" in
both filter revisions. Measured (headless, LP cutoff 500 Hz, res 0.80,
white-noise input ~1 V), the resonant peak's height above the passband
shoulder collapses as Drive goes 1 → 8:

| Mode | Drive 1 | Drive 8 | Change |
|------|--------:|--------:|-------:|
| OTA  | 27.1 dB |  9.7 dB | −17.4 dB |
| K35  | 24.8 dB | 11.1 dB | −13.7 dB |

This is inherent to how Drive is wired, and directionally correct for an
MS-20 (saturating the feedback / input path tames resonance), but the
magnitude is steep — Drive doubles as a "resonance eraser." We want a
tunable control that lets the user keep more resonance at high Drive.

## Mechanism

Drive is delivered to the filter core through two smoothed values
(`MF20Filter.cpp`):

- `_driveSqrt` = √drive — OTA **input pre-gain** only (`otaPreGain`).
- `_clipThresh` = 1/√drive — the saturating **clip threshold**, fed to
  `setDriveCharacterFromThreshold()`. This is the value that squashes
  resonance:
  - **OTA**: it is the feedback-diode clip threshold. Lower threshold →
    the resonant peak at the BP node clips sooner → resonance squashed.
  - **K35**: it sets the forward-clip pre-gain (`in / clipThreshold` =
    `in × √drive`). More pre-gain → input saturation floods the output
    with harmonics and the resonant peak no longer stands out.

Both squashing paths run off `_clipThresh`. So a single change addresses
both modes: interpolate the clip-threshold **target** away from
`1/√drive` toward `1.0` (its Drive-1 value) by a retention amount
`r ∈ [0, 1]`:

```
_clipThreshTarget = (1 − r) · (1 / √drive) + r · 1.0
```

- `r = 0` → `1/√drive` → **current behavior, unchanged** (patch-compatible).
- `r = 1` → `1.0` → the clip threshold no longer moves with Drive, so
  the feedback diode (OTA) / forward clip (K35) behaves as at Drive 1 →
  resonance preserved.
- At Drive 1, `1/√drive = 1.0`, so `r` has no effect (nothing to retain).

`satSlope = 0.25 · clipThreshold` is derived inside
`setDriveCharacterFromThreshold()`, so it tracks the retained threshold
automatically. Both endpoints are known-stable (Drive-8 and Drive-1
behavior respectively), so no new stability regime is introduced.

Note the tradeoff differs by mode, and that is expected:
- **OTA** keeps its input pre-gain (`_driveSqrt`, untouched), so Drive
  keeps its loudness/edge — retention only stops it from crushing the
  feedback peak. Clean win.
- **K35** has no separate pre-gain; `_clipThresh` is its only Drive path,
  so retaining resonance necessarily eases the input saturation. Higher
  retention = more resonance but gentler Drive coloring. Honest tradeoff,
  acceptable for this control.

## Control

One context-menu control, "Resonance retention", shared by both modes.

- Stored as `float _resRetention` in [0, 1], default **0.0**
  (= current behavior; existing patches sound identical on reload).
- Persisted in `dataToJson`/`dataFromJson` as `resRetention` (json_real,
  clamped [0,1]), mirroring the existing `_filterMode` persistence.
- Read in `modulate()` and folded into `_clipThreshTarget`; slewed by the
  existing `_clipThreshSlew` so slider moves don't zipper.
- UI follows the established Vespid/Particules pattern:
  - VCV desktop: `ui::Slider` + `Quantity` (`#ifndef METAMODULE`),
    displayed 0–100 %.
  - MetaModule: `createIndexSubmenuItem` discrete list
    (0 % / 25 % / 50 % / 75 % / 100 %) under `#ifdef METAMODULE`, snapping
    to the nearest value — same float/JSON field either way.

## Out of scope

- No panel/param change; menu-only, like the filter-revision toggle.
- No makeup gain or resonance-frequency compensation.
- K35 "split path" (keep full drive color *and* full resonance) is a
  possible future refinement, not this prototype.

## Verification

Re-run the headless resonance measurement (scratchpad `mf20_drive_test.py`,
extended with a `resRetention` state field) at Drive 8 for r ∈ {0, 0.5, 1}
in both modes. Expect: r=0 matches the current numbers above; r=1 restores
the resonant peak toward the Drive-1 level. Confirm r=0 output is
bit-identical / RMS-identical to pre-change Drive-8 output (patch-compat).
