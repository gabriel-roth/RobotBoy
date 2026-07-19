# Vespid — Drive remap: 2×–64× so the knob works the clipping range

**Date:** 2026-07-19
**Status:** implemented (autonomous session — decisions recorded inline)
**Follows:** `2026-07-15-yellowjacket-dsp-design.md` (Drive = input gain 1×–8×)

## Problem

With a standard ±5 V VCV signal, the original Drive law `gain = 2^(3·d)`
(1×–8×, 18 dB) was badly staged against the core's headroom. Measured THD
on raw LP (Screaming, fc 1 kHz, 110 Hz sine at 5 V peak, high accuracy):

| drive01 | gain | THD |
|---|---|---|
| 0.00 | 1× | 0.02 % |
| 0.33 | 2× | 0.06 % |
| 0.45 | 2.5× | 5.5 % |
| 0.67 | 4× | 18 % |
| 1.00 | 8× | 17–32 % (rising with ρ) |

Three properties combined into "the knob does nothing, then it's all the
same rasp":

1. **Saturation onset is at ≈12.5 V equivalent input** (the nInv = 6.7
   summing divider gives the core huge headroom vs VCV levels), so the
   bottom half of the knob was pure clean volume.
2. **Onset is a cliff** — THD goes 0 % → 18 % between gains 2.5 and 4.
3. **Past the rails the circuit runs out of vocabulary** — fundamental
   pins at ≈6.8 V and THD plateaus ≈21 %. What *does* keep evolving at
   higher gain is the harmonic profile: h2 falls from −14 dBc (8×) to
   −40 dBc (64×) as the clipping goes symmetric — an asymmetric→odd
   morph, not more THD.
4. Resonance adds no drive-dependent grit below onset (THD 0.02 % flat
   across ρ 0.3–0.9 at gains ≤ 2); the ever-present rasp users hear is
   the resonance/diode character, independent of Drive.

The design doc's acceptance item "THD at drive extremes" was never
implemented in wasp_ref.py or golden.json, so this staging was never
characterized before ship.

## Design (user-selected: option A + 2× pre-gain)

```
gain = 2 · 2^(5·d) · trimGain        // 2×–64×, 30 dB span
```

- **2× fixed pre-gain**: drive-0 stays clean at 5 V (THD 0.06 %) but sits
  just under the onset, so the knob starts biting almost immediately.
- **30 dB span (2^(5·d))**: onset (≈2.5× effective) lands ≈6 % up the
  knob; the remaining travel covers the full clipping range including the
  asymmetric→odd morph out to 64×, which the old 8× ceiling never reached.
- `engine.hpp` drive smoother seeds/reset move 1.0 → 2.0 to match the new
  at-rest gain (no sweep-in on voice reset).
- Input trim (±12 dB) still multiplies into the same product, shifting the
  onset point around the knob.

Rejected alternatives: (B) larger fixed pre-gain so 5 V is dirty at
drive 0 — changes the default clean character; (C) Onbetap-style
drive-following push into an added output saturator — new distortion
source the Wasp model doesn't have.

## Consequences

- Voicing change for existing patches: same knob positions are hotter
  (+6 dB at drive 0, up to +18 dB at max). Output level menu (±12 dB) can
  rebalance.
- Loudness still rises with drive until the rails (~6.8 V raw); no
  compensation pair, per the Onbetap rule ("more Drive must always mean
  more level or more dirt").
- Core-level staging contract pinned by the drive-staging test in
  tests/vespid/test_wasp_filter.cpp: effective gain 2× is clean (<1 %
  THD), 64× is heavily clipped (>10 % THD, bounded, finite).
