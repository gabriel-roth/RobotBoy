# Onbetap 4x decimator upgrade — design

**Date:** 2026-07-18
**Status:** approved (user, 2026-07-18)
**Branch:** main (small self-contained change, per user)

## Problem

Onbetap's 4x oversampling path still decimates with the original 4-sample
boxcar average (`src/Onbetap.cpp` `processSide`, the `else` branch). The 2x
path got a proper 13-tap decimation FIR (`DecimFir13`,
`src/onbetap/engine.hpp`) during the Task-5 redesign; the 4x path was
explicitly deferred ("out of this task's scope — see worklog"). The boxcar's
worst attenuation in the band that folds into the audible range is about
−11 dB (at 72 kHz), so 4x currently combines more substeps with a worse
decimator. The user wants to compare 2x and 4x "like against like": same
decimation quality, so the comparison isolates the oversampling factor.

Measured baseline (worklog, Task 5, 5 kHz sine / max drive / LP 20 kHz):
worst non-harmonic spur −29 dB at 2x, −35 dB at 4x (boxcar). Top-octave
droop at 2x after the FIR swap: −1.76 dB at 18 kHz (2 dB budget).

## Approach (chosen: two-stage cascade)

Decimate 192k→48k in two stages:

1. **Stage A (new):** short Kaiser-windowed FIR at fsOs = 192 kHz,
   decimating 192k→96k. Target ~9 taps; exact length/cutoff/beta chosen by
   measurement during implementation (scipy.signal.firwin, same workflow as
   DecimFir13). Design criteria, at the 48 kHz-host case:
   - ≥40 dB attenuation over 72–96 kHz. This is the only band that folds
     into the audible 0–24 kHz after both decimations. (48–72 kHz folds to
     24–48 kHz, which stage B's stopband already covers.)
   - ≤0.1 dB droop at 20 kHz. The transition band (24k→72k) is a quarter
     of the sample rate, so this is easy at ~9 taps.
2. **Stage B (existing):** the per-voice `DecimFir13` instances
   (`firLp/Bp/Hp`, currently unused on the 4x path) run at 96 kHz,
   decimating 96k→48k, exactly as on the 2x path.

Because stage B *is* the 2x decimator, the 4x passband matches the 2x path
by construction. Residual passband difference is the linear-interp
upsampler's droop (~1.4 dB at 18–20 kHz at 2x vs ~0.35 dB at 4x), which
favors 4x slightly; the upsampler remains out of scope (still the deferred
"later task").

Rejected alternative: single-stage FIR at 192 kHz. The 20k→28k transition
at a 192 kHz rate needs ~50+ taps for 40 dB (Kaiser estimate) — about 3x
the MAC cost of the cascade, plus a fresh passband design to tune, for no
quality advantage.

Coefficient convention: fixed coefficients designed for the 48 kHz-host
case (fsOs = 192 kHz); at other host rates the response scales
proportionally. Same convention DecimFir13 already uses.

## Implementation shape

- New struct in `src/onbetap/engine.hpp` mirroring `DecimFir13`: fixed
  `h[]`, delay line `z[]`, `push()`, `reset()`, plus a comment block
  documenting the folding-band math and design provenance.
- `OnbetapVoice` gains six stage-A FIRs (L/R × lp/bp/hp), ~200 bytes/voice.
  `reset()` and `sanitize()` clear them alongside the existing FIR state.
- `processSide` (`src/Onbetap.cpp`) becomes three explicit branches:
  - **4x:** 4 substeps; every substep pushes core outputs into stage A;
    substeps 2 and 4 push stage A's output into stage B; substep 4's
    stage-B output is kept.
  - **2x:** unchanged, byte-identical.
  - **1x:** single substep, no decimation filtering, unchanged.
- Runtime oversampling switches leave ≤13 samples of stale FIR state, same
  as today's 1x↔2x switch — harmless, no special handling.
- No new error handling: NaN recovery already flows through `sanitize()`.

## Success criteria

> **Amended 2026-07-18 (criteria 1–2), during Task 4.** The original
> absolute gates were anchored to Task 5's measurements, which predate the
> baked drive voicing (span 30 → 36 dB, grit 3.5 dB, commit 429e1df). At
> today's max drive the output VCA rail-clips at 9 V *at host rate,
> downstream of the decimator* — all decimator variants measure within
> 0.3 dB of each other there (≈−14 dB, dominated by the VCA's 25 kHz
> harmonic folding to 23 kHz), so the max-drive scenario no longer probes
> the decimator at all. Likewise the 18 kHz absolute droop is dominated by
> the `kCLag` phase-lag term (tuned at 2x; kEff differs between fsOs 96k
> and 192k at the same cutoff) — a pre-existing core property, not
> decimation. Criteria 1–2 are therefore restated as *relative* gates that
> measure what this change actually controls:

1. At a decimator-sensitive operating point (5 kHz sine, LP 20 kHz, res 0,
   the highest drive at which the output VCA stays clearly below the 9 V
   rail, peak < 8 V), the cascade's worst non-harmonic spur must be
   strictly better than BOTH the old 4x boxcar's and the 2x path's, same
   harness, same scenario. Report magnitudes.
2. 4x droop at 18 kHz (LP cutoff 20 kHz, drive 0, res 0) strictly better
   than the old 4x boxcar's, with the residual 4x-vs-2x difference
   attributed (kCLag) and documented in the worklog as a known,
   pre-existing 2x↔4x voicing difference — flagged to the user as a
   possible follow-up, out of scope here.
3. 2x and 1x paths bit-identical to before (verified on a test render).
4. `tests/run.sh` unit lane green, including a new unit test: stage-A DC
   gain ≈ 1 and a low-frequency sine passes the composed 4x path at
   unity-ish gain.
5. VCV↔MM headless identity at 4x (JSON `{"oversample":4}` override, method
   documented in the worklog).
6. Cost benchmark re-run; new 4x/2x ratio recorded in the worklog
   (baseline: 1x 37 ns / 2x 97 ns / 4x 160 ns per stereo host sample on
   the dev Mac, ratio 1.66; expect the ratio to widen somewhat).

## Out of scope

- The linear-interp upsampler (deferred "later task" from Task 5).
- Any change to the oversampling default (stays 2x).
- The 1x and 2x paths.
- MetaModule real-hardware CPU measurement (separate open checklist item).
