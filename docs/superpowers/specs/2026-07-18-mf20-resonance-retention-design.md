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

Both modes must **keep their Drive character (grit) and regain the
resonant peak**. Each mode gets a different, mode-appropriate retention
path (`modulate()` branches on `_filterMode`), because the two revisions
squash resonance through different stages.

Drive reaches the filter core through smoothed values (`MF20Filter.cpp`):
`_driveSqrt` = √drive (OTA input pre-gain), `_clipThresh` (the saturating
clip threshold fed to `setDriveCharacterFromThreshold()`), and the new
`_fbThresh` (K35 resonance-loop clip threshold, via `setFbThreshold()`).

Let `r = _resRetention ∈ [0, 1]`.

**OTA** — the feedback diode (threshold = `_clipThresh`) is what clips the
resonant BP-node peak; the input pre-gain (`_driveSqrt`) is a separate
stage and is the grit source. So retention eases only the diode:

```
_clipThreshTarget = (1 − r) · (1 / √drive) + r · 1.0
```

Input pre-gain is untouched → Drive keeps its loudness/edge; the diode
relaxes toward its Drive-1 threshold → the peak rings back. Both endpoints
are known-stable (Drive-8 / Drive-1).

**K35** — the *forward input clip* does double duty: it is both the grit
source and (because it is the only Drive-dependent stage) the resonance
squasher. Easing it would take the grit with it. Instead, split the two
jobs:

- Forward clip stays at **full drive** (`_clipThresh = 1/√drive`,
  retention-independent) → grit intact.
- The resonance-loop clip threshold opens with retention:

```
_fbThreshTarget = 1.0 + r · (kK35FbRetentionMax − 1.0)   // kK35FbRetentionMax = 3.0
```

Raising the loop clip lets the resonant peak ring higher, back up above
the saturation, so the squelch returns *without* touching the grit. The
loop stays bounded because the saturated-region slope (0.25) is unchanged;
verified finite at res 1.0 / Drive 8 / r = 1.0.

Measured (LP 500 Hz, res 0.80, noise ~1 V), resonant peak-to-shoulder and
grit (median PSD 1.5–5 kHz) as retention goes 0 → 100 % at Drive 8:

| Mode | peak-to-shoulder | grit |
|------|------------------|------|
| OTA  | 9.7 → 19.5 dB    | −81.7 dB (flat) |
| K35  | 11.1 → 21.0 dB   | −76.9 dB (flat, +13 dB vs Drive-1 → grit kept) |

## Control

Retention is **baked in at 0.75**, not user-adjustable. It briefly shipped
as a context-menu slider during prototyping; after auditioning, 75 % was
chosen as the fixed value and the slider was removed.

- `static constexpr float kResRetention = 0.75f`, read in `modulate()` and
  branched by mode into `_clipThreshTarget` (OTA) / `_fbThreshTarget`
  (K35); slewed by `_clipThreshSlew` / `_fbThreshSlew` so mode/Drive
  changes don't zipper.
- No param, no menu entry, no JSON persistence — nothing to save because
  there is nothing to change. (`_filterMode` persistence is unchanged.)

## Out of scope

- No panel/param change; menu-only, like the filter-revision toggle.
- No makeup gain or resonance-frequency compensation.
- No CV control of retention.

## Verification

Headless resonance measurement (scratchpad `mf20_split_test.py`, driving a
`resRetention` state field through the vcv-headless host's `state` support),
Drive 8, retention sweep, both modes. Confirmed:
- Resonant peak-to-shoulder rises monotonically with retention (both modes).
- K35 grit (median PSD 1.5–5 kHz) stays flat across the sweep and ~13 dB
  above the Drive-1 reference → grit kept while resonance returns.
- OTA regression matches the pre-split build.
- `finite = True` at every setting, including res 1.0 / Drive 8 / r = 1.0.
