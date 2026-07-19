# Onbetap: rate-independent kCLag — design

**Date:** 2026-07-18
**Status:** approved (user directive: "make kCLag rate-independent", autonomous execution)
**Branch:** main (small self-contained change, same pattern as the 4x-decimator work)

## Problem

The phase-lag damping correction is computed per sample as
`kEff = kBase − kCLag·g²/(1+g²)` (`src/Onbetap.cpp:313`) from the slewed
prewarp gain `g = tan(π·fc/fsOs)`, and `fsOs = sampleRate·oversample`. The
term models a *hardware* property — self-osc onset arrives earlier at high
cutoff (calibration target 2b, Δonset ≥ 0.03 between 8 kHz and 200 Hz) — but
using `g` as the fc proxy accidentally made it depend on the oversample
setting: at fc = 20 kHz the correction is 0.093 at 2x (fsOs 96k, g = 0.767)
vs 0.026 at 4x (fsOs 192k, g = 0.340), so kEff is 0.972 vs 1.039. The 4x
path runs audibly more damped in the top octave and self-oscillates later at
high cutoff — independent of the decimator quality, and it colors any
2x-vs-4x listening comparison (worklog, 2026-07-18 4x-decimator entry).

## Approach

Make the correction a pure function of fc, evaluated at the **reference
rate the constant was calibrated at**: fsOs_ref = 96 kHz (2x OS of a 48 kHz
host — every Task 5 calibration measurement ran there).

1. New host-free helper in `src/onbetap/engine.hpp`, `namespace onbetap`:

   ```cpp
   // Phase-lag damping-correction factor, a pure function of cutoff.
   // Evaluated at kCLagRefFsOs — the oversampled rate the kCLag constant
   // was calibrated at (Task 5: 2x OS, 48 kHz host) — so the correction,
   // and with it kEff, self-osc onset, and top-octave damping, no longer
   // depend on the oversample setting or the host rate.
   inline constexpr float kCLagRefFsOs = 96000.f;
   inline float cutoffLagCorr(float fcHz) {
       float gr = OnbetapFilter::cutoffToG(fcHz, kCLagRefFsOs);
       return gr * gr / (1.f + gr * gr);
   }
   ```

2. `modulate()` folds the correction into the slewed k target
   (`v.kTarget = k − kCLag * onbetap::cutoffLagCorr(fc)`), using the same
   per-voice, drift-inclusive `fc` already computed there.

3. `process()` drops the per-sample term:
   `float kEff = std::max(kBase, -0.31f);` where `kBase` is the slewed
   kTarget (the −0.31 denominator-guard floor stays, applied to the slewed
   value per sample exactly as today).

kCLag stays 0.25 in `src/Onbetap.cpp`; only what it multiplies changes.
Update the one-line comment at its definition (src/Onbetap.cpp:30) and the
"phase-lag term applied per sample from slewed g" comment at the kTarget
assignment (src/Onbetap.cpp:182).

### Rejected alternatives

- **Per-sample reference-rate correction** (convert slewed g back to fc via
  atan, re-prewarp at 96k): adds transcendental math per sample per voice to
  preserve a smoothing nuance nobody can hear — the correction already only
  gets fresh fc data at modulate rate (2.5 ms), and kSlew smooths the folded
  target with the same 5 ms alpha the old path's g-slew gave it.
- **Scaling kCLag by oversample ratio**: only first-order-corrects (tan is
  nonlinear near the top octave); the reference-rate evaluation is exact and
  simpler to reason about.

## Behavior changes (all intentional, to be documented in the worklog)

- **4x:** correction now matches 2x at every cutoff → kEff identical → same
  top-octave damping and self-osc onset as 2x. Brighter than before at high
  fc. At fc > 23.5 kHz (reachable only at 4x; 2x clamps there) the
  correction saturates at its 23.5 kHz value via cutoffToG's own fc clamp —
  conservative, since kCLag was never calibrated beyond 20 kHz.
- **1x:** correction previously *larger* (g at fsOs 48k) → 1x becomes
  slightly less damped at high fc, also now matching 2x.
- **2x at 48 kHz host:** steady-state kEff numerically unchanged (same
  tan(π·fc/96000) expression); only the smoothing path differs (correction
  now rides kSlew instead of deriving per sample from gSlew) → transient
  differences during cutoff sweeps and the first smoother settle only. NOT
  bit-identical; must converge (see criteria).
- **Other host rates:** voicing now host-rate-independent too (a 96 kHz
  host previously got a different correction; now it matches the calibrated
  48 kHz voicing). Consistent with how every other baked constant was
  calibrated.

## Success criteria

1. Unit test (host-free): `onbetap::cutoffLagCorr` matches the old 2x/48k
   expression `g²/(1+g²)`, `g = cutoffToG(fc, 96000)`, to 1e-6 across fc
   {20, 200, 1k, 5k, 8k, 18k, 20k, 23.5k} Hz — pins "2x voicing preserved"
   and (by the function having no fsOs input) rate-independence.
2. Existing `tests/run.sh` lane stays green (law-guard tests compute their
   own kEff at 2x/48k steady state and are unaffected by construction).
3. Render check, 2x defaults, Task-1 input: vs `base_2x.wav`, the final
   1 s max abs diff ≤ 1e-4 V (steady-state convergence; early-transient
   divergence from the smoothing-path change is expected and unbounded by
   this gate).
4. Measured droop@18k (Task-4 harness, updated kEff formula): os2 itself
   moves ≤ 0.15 dB from −2.33 dB, and the os2↔os4cas gap shrinks by the
   kEff-attributable amount — 0.55 ± 0.15 dB, the linearised-SVF
   prediction for ΔkEff = 0.067 at fc 20 kHz (|H(18k)| with k 0.972 vs
   1.039 at r = 0.9).

   > **Amended 2026-07-18, during Task 3.** Originally "gap ≤ 0.6 dB" — a
   > guess that wrongly attributed the whole pre-change 1.42 dB gap to
   > kEff. Measured closure was 0.54 dB (1.42 → 0.88), matching the 0.55 dB
   > linearised prediction to 0.01 dB: the fix delivered exactly what kEff
   > controls. The residual ≈0.88 dB at this extreme corner (fc = 20 kHz,
   > measured at 18 kHz) is bilinear prewarp shape difference between
   > fsOs 96k and 192k plus the upsampler/decimator chain — inherent to
   > the oversampling ratio, out of scope here (see Out of scope), and
   > shrinking rapidly at lower cutoffs as (fc/fsOs)² falls.
5. VCV↔MM parity at 4x re-verified ≤ 1e-4 V (Task-5 method).

## Out of scope

- kCLag's value (0.25) and the onset calibration itself.
- The linear-interp upsampler (still deferred); the decimators.
- The per-OS cutoff ceiling (fc clamp at 0.245·fsOs in cutoffToG) — a
  pre-existing, separate behavior.
