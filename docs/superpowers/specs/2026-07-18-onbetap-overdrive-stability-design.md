# Onbetap — deep-overdrive stability: gated state leak + saturator residual slope

**Date:** 2026-07-18
**Status:** implemented (autonomous session — decisions recorded inline)
**Follows:** `2026-07-18-onbetap-drive-grit-design.md`,
`docs/research/onbetap-drive-resonance-investigation.md`
**Repro patch:** `\~/Desktop/test-patches/6.vcv` (Onbetap: span 36 persisted,
res 0.68, onset trim +0.0441, Drive maxed, VCO saw, cutoff swept low)

## Problem

User report: *"at low cutoff values, when the signal is filtered down a lot,
at high values of drive the signal disappears."* Investigation found **two**
distinct deep-overdrive pathologies in the core:

1. **Subsonic burst / note swallow** (the patch's actual symptom). At deep
   drive the huge node swing swamps the first stage's secant gain
   (`n1 = sat(node)/node → 0`), which collapses the loop's damping term
   (`D = 1 + g₁n₁(kEff + g₂n₂) → 1`); the resonant mode comes unhooked and
   rings at \~cutoff. With cutoff swept subsonic that is a rail-scale
   20–40 Hz rumble (measured 7.3 V RMS) that replaces the audible note
   (which itself halves at span 36). Confirmed **not** numerics (8-pass
   converged solve is bit-identical) and **not** the asymmetry DC (survives
   a symmetrized core). Onset ≈ node swing ≥ \~8 core units; the shipped
   default span 30 reaches 7.9 at ±5 V, so the default sits just below it —
   span 36+ (persisted in older patches) is inside it.

2. **Rail-pin absolute silence** (found first, needs hotter drive: \~span 48
   + 10 V). Asymmetric-clipping rectification DC drags both integrator
   states to the negative rail (−4.1), where the saturator was exactly flat
   (`tanhish` hard-clamps at |x| ≥ 3, derivative 0) → zero small-signal gain
   → true silence until Drive is backed off. A real diff pair chokes
   asymptotically, never completely — the absolute dead zone is a model
   artifact.

**Rejected fix — input pre-clipper:** prototyped and measured byte-identical
to no-fix on both pathologies. The loop's feedback only interacts with the
input near its zero crossings (within \~±3.7 units); clipping peaks moves no
crossing, so the saturated node sees the same waveform. Input-level
protection cannot address either mechanism.

**Rejected fix — fixed-frequency state leak:** kills the burst but also
kills *legitimate* self-oscillation at low cutoff (fc 40/80 self-osc → 0
even at a 4 Hz pole). Burst and legit self-osc share a frequency range; the
distinguishing variable is input drive depth, not frequency.

## Design

Two guards in `OnbetapFilter.hpp`, both inert in normal operation:

1. **Drive-gated state leak.** After the state update, before the rail
   clamp:

   ```
   gate = min(|xin| / 8, 1)          // xin = kGin·in + off — input only,
   s *= 1 − leak · gate              // feedback not in the gate
   ```

   `leak` is per-substep, set by the wrapper as `2π·kLeakPoleHz/fsOs`
   (`kLeakPoleHz = 15`), i.e. a \~15 Hz integrator-loss pole **at full
   gate**. Zero input → zero gate → self-oscillation untouched at every
   cutoff. At mid drive (span 30, drive 0.5, ±5 V: |xin| ≈ 1.4) the gate is
   \~0.18 → effective pole \~2.7 Hz — inaudible. Restores drive-dependent
   damping exactly where the secant-gain collapse removes it.

2. **Saturator residual slope.** `sat()` keeps a `kSatLeak = 0.05` slope
   beyond its former hard-flat clamp (|v| > 3·window), so small-signal gain
   never reaches exactly zero — the asymptotic choke a real diff pair has.
   `tanhish` itself stays exact: the output VCA's 9 V bound depends on it
   (adding the slope there measured 10 V peaks — rejected).

`setLeak()` is called from `modulate()` (fsOs varies with the oversampling
menu). **Amendment (same day, user request):** the pole is boosted below
`kLeakCornerHz = 80` by `clamp(80/fc, 1, kLeakBoostMax = 4)` — up to a 60 Hz
pole at fc 20 — after residual by-ear dropouts on certain low notes;
unchanged at fc ≥ 80, and the zero-input gate still protects self-osc. Host-free default is `leak = 0` (off) so the core class alone is
unchanged unless configured; the committed tests mirror the wrapper's
configuration.

## Measured (test_overdrive_stability.cpp; conditions in the test)

| Check | Before | After |
|---|---|---|
| Burst: note at drive 1.0 (patch conditions) | 0.32 V (halved from 0.85) | 1.10 V, monotone rising |
| Burst: rumble RMS at drive 1.0 | 7.30 V | 0.78 V |
| Rail-pin output (span 48, 10 V) | 0.000 V (silence) | 3.46 V |
| Self-osc RMS fc 750 / 80 / 40 | 8.62 / 8.39 / 4.68 | 8.63 / 8.41 / 4.69 |
| Resonant ring (res 0.6, fc 750) | 9.39 V | 9.39 V |
| Drive-grit criteria (res 0.6, drive 1) | 18.4 dB / 31.1 % | 18.4 dB / 31.8 % |
| Output peak bound | 9.00 V | 9.00 V |

Full committed suite green (181 checks), including the Drive-level and
Drive-grit guards and the stability torture test.

## Trade-offs (accepted)

- The subsonic burst was arguably adjacent to documented hardware behavior
  (erratic infrasonic self-osc), but it is drive-*created* oscillation —
  inverting the documented "drive suppresses resonance" — and perceptually
  it replaces the audible signal with inaudible rumble. Removed.
- MM-vs-VCV bit-parity is preserved (deterministic; same code both builds),
  but output differs from pre-fix builds at deep drive — headless A/B
  against old renders will show differences there by design.

## Out of scope

- The user's patch keeps span 36 by choice; no span/default changes.
- No change to drive mapping, grit push, makeup, taps, oversampling.
