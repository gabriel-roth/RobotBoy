# Onbetap DSP spec — Polivoks VCF emulation

2026-07-15. Spec for the filter engine behind the existing Onbetap scaffold
(`src/Onbetap.cpp`, panel final, do not touch `res/Onbetap.svg`). Sources:
`docs/research/polivoks-local-materials.md` (factory + Erica + Shruthi + IME
circuit analysis), `docs/research/polivoks-web-literature.md` (Wasp DAFx paper,
device physics, Zavalishin TPT), `docs/research/polivoks-emulations.md`
(behavioral checklist, prior art). Citations below refer to those docs.

## 1. What we're modeling

The Polivoks VCF core is two К140УД12 programmable op-amps run open-loop as
current-controlled integrators (their internal \~30 pF compensation caps are the
only "capacitors"), closed into a two-integrator state-variable loop by six
resistors. Cutoff = op-amp gain-bandwidth ∝ Iset current. Resonance = a pot that
*removes* damping (BP output fed back to the summing node). All the character
comes from three stacked nonlinearities in the integrators themselves:

1. **Diff-pair tanh** at each op-amp input — linear window only ±\~2·V_T at the
   pins, ≈ ±2.4 V referred to stage outputs through the 47k:1k dividers.
   Slew limiting is the *same* phenomenon (rate ceiling = ω_u·window), so a tanh
   integrator reproduces both regimes with no extra machinery [web-lit §5].
2. **Rail clipping of the integrator states** (\~±10.5 V swing on ±12.5 V rails,
   asymmetric per unit). The Wasp DAFx paper shows clamping the *states* (not a
   post-saturator) is what makes high-resonance behavior right [web-lit §2].
3. **Per-unit asymmetry/offsets** (input offsets ×48 noise gain, uncompensated
   expo converter) — even harmonics, DC thump on sweeps, drift [local §7].

Target behaviors (the load-bearing items from `polivoks-emulations.md` §4):
clean-ish at low Q/level; ferocious under input drive; **drive suppresses
resonance**; self-osc onset \~2/3 of the resonance travel, approached in
harmonic steps, suddenly harsh at max; erratic wide-range self-oscillation
(sub-audio to ultrasonic); hard self-osc ≈ rail square, soft ≈ sine slightly
higher in pitch; nasal 6 dB/oct BP; audio-rate FM-friendly; low self-noise.

## 2. Core model (continuous time)

States `y1` (BP, stage-1 output) and `y2` (LP, stage-2 output), referred to
stage-output scale and normalised so the tanh window W = 1:

```
node = Gin·x + y2 + k·y1          // current sum into the 1k node, output-referred
ẏ1  = −ωc · N(node)              // stage 1: inverting integrator
ẏ2  = +ωc · N(y1)                // stage 2: non-inverting integrator
N(v) = W · tanhish((v − off) / W) + off·… // saturator, window W = 1, slight asymmetry
```

Linearised this is exactly the SVF `s² + k·ωc·s + ωc²` with **Q = 1/k**; taps:

- `LP = y2` (12 dB/oct; circuit inverts, we re-invert like Erica V2)
- `BP = y1` (6 dB/oct skirts, peak gain ≈ Q)
- `HP = −N(node)` (the saturated node signal = ẏ1/ωc — physical, gritty)
- `Notch = LP + HP`, `Peak = LP − HP` (extension modes, as in Elta/Filtomika)

Sign convention checked: `s²·y2 + k·ωc·s·y2 + ωc²·y2 = −ωc²·x`.

Amplitude scale calibration (physical → normalised): window 2.4 V = 1.0; rails
±10.5 V → clamp ≈ ±4.4; factory nominal core level 0.5 V → 0.21. VCV ±5 V input
maps through Drive (see §5) so that low drive ≈ nominal (clean), high drive
slams the window ×20 — matching "the core's nominal is only \~0.5 V" and Erica
accepting 20 Vpp [local §4.7, §6].

### Rail clamp — two flavors (context menu "Resonance limiting")

- **Hard (default, authentic factory/Erica):** clamp states to asymmetric rails
  `[−4.1, +4.4]` (asymmetry per §1.3). Self-osc at max resonance walks to the
  rails → square-ish, "suddenly harsh" [emulations §0, modularsynthesis].
- **Soft (Elta/Slocum diode mod):** soft tanh-knee clamp at \~±3.4 before the
  hard rail; self-osc stays sine-ish and sits slightly higher in frequency.

### Resonance / damping mapping

`k = damping = 1/Q`. Knob res ∈ [0,1] maps through a rev-log-style curve to
`k = kMin + (kMax − kMin)·(1 − res)^p` with kMax ≈ 1.02 (min resonance is
already Q ≈ 1 — the filter is never flat [local §3]), kMin ≈ −0.06 (guaranteed
oscillation at max), p tuned so k crosses 0 near res ≈ 0.72.

**Phase-lag term:** the УД12's extra internal poles subtract damping as ω_u
rises, so real units self-oscillate earlier at high cutoff and go unstable at
high cutoff + high res [local §3, §7.4]. Model: `k_eff = k − cLag·g²` (g =
prewarped gain, cLag ≈ 0.1, tune). This gives onset that *moves with cutoff* —
a documented behavior nobody in the open has modeled.

Self-oscillation seeds from the module's alternating ±1e-9 denormal dither
(house pattern), so it starts from silence like the hardware.

## 3. Discretization

TPT/ZDF trapezoidal integrators (Zavalishin ch. 4) with the two embedded
saturators handled by **per-sample gain linearisation** (Mystran-style, cheap
and non-iterative — house-compatible, no per-sample transcendentals in the
solve):

1. Predictor: evaluate both nonlinearity secant gains `n1 = N(node*)/node*`,
   `n2 = N(y1*)/y1*` at the previous-sample mid values (states as predictor).
2. Solve the *linear* ZDF SVF with those gains folded into the loop:
   `y1_mid = (s1 − g·n1·(Gin·x + s2)) / (1 + g·n1·(k_eff + g·n2))` (derived in
   implementation; denominator strictly positive for g·n ≥ 0, k_eff > −1/…),
3. One refinement pass: recompute n1, n2 at the solved mids, re-solve. (Fixed
   two-pass, no convergence loop; measured error is inaudible at 2× OS.)
4. Rail clamp applied to the updated states `s = 2·mid − s`, then the clamp
   flavor (hard clamp or soft-knee) — Wasp-style state clipping.
5. `tanhish` is a rational approximation (e.g. `x·(27+x²)/(27+9x²)` hard-limited
   to ±1) — no libm tanh in the audio path.

Oversampling: **2× default** (context menu 1×/2×/4×). Input linearly
interpolated between host samples; output decimated by 2-tap average (zero at
Nyquist). g computed for the oversampled rate. All rates derived from the host
sample rate — no hardcoded 44.1/48k.

Cutoff → g: `g = tan(π·fc/fs_os)` computed at modulate rate (2.5 ms), slewed
per-sample in the g domain (MF-20 pattern: no tan/exp2 in the audio path).
Cutoff clamp: [0.5 Hz, 0.49·Nyquist] (= 0.245·fs_os) — CV can close the filter to sub-audio
(hardware self-oscillates down to \~2 Hz) and open it ultrasonic (\~50 kHz on
hardware) [emulations §0].

## 4. Module behavior (`src/Onbetap.cpp`)

Follows the MF-20 wrapper pattern exactly: `modulate()` every 2.5 ms reads
params/CVs and computes targets; per-sample OnePoleSmoothers (5 ms) slew g,
k, drive gains; NaN `sanitize()` per modulate block; alternating-sign denormal
dither; poly up to 16 voices (`max` of L/R channel counts); true stereo — R
processed through its own filter when connected, mirrored from L when not.

- **Cutoff:** knob log2(20)..log2(20k) Hz (scaffold, unchanged); CV = 1 V/oct
  × attenuverter added in log2 domain. (The hardware's quasi-expo 0.6 V/oct
  converter [local §2] is a calibration detail, not a musical feature — we
  keep standard 1 V/oct. Vintage mode supplies the drift/inaccuracy instead.)
- **Resonance:** knob [0,1] + CV/5V × attenuverter, clamped, → k map (§2).
- **Drive:** knob [0,1] + CV/5V × attenuverter → input gain, exponential map
  across ≈ −12 dB…+24 dB (36 dB span, default 0 = −12 dB ≈ hardware "advised
  −10 dB input" [emulations §3.4 Bareille]). Constants behind context-menu
  tuning sliders (§6).
  **Superseded (2026-07-18):** the "partial output makeup gain `1/sqrt(driveGain)`
  so sweeps don't just get louder" is removed — the makeup is now a *constant*
  buffer gain. The core's rail clamping already compresses level, so a
  drive-dependent makeup double-compensated (Drive made the module quieter and
  cleaner). See `2026-07-18-onbetap-drive-hw-path-design.md`.
- **Mode:** 5-position snap knob LP/BP/HP/Notch/Peak (scaffold). All taps
  computed from the one solved core; mode switch crossfades over \~5 ms to
  avoid clicks (Erica V2 behavior; the factory's DC thump belongs to Vintage).
- **Output:** non-inverted (Erica V2 precedent). ±5 V nominal.

## 5. Character: Tamed vs Vintage (existing menu toggle)

Both modes share the core; Vintage adds the per-unit/thermal misbehaviors
[local §7]. All Vintage randomness is **deterministic per channel** (fixed
constants / seeded from channel index, not RNG-per-instance) so tests and
VCV↔MM comparisons are reproducible.

| Aspect | Tamed | Vintage |
|---|---|---|
| Cutoff | stable, calibrated | slow OU random-walk drift, \~±0.3 oct over minutes, decorrelated L/R |
| Integrators | matched | ω mismatch (+7% / −5% style fixed constants, different L vs R) |
| Asymmetry/offset | small fixed asymmetry (even harmonics, always present) | larger offset, scales with cutoff → DC thump on fast sweeps, per-unit clip asymmetry |
| Self-osc onset | fixed map + phase-lag term | onset trim wobbles with drift; more erratic at high cutoff |
| Mode switch | crossfaded, clickless | small DC step allowed through (factory behavior) |

## 6. Context menu (additions to existing Character items)

- **Resonance limiting:** Hard (default) / Soft — §2.
- **Oversampling:** 1× / 2× (default) / 4×.
- **Tuning** (slider items, persisted, defaults = spec values — these exist so
  constants can be tuned by ear without rebuilding; may be culled once tuned):
  drive range (dB span), core headroom (window scale vs input), self-osc onset
  trim, output level trim.

All menu state persists via dataToJson/dataFromJson alongside `vintageDrift`.

## 7. Code layout & tests (house style)

- `src/onbetap/OnbetapFilter.hpp` — header-only nonlinear core: `processG(in,
  g, kEff) → {lp, bp, hp}` + notch/peak composed by caller; hard/soft clamp
  setting; asymmetry/offset/mismatch params; `stateFinite()`; static
  `cutoffToG(fc, fsOs)`. Pure — no Rack includes.
- `src/onbetap/engine.hpp` — VoiceEngine (L+R filter + smoothers + drift
  state), EnginePool[16] by value (MF-20 pattern). Reuses
  `../mf20/dsp_utils.hpp` (OnePoleSmoother, smootherAlpha).
- `src/Onbetap.cpp` — module wrapper + widget/menu (positions untouched).
- `tests/onbetap/test_onbetap.cpp` (+ `test_engine.cpp` if needed) — plain
  g++ lane, run via `tests/run.sh`:
  - linear regime: sine sweeps confirm fc placement, LP 12 dB/oct, BP 6 dB/oct
    skirts, HP/notch/peak shapes; Q ≈ 1 at res = 0 (small hump, never flat)
  - self-osc: silence + res = 1 → sustained bounded oscillation near fc; hard
    mode high crest/square-ish vs soft mode sine-ish and slightly higher pitch
  - onset: oscillation appears near res ≈ 0.72 at 1 kHz, earlier at 10 kHz
    (phase-lag term)
  - **drive suppresses resonance:** BP peak (or ringing) at fixed res shrinks
    as input level rises
  - stability: max res + max drive + cutoff sweeps at 44.1/48/96k → states
    finite, output bounded
  - determinism: two runs bit-identical; Vintage constants fixed
- MetaModule: no new registration work (Onbetap already in
  `metamodule/CMakeLists.txt`, `plugin-mm.json`, `init_RobotBoy`). Verify CPU
  with the headless simulator (`--preset headless`, load %); if 2× OS is too
  hot on MM-class targets, the OS menu setting is the escape valve.

## 8. Acceptance

1. `tests/run.sh` green (existing + new Onbetap tests).
2. VCV build loads; all five modes audibly correct; self-osc behaves per spec.
3. vcv-headless + MM headless render the same patch with closely matching
   output (same DSP code both sides); MM CPU load recorded in the worklog.
4. Behavioral checklist items 1–10 of `polivoks-emulations.md` §4 demonstrably
   present (each mapped to a test or a rendered-audio check).
