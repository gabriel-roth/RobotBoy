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

## 2026-07-15 — Task 6: MetaModule build + headless simulator verification

Built the `.mmplugin` from this worktree
(`cd metamodule && cmake --fresh -B build -GNinja && cmake --build build`,
ARM cross-toolchain) — succeeded with **zero Onbetap portability fixes**;
`metamodule-plugins/RobotBoy.mmplugin` produced (705 KB).

Built the macOS-hosted headless simulator in `~/Dev/metamodule/simulator`
with RobotBoy registered as a built-in via cache vars only (no
`ext-plugins.cmake` edit):
```
cmake --fresh --preset headless \
  -Dext_builtin_brand_paths="$HOME/Dev/RobotBoy/.worktrees/worktree-polivoks/metamodule" \
  -Dext_builtin_brand_libname="RobotBoy"
cmake --build build-headless
```
This surfaced one **portability fix, in `src/particules/Particules.cpp`
(not Onbetap)**: `dsp_memory_ = memalign(...)` was gated on
`#if defined(METAMODULE) && !defined(SIMULATOR)`, but the simulator's
ext-builtin brand path (`simulator/ext-plugins.cmake`) defines `METAMODULE`
without `SIMULATOR` for the plugin library itself (`SIMULATOR` is only
defined on the `simulator` executable target, which doesn't propagate to
its static-library dependencies) — so on macOS this branch was taken and
`memalign` is undeclared there (the `<malloc.h>` include two lines above
already had a matching `#ifndef __APPLE__` guard; the usage condition
hadn't been updated to match). Fixed by adding `&& !defined(__APPLE__)` to
the condition, mirroring the existing include guard — same real-firmware
behavior, now also correctly falls through to `posix_memalign` on the
macOS-hosted simulator. No DSP/behavior change. Combined-library build then
succeeded (206/206), Onbetap.cpp compiled without any changes needed.
`tests/run.sh` re-run after this fix: 19/19 DSP tests + 9/9 engine tests +
3/4 guard tests pass; `test_public_metadata` fails as expected/pre-existing
(module list check predates Onbetap/Ondes registration — noted as
out-of-scope in this task's brief).

### Patch and render

Patch (`patch.yml`, single `HubMedium` + one `RobotBoy:Onbetap`, panel
In1/In2 → Onbetap L/R in, Onbetap L/R out → panel Out1/Out2 — panel
`panel_jack_id` 0/1 confirmed from `firmware/lib/CoreModules/hub/
panel_medium_defs.hh` and cross-checked against `patches/default/
_autosave.yml`'s mapped_ins/outs): cutoff normalized 0.524677 (=
(log2(750)−log2(20))/(log2(20000)−log2(20)), i.e. the module's own 750 Hz
default), res 0.3, drive 0.2, mode 0 (LP). Rendered 96000 samples (2 s) at
48 kHz against a 3-tone test signal (200/900/3000 Hz, ~4 V peak combined)
via:
```
build-headless/simulator -p patch.yml --in in_stereo.wav --out out_mm.wav -n 96000
```
**Effective load (single core, 2× oversampling, the module default): 0.65%.**
Also measured at 1× oversampling via a `vcvModuleStates` JSON override
(`{"oversample":1}` — `dataFromJson` tolerates partial JSON, confirmed by
reading the code) on module 1: **0.35%** (roughly the expected ~2× scaling,
and the rendered audio differs materially between the two — confirms the
override actually took effect, not a no-op).

### Comparison against vcv-headless

Rendered the identical signal through the installed VCV plugin (dylib
confirmed current vs. commit `038cf9b`/no rebuild needed) at the same
param values (cutoff raw = log2(750) = 9.550747, res=0.3, drive=0.2,
mode=0) and sample rate (48 kHz, matching Task 5's own spec convention).

**Found and had to correct a WAV↔volts scale-convention mismatch between
the two hosts** (not a DSP bug): `vcv-headless`'s `host.cpp` uses
sample = volts/5.0 (its documented convention); the MM headless simulator's
audio path (`simulator/src/headless/audio_wrapper.hh` →
`firmware/src/patch_play/patch_player.hh` →
`firmware/vcv_plugin/export/src/VCV_module_wrapper.cc`) passes the WAV
sample straight through to `Port::setVoltage()`/from `Port::getVoltage()`
with **no scaling at all** — sample = volts, 1:1. Fed with the "same" WAV
(same sample values) but different implied voltages, the two engines'
resonant/driven filter naturally diverged (worst case seen: 12% relative,
with res=0.3/drive=0.2 amplifying a ~2.5% input-level mismatch through the
nonlinear resonant core — confirmed by a linear-passthrough diagnostic
render, cutoff=20 kHz/res=0/drive=0, where the divergence was ~2.5% instead
of a random/unrelated shape, and by tracing the exact scale factor in both
codebases). Generated two input WAVs from the same underlying volts array —
`in_mono.wav` (sample = volts/5, for vcv-headless) and `in_stereo.wav`
(sample = volts, L=R, for MM headless) — and compared outputs after
converting MM's samples back to the common volts/5 unit
(`out_mm_sample / 5.0`).

**Result: max |diff| = 2.74e-6 (full scale = 1.0), rms diff = 1.17e-6** —
essentially floating-point noise, comfortably under the 1e-3 pass bar. No
alignment offset needed (best cross-correlation lag = 0 samples); no
special first-buffer transient was observed in this render (checked
separately, diff excluding the first 512 samples is identical to the
full-signal value). MM L vs R outputs are bit-identical (0.0 max diff), as
expected for the Tamed/no-drift default (no vintage decorrelation).

Scratch files (WAVs, specs, patches, `compare.py`, `gen_input.py`) live at
`/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/168cdb10-1fe9-49fd-b698-5e1aa42f219f/scratchpad/onbetap-mm/`
(not committed).

### Concern

The `Particules.cpp` fix, while minimal and behavior-preserving on every
existing build path (real firmware, VCV Rack, GUI simulator, tests), is
technically outside this task's nominal Onbetap-only scope — it was
necessary because `ROBOTBOY_COMBINED` links all modules into one plugin
library, so the headless simulator build (a hard requirement of this task)
could not succeed without it. Flagging for awareness rather than treating
it as scope creep to revert.

## 2026-07-15 — Task 7: docs, changelog, final sweep

Wrote `Onbetap.md` (module doc, MF20.md-style: controls, right-click menu,
a "Character" section for the signature behaviors, provenance paragraph
pointing at `docs/research/polivoks-*.md`), added an "Unreleased" section
to `CHANGELOG.md` (Onbetap new-module line + the Task 6 Particules
macOS-simulator portability fix), and added Onbetap to `README.md`'s module
list (six modules now, alphabetical slot between MF-20 and Ondes) with a
short blurb matching the other entries' style. No screenshot exists yet for
Onbetap (`screenshots/Onbetap.png`) — every other module doc/README entry
has one; this is a follow-up item, not blocking, since screenshot capture
is a manual/GUI step outside this task's automated scope.

### User checklist (GUI-sim checks — run manually, not by an agent)

In VCV Rack:
- [ ] Load Onbetap, patch audio through it, sweep **Cutoff** across its
      range in each of the five **Mode** positions (LP/BP/HP/Notch/Peak) —
      each should sound like a lowpass/bandpass/highpass/notch/peak filter
      tracking the knob, no clicks on mode changes (Tamed).
- [ ] Turn **Q** to maximum with no input patched — filter should
      self-oscillate (a tone appears from silence). Sweep **Cutoff** while
      self-oscillating — pitch should track. Try both **Resonance
      limiting** settings (Hard = squarer/harsher, Soft = rounder, slightly
      higher pitch) and both **Oversampling** settings for comparison at
      high drive.
- [ ] With a loud signal patched in, compare **Drive** low vs. high at a
      fixed **Q** — resonance should audibly diminish as Drive increases.
- [ ] Toggle **Character** between Tamed and Vintage while holding a
      self-oscillating patch — Vintage should drift slowly and
      unpredictably (pitch wander, occasional thump on cutoff sweeps);
      toggling back to Tamed should stabilize.
- [ ] Open the right-click menu's **Tuning** submenu and confirm each
      slider (Drive span, Core headroom, Self-osc onset trim, Output trim)
      audibly changes something when dragged, and resets correctly on
      patch reload.
- [ ] Save and reload a patch with non-default menu settings (Character,
      Resonance limiting, Oversampling, Tuning sliders) — confirm they
      persist.

On MetaModule (GUI simulator, spot-check only):
- [ ] Load a patch with Onbetap, confirm knobs/CV jacks map to the same
      controls as VCV Rack and audio passes through correctly in at least
      one mode.
- [ ] Spot-check the right-click-menu-equivalent (module option page) for
      Character/Resonance limiting/Oversampling, and that the panel visual
      matches `res/Onbetap.svg`.
- [ ] The MM "Effective load" numbers recorded above (0.65% / 0.35%) are a
      macOS-hosted simulator proxy, not a real Cortex-A7 measurement — check
      actual CPU load on real MetaModule hardware.

## 2026-07-15 — Final review fixes

- The 13-tap `DecimFir13` (2× oversampling path, `src/onbetap/engine.hpp`)
  adds a fixed group delay of 6 samples at fsOs — about 3 host samples once
  decimated back to the host rate — on top of whatever the linear-interp
  upsampler contributes.

## 2026-07-15 — session wrap-up

All seven plan tasks executed via subagents, each gated by a task review, then
a whole-branch final review (most-capable model). Final review verdict: merge
with fixes — all four Important findings fixed and verified in e5dec0b (output
polarity re-inverted per spec, sanitize() now clears DC-blocker/FIR state on
NaN recovery, res/drive CV rescaled to /5 per spec + house convention,
identity-test module list updated so tests/run.sh is fully green). Remaining
deferred minors were applied afterward (test check renamed, unused include
dropped).

State: VCV build clean + installed; MetaModule .mmplugin builds; MM headless
output matches vcv-headless to 2.7e-6; unit lane fully green. Open items for
the user: GUI checklist (see Task 7 section), screenshots/Onbetap.png,
real-device MM CPU check, and the documented max-drive aliasing gap (−29 dB at
2× OS, −35 dB at 4×; 4× is the menu escape valve).

## 2026-07-18 — Drive makeup was double-compensating (fixed)

**Symptom (user, by ear):** with a note held at ~cutoff and Q ~70%, turning
Drive up past ~50% made the filter *quieter and cleaner*, not "louder and
dirtier" as intended. Ring suppression ("rings less") was correct.

**Root cause:** the output makeup gain `(0.25/driveGain)^0.75 · kOutScale` was
calibrated at res=0 / cutoff=20 kHz (filter wide open, sub-saturation — that's
the only condition where test 1b's +1.35 dB was measured). But the core's
integrator states clamp at their rails, which *is* a level compressor — the
authentic "natural compression between signal and self-osc" (emulations §4.4).
At moderate/high Q the resonant peak pins the core at its rails even at Drive=0,
so more input can't raise the level. The makeup then compensated a **second**
time on top of that, overshooting into net attenuation — and shrank the signal
below the output-VCA (`9·tanhish(v/9)`) knee, which is where most of the audible
grit at low Drive actually lives, so it stripped the dirt too.

**Measured, tone at cutoff (dB re 1 V RMS / THD):**

| Drive | old, res 0.70 | old, res 0.30 | new (const gain), res 0.70 | new, res 0.30 |
|------:|--------------:|--------------:|---------------------------:|--------------:|
| 0.0   | +16.5 / 11.6% | +15.4 / 7.9%  | +16.5 / 11.6%              | +15.4 / 7.9%  |
| 0.5   | +8.3 / 1.3%   | +8.3 / 1.2%   | +17.3 / 16.8%             | +17.3 / 16.7% |
| 1.0   | −0.7 / 1.4%   | −4.8 / 1.4%   | +17.6 / 4.4%*             | +17.4 / 17.4% |

(* res 0.70 top-of-sweep enters chaotic self-osc; THD non-monotonic there.)

**Fix:** output makeup is now a *constant* buffer gain (the ×11 clone
output-buffer analog), independent of Drive — the core's rail clamping is left
as the sole, authentic level-compression mechanism. Drive=0 is bit-identical to
before (both give `makeup = kOutScale`), preserving the test-1a calibration.
Gain mapping extracted to `src/onbetap/drive.hpp` and unit-tested by
`tests/onbetap/test_drive_level.cpp` (level holds/rises + THD rises with Drive
at res 0.0/0.30/0.70; verified red against the old formula, green after).

**Retired:** the "+6 dB drive budget" (test 1b) — it encoded the very
behavior being removed. New intent: level holds at high Q, rises then plateaus
at low Q; dirt rises with Drive. Design: `2026-07-18-onbetap-drive-hw-path-design.md`.

**Caveat:** at high Drive the output now runs hot (~+17 dB ≈ 7 V RMS, peaks
clipping into the 9 V VCA — the intended "ferocious" behavior, bounded within
±10 V). If ever too hot, use a *constant* trim (`kOutScale` or the Output-trim
menu slider), never a drive-dependent one. Default level unchanged.

**Follow-up — Drive span 36 → 30 dB:** user then reported Drive adds level/grit
up to ~90% then smooths and quietens again. Measured: no reversal at moderate Q
(core tap flat, fund −1.0 dB / THD 1.5% across drive 0.7→1.0 at res 0.50); the
effect is only near the self-osc knee (res ≈ 0.70), where Drive chokes the
resonance that supplies the high-Q level/edge — the resonance-suppression
feature at its extreme. Since grit is sourced from the core (minimal path), max
Drive suppresses resonance and resonance-grit together. Trimmed default
`tuneDriveDb` 36 → 30 dB (max +24 → +18 dB): at span 36 the drive=1.0 THD at
res 0.70 collapses to ~7.5%, at span ≤32 it recovers to ~16–20%; span 30 maps
the last-productive knob point off the end. Doesn't remove self-osc chaos at the
knee (physics, bounded). Menu range unchanged (24–48); saved patches keep their
stored span. See `2026-07-18-onbetap-drive-hw-path-design.md` (Follow-up).

## 2026-07-18 — Drive grit: VCA push keeps the top of the knob dirty

**Why:** the span trim above mitigated but could not cure the top-of-knob
calm zone (investigation report, Finding 3): at high Q the (authentic)
resonance choke takes the resonance-derived grit and \~1 dB of level with it,
so max Drive got smoother/softer — contradicting every hardware account
("majestic Polivoks roars" when overdriven; resonance steps "much accentuated
if the filter is overloaded"). Root cause: in the minimal core-only path,
high-Q grit rides on the very resonance Drive chokes; the real circuit's
input-clip roar has no downstream voice.

**Fix (voicing, deliberately not circuit-derived):** Drive-following push into
the existing output VCA — `out = 9·tanhish(push·v/9)`,
`push = exp2(gritDb/6.0206·drive²)`, new `vcaPush` field from
`onbetap::driveGains`. Always ≥ 1 (a boost into the fixed 9 V ceiling, never a
cut — cannot re-create the double-compensation bug), `push(0) = 1` exactly
(Drive-0 bit-identity), quadratic so the mid-knob voicing stays put. New
Tuning slider **Drive grit** 0–12 dB, default `onbetap::kDefaultGritDb` = 6;
0 dB recovers the old behavior exactly. JSON `tuneGritDb`; missing key →
default, so existing patches pick up the fix (intended).

**Measured** (`test_drive_grit sweep`; span 30, 5 V, 750 Hz tone at cutoff,
LP/Hard/Tamed/2×, 48 kHz):

```
gritDb | r.60 thd@.9 thd@1 lvl@1 | r.70 lvl@.9 lvl@1 | r.30 thd@.5 | r.50 thd@.5
   0.0 |       17.3   17.3   17.4 |       17.3   17.2 |        16.3 |        16.4
   4.0 |       25.2   27.0   18.2 |       17.9   17.9 |        18.7 |        18.8
   6.0 |       28.8   31.1   18.4 |       18.2   18.2 |        20.0 |        20.0
   8.0 |       31.9   34.4   18.5 |       18.4   18.4 |        21.2 |        21.3
  10.0 |       34.5   37.1   18.7 |       18.5   18.5 |        22.4 |        22.5
  12.0 |       36.7   39.2   18.8 |       18.6   18.7 |        23.7 |        23.7
```

At res 0.60 the top decile goes from dead flat (17.3 → 17.3 %) to rising
(28.8 → 31.1 %); at the res 0.70 knee the push adds +1.0 dB at full Drive.
Default 6 dB chosen over 4 (also passes, but with 2 pp / 0.2 dB margins on
chaos-adjacent measurements); mid-knob cost +3.6 pp, inside the +8 pp budget.

**Metric lesson (recorded in the spec amendment):** harmonic THD is the wrong
grit instrument at the self-osc knee — there the output is rail-to-rail with
79–87 % *inharmonic* non-fundamental energy (chaotic sidebands; h2…h6 < 0.5 V),
so harmonic bins read 3–5 % no matter how hard the VCA is pushed. The knee
guard is therefore level-based; the THD guards live at res 0.60 (stable choked
regime). Also: res ≥ 0.60 has authentic brief self-osc pockets at isolated
Drive points (e.g. +1.3 dB level bump at drive 0.7, span 30) — strict level
monotony is asserted only at res 0.30/0.50.

**Guards:** `tests/onbetap/test_drive_grit.cpp` (15 checks: law bit-identity /
escape hatch / monotony, top-decile THD rise + floor, knee level gain, mid-knob
budget, level monotony, 9 V bound). Red baseline = the `gritDb 0` sweep row.
Design: `2026-07-18-onbetap-drive-grit-design.md`.

## 2026-07-18 — Deep-overdrive guards: gated state leak + sat residual slope

User report from patch audition (`\~/Desktop/test-patches/6.vcv`): at low
cutoff + max Drive the signal disappears. Two core pathologies found (details
and measurements: `2026-07-18-onbetap-overdrive-stability-design.md`):

1. **Subsonic burst** — deep drive swamps n1, collapsing loop damping; the
   resonant mode rings free at \~cutoff (7.3 V RMS of 20–40 Hz rumble) while
   the note halves. Not numerics (8-pass solve identical), not the asym DC
   (survives symmetrized core). Onset \~node ≥ 8 units (span 36+ territory;
   default span 30 tops out at 7.9).
2. **Rail-pin silence** — rectification DC pins both states at −4.1 where
   the sat was hard-flat (zero gain) → absolute silence (\~span 48 + 10 V).

**Rejected:** input pre-clipper (measured byte-identical — clipping moves no
node zero-crossing, so the loop never sees it); fixed-Hz state leak (kills
legit self-osc at fc 40/80 — burst and self-osc share frequencies; the
discriminator is input depth).

**Fix:** (a) drive-gated state leak, pole 15 Hz at full gate,
`gate = min(|xin|/8, 1)` — zero-input self-osc untouched at every cutoff;
(b) sat() keeps a 5 % residual slope beyond the former hard clamp (real diff
pairs choke asymptotically, never to zero). tanhish stays exact (output-VCA
9 V bound). Wrapper sets the leak from fsOs in modulate().

**Guards:** `tests/onbetap/test_overdrive_stability.cpp` (8 checks: note
survival + rumble ceiling at the patch conditions, rail-pin audibility,
self-osc preservation at fc 750/80/40, 9 V bound). Verified red pre-fix
(note 0.32 V, rumble 7.3 V, pin 0.000 V). Full suite 181 green; drive-grit
THD moved 31.1 → 31.8 % (inside guards), everything else unchanged.

**Follow-up (same day):** residual by-ear dropouts on certain notes at low
cutoff (steady-state and note-transition harnesses could not reproduce them).
Per user request, the gated-leak pole now scales up below 80 Hz cutoff —
boost = clamp(80/fc, 1, 4), i.e. up to a 60 Hz pole at fc 20, unchanged at
fc ≥ 80. Constants `kLeakCornerHz`/`kLeakBoostMax` on OnbetapFilter; wrapper
computes the boost per voice in modulate(). Guards re-run: burst margins
improved (note@0.85 0.84 → 0.97 V), self-osc untouched, rail-pin output
3.46 → 2.98 V (still well above the 0.5 V audibility floor). Suite green.

## 2026-07-18 — Voicing baked, Tuning menu removed, Soft default

By-ear final from the user: **Drive span 36 dB** (knob = −12…+24 dB), **grit
3.5 dB**, headroom 1×, onset trim 0, output trim 0 dB — baked as constants
(`onbetap::kDriveSpanDb`, `onbetap::kDefaultGritDb`; the rest are literals at
the driveGains call). The **Tuning submenu is removed**; old patches' stored
tune* JSON keys are deliberately ignored. **Onset trim (same day, two reversals):**
the self-osc onset trim slider first came back as a single top-level menu
slider (near the knee k is only \~0.02–0.1, so a 0.045 trim moves the onset
from res \~0.72 to \~0.84 and triples the Q at res 0.68 — clearly audible),
then was **baked at +0.045** (`kOnsetTrim` in Onbetap.cpp) and the menu
removed entirely once the user settled on that value by ear. Self-osc now
begins \~84 % up the Q knob — a deliberate departure from the
hardware-matched \~2/3-of-pot onset. All tune* JSON keys are ignored on
load; the right-click menu is Character / Resonance limiting / Oversampling
only. **Resonance limiting defaults to
Soft** (listed first in the menu); Hard remains an option.

Span 36 is viable again — the choke reversal that motivated the 30 dB trim is
handled by the grit push + deep-overdrive guards. Measured at the shipped
combo (span 36, grit 3.5, sweep in `test_drive_grit`): res 0.60 top decile
rises 25.7 → 27.6 % THD (floor 25 % holds), knee level +0.6 dB over grit-0,
mid-knob +2.2 pp. `test_drive_grit` now measures at `kDriveSpanDb`;
`test_overdrive_stability` gained Soft-mode variants (results identical to
Hard in the deep-drive regimes — states sit below the clamp knee there).
`test_drive_level` keeps its explicit span-30/grit-0 reference config (a law
guard, not a shipped-config guard).
