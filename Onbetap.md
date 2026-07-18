# Onbetap

**Onbetap** emulates the filter core of the Soviet Formanta Polivoks (Поливокс) synthesizer (1982, designed by Vladimir Kuzmin) — a two-integrator state-variable filter built with no capacitors at all.

---

The Polivoks core is two К140УД12 programmable op-amps run open-loop as current-controlled integrators, closed into a loop by six resistors; a diff-pair saturator and asymmetric output rails inside each stage give it a reputation as one of the more feral filters to come out of the Eastern Bloc — clean and usable at low resonance, ferocious once driven, and prone to erratic, wide-range self-oscillation at the top of the resonance travel. Onbetap models that behavior directly (nonlinear core, not a linear filter with distortion bolted on) and adds a **Character** switch: **Tamed** for a calibrated, stable instance of the same circuit, **Vintage** for the drift, mismatch, and DC thump real hardware units exhibit.

Onbetap is fully stereo (right input normalled to left) and runs on both VCV Rack and MetaModule.

## Controls

- **Cutoff** — 20 Hz – 20 kHz (log-scaled, default 750 Hz). 1 V/oct CV input, scaled by its attenuverter.
- **Q** (panel label; "Resonance" internally) — 0–100%. Self-oscillation onset sits around 3/4 of the knob's travel, earlier at high cutoff (the real op-amps lose damping as their internal poles rise with cutoff, so onset *moves* — a behavior no open-source emulation had modeled before this one). CV input scaled ±5 V full range by its attenuverter (a 0–5 V envelope at full attenuverter sweeps the whole knob range).
- **Drive** — 0–100%, mapped to roughly −12…+18 dB of gain into the nonlinear core. Driving harder pushes the core into saturation, adding asymmetric-clipping grit and harmonics, and — characteristically — **suppresses resonance**: a loud signal rings noticeably less than a quiet one at the same Q setting, matching the hardware. At low Q it also gets louder as you push it; at high Q the core's own rail clamping holds the level roughly steady while the tone dirties and the peak ducks. CV input scaled by its attenuverter.
- **Mode** — 5-position knob: **Lowpass** (12 dB/oct), **Bandpass** (6 dB/oct skirts, the circuit's native two outputs), **Highpass**, **Notch**, **Peak**. LP and BP are what the hardware actually has; HP/Notch/Peak are extensions built from the same solved core (as several modern Polivoks derivatives — Elta, Cherry Audio's Filtomika — also do), and mode changes crossfade over 5 ms to avoid clicks (except in Vintage — see below).
- **Audio in/out** — stereo. The right input is normalled to the left (mono in → mono in, doubled to both outputs); patch the right input to run true independent stereo.

Onbetap supports polyphony on both VCV Rack and MetaModule (voices follow the wider of the L/R input channel counts).

## Right-click menu

- **Character** — **Tamed** (default) or **Vintage**. Vintage layers in, deterministically per session: a slow random-walk drift of cutoff (decorrelated between L and R), a fixed integrator mismatch between the two stages, a per-unit DC offset that scales with cutoff (producing a thump on fast cutoff sweeps, just like the factory panel switch), and a hard, unfaded mode switch instead of the 5 ms crossfade Tamed uses. All of it is seeded rather than random, so the same patch drifts the same way every time you load it.
- **Resonance limiting** — **Hard** (default, factory/Erica rails) or **Soft** (diode-clamp mod). At full resonance both flavors emerge near-square through the output stage (measured crest factors 1.044 Hard vs. 1.052 Soft — close enough that "squarer vs. rounder" isn't reliably audible); what does survive is pitch (308 Hz Hard vs. 362 Hz Soft under identical conditions) and behavior near onset.
- **Oversampling** — 1× / **2×** (default) / 4×. Raising it to 4× reduces the aliasing produced at maximum drive, at roughly double the CPU cost; 1× is available for CPU-constrained patches.
- **Tuning** — four sliders exposing the calibration constants directly, for by-ear adjustment without a rebuild:
  - **Drive span** (24–48 dB, default 30 dB) — the dB range the Drive knob covers. Raise it for a more extreme (and, at high Q, self-suppressing) top end.
  - **Core headroom** (0.5–2×, default 1×) — overall input scale into the nonlinear core, independent of Drive.
  - **Self-osc onset trim** (±0.1, default 0) — nudges where on the Q knob self-oscillation begins.
  - **Output trim** (±12 dB, default 0 dB) — overall output level.

## Character

A few behaviors worth knowing about, all measured against the model's targets:

- **Drive suppresses resonance.** Overdriving the input compresses the resonant peak — a quiet signal at a given Q rings more than a loud one at the same Q.
- **Self-oscillation onset moves with cutoff**, appearing earlier (lower on the Q knob) at high cutoff than at low cutoff.
- **Hard self-oscillation at maximum resonance can drop into a slew-limited relaxation regime** — a lower-pitched, harsher oscillation than the filter's own resonant frequency, the same "suddenly harsh" quality reported on real units.
- **No bass loss at high resonance** — the lowpass output stays present under the sound even with Q driven hard, rather than thinning out.
- **Low self-noise** — the filter is quiet at rest; self-oscillation starts from the same alternating-sign denormal dither the rest of this plugin's DSP uses, so it wakes from silence rather than from injected noise.

## Provenance

Onbetap's model was derived from factory Polivoks schematics, the Erica Synths DIY Polivoks VCF circuit, and the physics of the К140УД12 programmable op-amp (no RC/LC time constants anywhere in the core — cutoff is the op-amps' own gain-bandwidth, set by bias current), rather than from a generic SVF-plus-saturator approximation. Full circuit analysis, literature survey, and a survey of prior emulations are in `docs/research/polivoks-local-materials.md`, `docs/research/polivoks-web-literature.md`, and `docs/research/polivoks-emulations.md`.
