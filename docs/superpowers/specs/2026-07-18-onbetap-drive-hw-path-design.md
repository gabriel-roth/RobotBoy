# Onbetap — Drive: straight-to-hardware output gain

**Date:** 2026-07-18
**Status:** design approved
**Supersedes:** the drive-makeup portion of `2026-07-15-onbetap-dsp-spec.md` §5
**Related:** `docs/research/polivoks-emulations.md` §4, `docs/research/onbetap-worklog.md` Task 5 (tests 1a/1b)

## Problem

The Onbetap Drive control is documented (and intended) to behave like driving a
real Polivoks input: as you push it, the sound gets **louder and dirtier while
the resonant peak rings less** — the signature "drive fights resonance"
interaction (`polivoks-emulations.md:309-315`).

The "rings less" half is correct. The other half is inverted. Measured with a
tone held at cutoff (750 Hz), the module gets **quieter and cleaner** as Drive
rises — the opposite of the promise — under exactly the conditions the effect is
meant to be auditioned (moderate-to-high Q):

| Drive | res 0.70 level | res 0.70 THD | res 0.30 level | res 0.30 THD |
|------:|---------------:|-------------:|---------------:|-------------:|
| 0.0   | +16.5 dB       | 11.6 %       | +15.4 dB       | 7.9 %        |
| 0.5   | +8.3 dB        | 1.3 %        | +8.3 dB        | 1.2 %        |
| 1.0   | −0.7 dB        | 1.4 %        | −4.8 dB        | 1.4 %        |

(Levels are output RMS in dB re 1 V; THD is harmonic RMS ÷ fundamental. All
figures from the host-free measurement harnesses used during investigation.)

## Root cause: double compensation

Two things scale with the Drive knob, in opposite directions:

- **Input drive** `driveScale` — 0.25×…~16× into the core (`Onbetap.cpp:186`).
- **Output makeup** `makeup = (0.25/driveGain)^0.75 · kOutScale` — a ~27 dB
  *cut* across the sweep (`Onbetap.cpp:187`).

The makeup gain was designed to cancel the input drive so "sweeps don't just get
louder" (`2026-07-15-onbetap-dsp-spec.md:132-134`) and calibrated at **res = 0,
cutoff = 20 kHz** (filter wide open, sub-saturation), where the core output
grows roughly linearly with input and the two cancel to +1.35 dB.

But the **core already compensates level on its own**: the integrator states
clamp at their output-swing rails, which is a compressor — the authentic
*"natural compression between signal and self-osc"* the research names
(`polivoks-emulations.md:314`). At moderate/high Q the resonant peak pins the
core at its rails even at Drive = 0, so more input can't raise the level; it only
adds asymmetric-clip harmonics and chokes the ring.

The makeup gain then compensates a **second** time on top of the core's own
compression, overshooting into net attenuation. And because it shrinks the
signal below the knee of the output VCA stage (`9·tanhish(v/9)`,
`Onbetap.cpp:263`) — which is where most of the audible grit at low Drive
actually comes from — it *strips the dirt* on the way down. That is the measured
"quieter and cleaner."

Note the makeup's drive-dependence is **not** circuit-derived: real Polivoks
outputs are low-level and clones add a *fixed* output buffer (×11)
(`polivoks-emulations.md:64-65`). The drive-dependent curve is a module
invention, and the wrong one.

## Design: constant (buffer-style) output gain

Remove the Drive dependence from the makeup gain. Make it a **constant** output
buffer gain (the ×11 clone analog), leaving the core's rail clamping as the
sole, authentic level-compression mechanism.

Measured result of the constant-gain path (same tone-at-cutoff conditions):

| Drive | res 0.70 level / THD | res 0.30 level / THD | res 0.00 level / THD |
|------:|---------------------:|---------------------:|---------------------:|
| 0.0   | +16.5 dB / 11.6 %    | +15.4 dB / 7.9 %     | +10.3 dB / 2.0 %     |
| 0.5   | +17.3 dB / 16.8 %    | +17.3 dB / 16.7 %    | +17.3 dB / 16.5 %    |
| 1.0   | +17.6 dB / 4.4 %*    | +17.4 dB / 17.4 %    | +17.4 dB / 17.4 %    |

Every column now matches the promise:

- **Level holds** at moderate/high Q (core self-limits) and **rises** at low Q
  (+7 dB, then plateaus as the core reaches its rails) — both authentic.
- **Dirt rises** monotonically with Drive across the Q range (2 %→17 %).
  (*At res 0.70 the top of the sweep enters the chaotic self-oscillation region,
  where THD is non-monotonic; the useful range is unambiguously dirtier.)
- **Ring suppression** is unchanged — it was always the core's doing.

Drive = 0 is bit-identical to today (both paths give `makeup = kOutScale`), so
the existing Drive = 0 level calibration (worklog test 1a, −0.41 dB) is
preserved.

### Change surface

1. **Extract the gain mapping to a testable header.** Move the Drive→gain
   computation and its constants out of `Onbetap.cpp` into a new header
   `src/onbetap/drive.hpp`:

   ```cpp
   namespace onbetap {
   constexpr float kVoltsToCore = 1.f/2.4f;
   constexpr float kBaseTrim    = 0.4f;
   constexpr float kOutScale    = 20.5f;
   struct DriveGains { float driveScale, makeup; };
   inline DriveGains driveGains(float drive, float driveDb,
                                float headroom, float outDb) {
       float spanOct   = driveDb / 6.0206f;
       float driveGain = std::exp2(-2.f + spanOct * drive);
       float driveScale = driveGain * kBaseTrim * kVoltsToCore * headroom;
       float makeup     = kOutScale * std::exp2(outDb / 6.0206f); // constant
       return { driveScale, makeup };
   }
   }
   ```

   This puts the exact formula under test (rather than a copy) and removes the
   now-obsolete `kMakeupExp` constant entirely.

2. **`Onbetap.cpp::modulate()`** calls `onbetap::driveGains(...)` for
   `driveTarget`/`makeupTarget`; drops its local `kVoltsToCore`, `kBaseTrim`,
   `kOutScale`, `kMakeupExp` constants; updates the file-header comment block
   (lines 21-27) to describe the constant makeup.

3. Nothing else in the audio path changes: `driveScale`, the core, the FIR
   decimator, the tap select, the output VCA, and ring suppression are all
   untouched.

### Loudness caveat (accepted)

At high Drive the output runs hot (~+17 dB ≈ 7 V RMS, peaks clipping into the
9 V output VCA — the intended "ferocious/harsh" behavior). This is much louder
at high Drive than the current build. It is authentic and bounded (≤ 9 V peak,
within ±10 V rails). If a quieter default is ever wanted, the correct lever is a
**constant** trim — lower `kOutScale`, or the existing **Output trim** menu
slider (±12 dB) — never a Drive-dependent one. Default `kOutScale` is left at
20.5 to preserve the Drive = 0 calibration. No default level change in scope.

## Testing

The committed core tests (`tests/onbetap/test_onbetap.cpp`) operate on the
header-only filter core and do **not** exercise the makeup gain; none break. The
old "+6 dB" target (worklog test 1b) was a one-off measurement, not committed
code — and it encoded the very behavior being removed, so it is retired.

Add a new host-free test `tests/onbetap/test_drive_level.cpp` that mirrors the
module's per-sample glue (drive → 2× oversampled core → FIR decimate → tap →
makeup → DC block → output VCA) but calls the real `onbetap::driveGains`, and
asserts the corrected behavior at res ∈ {0.0, 0.30, 0.70}, tone at cutoff:

- **Does not get quieter:** `level(0.5) ≥ level(0.0) − 0.5 dB` and
  `level(1.0) ≥ level(0.0) − 1.0 dB`. (The old code fails this hard: −8 to −18 dB.)
- **Gets dirtier:** `THD(0.5) ≥ THD(0.0) + 2 percentage points`. (Old code:
  THD drops to ~1.3 %.)

These assertions fail on the pre-fix formula and pass on the constant-gain
formula, so the test is written first (TDD) against the current code, observed
red, then made green by the change.

## Documentation updates

- `docs/research/onbetap-worklog.md`: append a dated entry recording the
  double-compensation finding, the measurement tables above, and the retirement
  of the +6 dB target in favor of "level holds at high Q / louder at low Q,
  dirt rises with Drive."
- `2026-07-15-onbetap-dsp-spec.md` §5: add a one-line pointer noting the makeup
  gain is superseded by this design (constant, not `1/sqrt(driveGain)`), so the
  spec doesn't contradict the code.
- `Onbetap.md`: the shipped doc already states only ring-suppression and does
  not overclaim loudness, so it stays correct. Add one clause to the Drive bullet
  (`:15`) noting driving harder also adds harmonic grit, now that it's true.

## Out of scope

- Default output-level voicing (the hotness) — separate voicing decision.
- The max-drive aliasing gap (worklog test 2f) — pre-existing, unrelated.
- Any change to the core, resonance map, oversampling, or Vintage character.
