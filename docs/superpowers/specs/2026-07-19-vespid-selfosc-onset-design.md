# Vespid — Self-oscillation onset: noise seed + 30 kHz fPole floor

**Date:** 2026-07-19
**Status:** implemented (autonomous session — decisions recorded inline)
**Follows:** `2026-07-19-vespid-input-calibration-design.md`

## Problem

From silence at rho=1, Screaming's self-oscillation grows exponentially out
of the module's denormal-prevention dither (±1e-9 V). Growth rate scales
with cutoff and with |kC2|, so onset was musical only at high cutoffs.
Measured time to |BP| > 1 V from silence (Screaming, rho=1):

| fc | 60 kHz, 1e-9 seed | 80 kHz, 1e-9 seed |
|---|---|---|
| 500 Hz | 3.4 s | 5.8 s |
| 1 kHz | 0.69 s | 1.07 s |
| 2 kHz | 0.17 s | 0.25 s |
| 200 Hz | never | never |

A hardware A-124 starts screaming near-instantly: its noise floor is
~5 decades above 1 nV, and exponential growth pays for every decade.

## Changes

1. **Noise seed 1e-9 → 1e-4 V** (`_dither`, Vespid.cpp). Physically
   motivated (~0.1 mV circuit noise floor), inaudible (−94 dBV, sits at
   Nyquist, killed by the output paths), still denormal-preventing and
   bit-reproducible VCV vs MM. Cuts onset ~40% everywhere.
2. **Inverter-bandwidth floor 60 → 30 kHz** (slider min, JSON clamp, MM
   menu now {30, 60, 80, 120, 220}). Lower fPole = more negative kC2 =
   faster growth and a wider oscillating fc range. **Tame floors its
   effective fPole at 60 kHz in modulate()** — it free-runs below
   ~55–60 kHz (fitted_constants.md), and that promise must survive any
   menu setting.

Measured with both changes (Screaming, rho=1, onset / sustained amp):

| fc | fPole=30 kHz | fPole=60 kHz |
|---|---|---|
| 300 Hz | 2.9 s / 2.63 V | never |
| 500 Hz | 0.71 s / 2.68 V | 1.9 s / 2.65 V |
| 1 kHz | 0.16 s / 2.75 V | 0.38 s / 2.70 V |

Sustained amplitude stays in family with the golden 2.66 V; pitch sane.
Tame at its 60 kHz floor with the 1e-4 seed: tail exactly 0 V (no free-run).

## Consequences

- At the 30 kHz floor, Screaming at rho=1 starts screaming in well under a
  second for fc ≥ 500 Hz and reaches down to ~300 Hz cutoffs. The default
  80 kHz keeps the hardware-fit behavior.
- fc ≈ 200 Hz still never self-oscillates — authentic (H1's pole raises
  damping at low fc; the hardware self-oscillates at mid/high cutoffs).
- Guards pinned by test 12 in tests/vespid/test_wasp_filter.cpp:
  Screaming @30 kHz onsets < 1 s at fc=1000 from the 1e-4 seed; Tame at
  its 60 kHz floor never free-runs.
- The 220 kHz slider ceiling (just past the ~218.1 kHz sustain threshold)
  is unchanged — max slider = long ring-out but no sustain; a possible
  future cap at 200 kHz is noted but not applied.
