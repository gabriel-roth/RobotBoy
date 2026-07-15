# Onbetap DSP — worklog

Working notes for the autonomous DSP build session (2026-07-15). Newest entries at the bottom.

## 2026-07-15 morning — setup and research

- Working strictly on `worktree-polivoks` (`/Users/gabrielroth/Dev/RobotBoy/.worktrees/worktree-polivoks`). No changes to main or other worktrees.
- Copied `resources/polivoks-sources/` (schematics, Erica DIY docs, Shruthi Polivoks board PDF, IME mk2 quickstart) from the main checkout into this worktree per instructions. Left untracked (\~15 MB of binaries).
- Surveyed the scaffold (`src/Onbetap.cpp`): stereo in/out, Cutoff (log2 Hz knob 20–20k, default 750), Resonance, Drive (all three with CV input + attenuverter), MODE_PARAM 5-position snap knob (LP/BP/HP/Notch/Peak), `vintageDrift` bool persisted via JSON with a Tamed/Vintage context menu. Panel is final — not to be touched.
- House DSP style established by MF-20 (`src/mf20/`): header-only filter core (TPT/ZDF 2-pole, closed-form piecewise-linear nonlinear solve, no iteration), `engine.hpp` voice pool (16 voices by value), modulate() at 2.5 ms with per-sample OnePoleSmoother slews in the g-domain (no per-sample tan/exp2/sqrt), deterministic alternating-sign denormal dither, NaN sanitize per modulate block, `processVCVG` ±5 V ↔ ±1 normalised convention (kVCVScale 0.2), mono-input R-mirror optimization. Host-free unit tests in `tests/mf20/*.cpp` run by `tests/run.sh`.
- Test lanes available: (1) `tests/run.sh` g++ DSP tests; (2) vcv-headless host (`~/Dev/vcv-headless`) WAV-in/WAV-out on the built VCV plugin; (3) MetaModule headless simulator (`~/Dev/metamodule/simulator`, `cmake --preset headless`, plugin compiled in as built-in via `-Dext_builtin_brand_*` cache vars — never edit ext-plugins.cmake).
- Dispatched three research agents: local schematic/doc analysis, web literature (DAFx paper etc.), existing emulations (open-source code, hardware clones). Outputs land in `docs/research/`.

## 2026-07-15 — Task 5: calibration against acceptance targets

Measured every acceptance target via `vcv-headless` (installed plugin, render →
Python/numpy FFT+RMS analysis) plus one host-free C++ scratch harness for the
one case headless structurally cannot exercise (L/R stereo decorrelation —
see below). All scratch scripts/specs/WAVs live under
`/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/168cdb10-1fe9-49fd-b698-5e1aa42f219f/scratchpad/onbetap-cal/`
(not committed).

### Calibration results

| Target | Measured before | Constant changed | Measured after |
|---|---|---|---|
| 1a. 1 kHz sine, 10 Vpp, drive=0, res=0, LP, cutoff=20 kHz: output within ±2 dB of input | −5.76 dB | `kOutScale` 10.5 → 20.5 | **−0.41 dB** (pass) |
| 1b. Same, drive=1: output RMS ≤ +6 dB over drive=0 case | +9.33 dB | `kOutScale` (above) + new `kMakeupExp` (0.5 → 0.75, replacing a plain inverse-sqrt makeup curve) | **+1.35 dB** (pass) |
| 1b. THD proxy: harmonic RMS (>3 kHz) > −20 dB rel. fundamental | −16.66 dB | (same as above) | **−17.53 dB** (pass, unaffected materially) |
| 1c. Self-osc peak (no input, res=1, cutoff=1 kHz): 8–10 V | 9.0 V | none (already clamped by the output VCA stage regardless of `kOutScale`) | **9.0 V** (pass) |
| 2a. Self-osc onset (res sweep 0→1 over 20 s, cutoff=1 kHz): res 0.65–0.80 | 0.7625 | none | **0.7625** (pass) |
| 2b. Onset ≥0.03 earlier at 8 kHz vs 200 Hz cutoff | 0.7 (8 kHz) / 0.8325 (200 Hz), Δ=0.1325 | none (`kCLag`=0.25 already sufficient) | **Δ=0.1325** (pass) |
| 2c. Drive suppresses resonance (BP, res=0.6, 1 kHz@fc): loud (8 V) gain ≥6 dB below quiet (0.5 V) gain | quiet 20.93 dB, loud 2.24 dB, Δ=18.69 dB | none (`kBaseTrim`=0.4 already sufficient) | **Δ=18.69 dB** (pass) |
| 2d. Hard vs Soft self-osc: hard crest < soft crest | — | none (menu toggle only, both defaults kept) | hard crest **1.044**, soft crest **1.052** (pass); freq hard **308 Hz**, soft **362 Hz** (soft sits higher, matches hardware lore) |
| 2e. Vintage wander: std of per-second self-osc pitch estimates, 0.2%–3% | 3.90% | `kVintageDriftOct` 0.18 → 0.12 | **2.59%** (pass) |
| 2e. Vintage L/R decorrelation: correlation < 0.99 (same self-osc conditions, both sides driven) | — | none | **corr = 0.0016** (pass; measured via a host-free scratch harness, see below) |
| Carryover: top-octave droop, 18 kHz rel. to 1 kHz, LP cutoff=20 kHz, drive=0, res=0, 2× OS | −2.61 dB (exceeds 2 dB budget) | decimator redesign — see below | **−1.76 dB** (pass) |
| 2f. Aliasing sanity: 5 kHz sine, max drive, LP 20 kHz, 2× OS: worst non-harmonic spur < −40 dB | −29.54 dB | decimator redesign (see below); no further fix applied | **−29.03 dB** (fail — see Concerns) |

### Decimator fix (carryover item)

The 2× oversampling path decimated by averaging the two oversampled-rate
core outputs (`lp += o.lp; ...; lp *= 0.5f`), a crude 2-tap boxcar. Its
magnitude response is `|cos(pi*f/fsOs)|`, which only reaches −3 dB at the new
Nyquist (24 kHz for fsOs=96 kHz) — badly under-attenuating the 24–48 kHz band
that folds back into the audio range on downsampling, plus non-trivial
in-band droop near 18–20 kHz.

Replaced it with `DecimFir13` in `src/onbetap/engine.hpp`: a 13-tap FIR run
at the oversampled rate, with per-side, per-tap (lp/bp/hp) history state
added to `OnbetapVoice` (`firLpL/firBpL/firHpL/firLpR/firBpR/firHpR`),
reset in `reset()`. `processSide()` in `src/Onbetap.cpp` only uses it on the
`oversample == 2` path — 1× bypasses it entirely (single core solve, no
decimation needed) and 4× keeps the original boxcar average (out of this
task's scope; 4× has 2× the headroom to Nyquist already).

The brief's suggested starting point (a textbook 7-tap half-band FIR,
`h = [-0.03125, 0, 0.28125, 0.5, 0.28125, 0, -0.03125]`) was tried first and
**rejected by measurement**: a true half-band filter is pinned to exactly
−6 dB at fsOs/4 by construction (a structural property of zero even-index
taps), which pushed the measured 18 kHz droop to **−3.43 dB** — worse than
the boxcar decimator it was meant to replace. The final design is a
Kaiser-windowed FIR (`scipy.signal.firwin`, 13 taps, cutoff 23.5 kHz,
beta=1.0, at fsOs=96 kHz) chosen by grid search over taps/cutoff/beta for
passband flatness rather than maximum stopband rejection: the linear-interp
upsampler ahead of it (left untouched, out of this task's scope) already
contributes roughly 1.4 dB of its own droop near 18–20 kHz on top of
whatever the decimator adds, so the decimator's own budget had to stay under
~0.5 dB there for the combined path to clear the 2 dB target.

Re-verified after the swap:
- `tests/onbetap/test_onbetap.cpp` and `test_engine.cpp`: still 19/19 and
  9/9 (the filter core itself, `OnbetapFilter.hpp`, was not touched).
- Level targets (1a/1b/1c) and self-osc onset/suppression (2a–2c):
  unaffected (numbers above are post-decimator-fix).
- Aliasing sanity (2f): essentially unchanged (−29.03 dB vs −29.54 dB
  before) — see Concerns below for why a decimator fix alone can't move
  this number much further.

### Concern: aliasing sanity target not met (−29 dB vs −40 dB target)

Diagnosed the dominant spurs (worst at 1 kHz, also 4 kHz/6 kHz/9 kHz/11 kHz
etc., all *not* multiples of 5 kHz) as **intra-core aliasing**: at max drive
the diff-pair saturator generates a very rich harmonic series from the 5 kHz
input, and at 2× oversampling (fsOs=96 kHz, Nyquist=48 kHz) harmonics above
the 19th (95 kHz) already alias *inside the nonlinear solve itself*, before
the decimator ever sees the signal — e.g. the 19th harmonic (95 kHz) aliases
to exactly 1 kHz within the 96 kHz-rate representation, matching the
measured worst spur. No decimator design, however well tuned, can remove
aliasing that has already corrupted the discrete-time signal upstream of it.

Verified this diagnosis empirically: temporarily flipping the `oversample`
default to 4 (fsOs=192 kHz) and re-rendering the same test improved the
worst spur from −29.5 dB to **−35.25 dB** — a real improvement, confirming
the oversampling *ratio* (not the decimator) is the limiting factor — but
still short of the −40 dB target even at 4×. (Default was reverted to 2
immediately after this diagnostic measurement; not a permanent change.)

This is a genuine architectural limitation of running a hard-saturating
nonlinear resonant filter at max drive under practical oversampling ratios,
not a decimator defect, and not fixable by a constant tweak within this
task's scope (a true fix would need a band-limited saturator or a much
higher oversampling ratio than 2×/4×, both out of scope for a calibration
pass). Flagging as a concern rather than chasing further, per the task's
"don't chase perfection" guidance — the module's existing 4× oversampling
menu option is the user-facing mitigation for extreme drive+cutoff
combinations, though even that doesn't fully close the gap.

### Vintage L/R decorrelation: measurement method note

`~/Dev/vcv-headless`'s `host.cpp` never calls `setChannels()` on
`AUDIO_INPUT_R`, so `Onbetap::process()`'s `rConnected` is always false and
the R output always mirrors L exactly — the headless host cannot exercise
true stereo decorrelation at all. Measured this target instead with a
small host-free C++ scratch program
(`onbetap-cal/vintage_probe.cpp`) that replicates `Onbetap.cpp`'s
`modulate()`/`processSide()` formulas directly against `OnbetapFilter.hpp`
and `engine.hpp` (both host-free already), self-osc/no-input/Vintage-on,
60 s. Cross-checked against the real module: the harness reproduces the
same clamped self-osc peak (9.0 V) as the installed plugin under identical
conditions, giving confidence it matches the compiled module's behavior.
Not part of the committed test lane (scratch-only, per the task's file
list).

### Level target math note (why `kMakeupExp` instead of tuning further)

The drive-knob makeup gain was `sqrt(0.25/driveGain) * kOutScale`, i.e. an
inverse-square-root compensation curve. Theoretically, input scaling ×
makeup scales like `sqrt(0.25 * driveGain)` end to end, a ~+12 dB swing
from drive=0 to drive=1 before core saturation compresses it down to the
measured +9.33 dB — still over the +6 dB budget. Generalized the makeup
curve's exponent to a named constant `kMakeupExp` (0.75, up from the
implicit 0.5) so more of the drive-gain increase is compensated by makeup
rather than left as audible loudness increase; measured net effect dropped
to +1.35 dB, comfortably under budget with margin, while the THD proxy
(unaffected by makeup, which is a pure gain scalar) still shows healthy
saturation.

Full detail (all render conditions, constant values, and the design search
for the FIR coefficients) is in `.superpowers/sdd/task-5-report.md`.
