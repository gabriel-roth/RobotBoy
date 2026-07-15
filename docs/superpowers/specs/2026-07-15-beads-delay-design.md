# Échos (beads-delay) — Design Spec

Robot Boy module emulating the hidden **delay mode** of Mutable Instruments
Beads (the mode entered on hardware by turning SIZE fully clockwise to ∞).
Targets VCV Rack and MetaModule, as part of the RobotBoy plugin.

Working project name: **beads-delay** (branch `worktree-beads-delay`).
Proposed module slug: `Echos`, display name **Échos** — following the
Particules / Ondes convention of French names for the Beads-derived modules
(and avoiding shipping another maker's module name). **Renaming is trivial
until patches exist; flagged for Gabriel's review in NOTES.md.**

## Sources

- `resources/Mutable-Beads-manual.pdf` — especially "Beads as a delay",
  "Recording quality and audio input", "Attenurandomizers", "Mixing and
  audio output".
- Transcript, "Beads as a delay" (DivKid-style patch video,
  youtube.com/watch?v=6cvBiu1h2HU) — scratchpad `beads_vid1.txt`.
- Transcript, "Ultimate guide to Beads" (youtube.com/watch?v=1K89l_bgTe8),
  delay-mode chapters — scratchpad `beads_vid2.txt`.
- Existing `particules_dsp` core in this repo (recording buffer, quality
  processor, saturation, attenurandomizer, random) — proven on MetaModule.
- `~/Dev/4ms-vcv` — MetaModule-oriented DSP techniques (survey in
  `docs/superpowers/specs/2026-07-15-beads-delay-mm-optimization-notes.md`).

## Non-goals

- No granular mode (Particules covers it), no wavetable mode (Ondes covers
  it), **no reverb** (maybe later).
- No auto-gain-control input stage (VCV levels are predictable; Particules'
  AutoGain exists if we change our minds). A fixed input trim is available
  in the context menu instead.
- No buffer persistence across power cycles (hardware backs up the buffer
  after 10 s of freeze; skip).
- No assignable-CV multiplexing (hardware button [M]): we have panel space
  for dedicated FEEDBACK and BLEND CV jacks.
- No mono-buffer doubling of record time (hardware doubles buffer length
  when only one input is patched). Our buffer is always stereo.

## How the hardware behaves (reference semantics)

Distilled from the manual and videos:

1. **One eternal grain.** Delay mode = a single never-ending "grain"
   (read head) that continuously follows the write head at a distance.
2. **Base delay time** is set one of two ways:
   - *Manual*: no clock patched → DENSITY knob + CV set base time.
     At 12 o'clock, base time = full buffer duration. Turning away from 12
     (either direction) shortens it, down to audio rates (flanger / comb /
     Karplus-Strong territory). CCW = single tap; CW additionally enables a
     second, unevenly spaced tap (multi-tap).
   - *Clocked / tap*: clock into SEED jack (or taps on SEED button) set base
     time = interval between ticks. DENSITY then selects a subdivision:
     12 o'clock = 1/1; CCW = binary subdivisions (1/2, 1/4, 1/8, 1/16);
     CW = wider ratio set (1/2, 1/3, 1/4, 1/6, 1/8, 1/10, 1/12, 1/16),
     with the CW side keeping the extra tap behavior.
3. **TIME** = multiple of the base delay time (the actual delay distance).
   With FREEZE on, the buffer is sliced into base-time-length slices and
   TIME selects *which slice* loops (beat slicer).
4. **PITCH** = classic rotary-head (dual crossfaded head) pitch shifter
   applied to the delayed signal, *bypassed at 12 o'clock*, range ±24
   semitones with virtual notches at musical intervals. Sits inside the
   feedback path → shimmer effects. PITCH CV tracks 1 V/oct when its
   attenurandomizer is fully CW.
5. **SHAPE** = tempo-synchronized amplitude envelope on the repeats;
   fully CCW = flat/no envelope (normal delay).
6. **FEEDBACK** mixes output back into the processing-chain input, with a
   per-quality limiting scheme (clean brickwall → tape saturation).
   Audio-rate base times + feedback = Karplus-Strong (excite with a pluck,
   V/oct on DENSITY CV plays it as a voice).
7. **Modulating the delay time is musical**, not clicky: read-head distance
   changes slew, producing tape-style doppler pitch bends.
8. **Quality setting** affects internal rate/resolution, buffer duration,
   feedback limiter character, and (scorched cassette) wow/flutter:
   Bright digital 48 kHz/16-bit, Cold digital 32 kHz/12-bit, Sunny tape
   24 kHz/12-bit, Scorched cassette 24 kHz/8-bit (longest buffer).
9. **Slow random LFOs** (not per-grain S&H randomness) are internally
   routed to the attenurandomizers in delay mode.
10. **FREEZE** halts recording (button latch or gate input).
11. Stereo in/out; single input normalizes to both channels.

## Panel

~12–14 HP. Straightforward layout, Beads-family look (consistent with
Particules). Panel via `vcv-panel-generate` YAML spec; **do not over-invest,
layout will be revised later.**

Controls (Beads names kept deliberately — this module is an emulation and
Beads tutorials should map 1:1):

| Control | Type | Function |
|---|---|---|
| DENSITY | large knob | base delay time (manual) / subdivision (clocked); CW half adds tap 2 |
| TIME | large knob | delay-time multiplier; slice selector when frozen |
| PITCH | large knob | rotary-head pitch shift ±24 st, notched, bypass at noon |
| SHAPE | medium knob | tempo-synced envelope on repeats; CCW = off |
| FEEDBACK | medium knob | feedback amount, per-quality limiting |
| BLEND | medium knob | dry/wet crossfade |
| TIME AR, PITCH AR, SHAPE AR | small knobs | attenurandomizers (CW: CV amount; CCW: randomization; slow-random-LFO source) |
| FREEZE | latching button + light | freeze buffer (beat-slicer mode) |
| SEED | momentary button + light | tap tempo; light blinks base time |
| QUALITY | momentary button + 4-state LED | cycles the four quality modes |

Jacks: IN L, IN R, OUT L, OUT R, DENSITY CV (V/oct at audio rates), TIME CV,
PITCH CV (1 V/oct when AR fully CW), SHAPE CV, FEEDBACK CV, BLEND CV,
SEED (clock/gate in), FREEZE (gate in). 12 jacks total.

## DSP architecture

New standalone core `src/beadsdelay/dsp/` in the `particules_dsp` mold —
same memory model (caller-allocated block, placement Impl, no allocation in
`Process()`), block-chunked `Process(input, output, num_frames)` with
`kMaxBlockSize = 64`, and a thin VCV adapter `src/beadsdelay/Echos.cpp`
using a 64-sample wrapper block runtime like Particules.

**Reuse from `particules_dsp` (compiled once in the same lib, included
directly):** `RecordingBuffer` (Hermite fast stereo reads, freeze
declicking, deferred clear, write decimation), `QualityProcessor` (4 modes
incl. tape wow/flutter), `Saturation` (per-quality feedback limiting),
`Random`, `StateVariableFilter`, dsp utils/interpolation. The new core gets
its own namespace `beadsdelay_dsp` and owns everything delay-specific.

### Signal flow (per sample, inside 64-frame blocks)

```
in L/R ──► input trim ──► [+ feedback] ──► quality-input processing ──► write (unless frozen)
                                              RecordingBuffer (stereo float, decimated write)
read head(s): tap1 = write - delay_samps (slewed), tap2 (optional, uneven ratio)
        ──► Hermite read ──► [rotary-head pitch shifter (bypass at noon)]
        ──► quality-output processing ──► SHAPE envelope
        ──► wet; feedback = LimitFeedback(wet * fb_amount)  (per-sample loop)
out = dry*(1-blend) + wet*blend   (equal-power crossfade)
```

- Feedback is **per-sample** (injected into the next write), not one-block
  delayed — required for stable audio-rate Karplus-Strong behavior.
- A DC-blocking one-pole HP sits in the feedback path (as in Particules).

### Delay time computation (block rate)

- Manual mode: `base = buffer_duration * exp2(-k * |density - 0.5| * span)`
  — exponential mapping from full buffer down to ~2 ms; DENSITY CV in
  V/oct (halves time per volt). Exact curve constant-tunable.
- Clocked mode: entered when a clock is present at SEED (with timeout) or
  tap tempo; base = smoothed tick interval; DENSITY picks subdivision from
  the two ratio tables (binary CCW / extended CW) with hysteresis on the
  knob position to avoid boundary flapping.
- TIME multiplier: continuous in manual mode (range ~1×…16×, clamped to
  buffer), snapped to integers in clocked mode. Under FREEZE, TIME selects
  the slice index instead.
- The *read distance in samples* is slewed with an exponential slew at a
  tunable rate → doppler pitch bends when modulated (hardware-matching).
  A context-menu option offers **crossfade jumps** instead (clickless but
  pitch-neutral) — both implemented, menu-selectable.

### Rotary-head pitch shifter

Two read heads on a short dedicated ring (~4096 samples at internal rate),
sawtooth phase offset by half, triangular crossfade — the classic
Clouds/Beads "rotary head" design. True bypass within a notch around noon.
Ratio from semitones via `exp2(semi/12)` computed at block rate only.
Virtual notches at ±12, ±7, ±5, 0 (reuse `pitch_notch_map.hpp` from
Particules if applicable).

### SHAPE envelope

Wet-path amplitude envelope with period = base delay time, phase-locked to
the base-time grid. SHAPE knob morphs: CCW flat → rectangular gate →
raised-cosine → slow-attack ramp. Feedback taps post-envelope (the envelope
shapes what repeats). Applied per sample from a block-rate-updated phase.

### FREEZE / beat slicer

On freeze: stop writes (RecordingBuffer::NotifyFreeze declick), anchor =
write head. Slice k spans `[anchor-(k+1)*base, anchor-k*base]`; TIME picks
k; read head loops the slice with a short crossfade at the seam. DENSITY
(subdivision) remains live. PITCH/SHAPE stay active. Unfreeze resumes
recording with the write crossfade.

### Attenurandomizers (delay-mode flavor)

TIME/PITCH/SHAPE each: CW from noon = external CV attenuator; CCW = CV
amount controls randomization depth; unpatched = internal randomization
with uniform (CW) / peaky (CCW) distribution. **Internal source = slow
random LFOs** (smoothed random, ~0.05–0.5 Hz, tunable) instead of
Particules' per-grain S&H — new small `SlowRandomLfo` component (two-point
interpolated random or filtered noise, block rate).

### Quality modes

Reuse Particules' four modes and decimation factors unchanged (consistency
within the plugin): kHiFi=1×, kClouds=2×, kCleanLoFi=8×, kTape=4× on a
fixed 192 000-frame stereo buffer (1.5 MB) → 4 s / 8 s / 32 s / 16 s.
Feedback limiting per mode via `Saturation::LimitFeedback`. Wow/flutter
pitch modulation applied to read position in tape mode. Quality switch
ducks/crossfades as Particules does.

## MetaModule CPU strategy

Assume a naive implementation is too slow; design to the budget from the
start. Techniques (Particules-proven, to be augmented from the 4ms-vcv
survey — see companion notes doc):

1. **64-sample block processing** everywhere; all parameter math, exp2/pow,
   ratio tables, slew coefficients, envelope phase increments computed at
   block rate; per-sample loop touches only adds/muls, Hermite reads, and
   the feedback saturator.
2. **Write decimation** (sample-and-hold) already halves/quarters/eighths
   the effective buffer bandwidth in lo-fi modes; reads stay cheap Hermite.
3. **Pitch shifter true bypass** at noon (zero cost in the common case);
   when active it adds 2 mono→stereo Hermite reads + a crossfade.
4. **No std::pow/sin/exp in the audio loop** — exp2 approximations or
   block-rate computation; saturation uses polynomial/mu-law curves.
5. **Fast paths**: single-tap mode skips tap-2 entirely; SHAPE fully CCW
   skips envelope; FREEZE skips write path.
6. **Stack discipline**: work buffers in Impl, not on the audio stack
   (MetaModule stack is small).
7. Target: comfortably below Particules' MM cost (it renders up to 30
   Hermite grain reads + reverb; we have ≤4 reads + a shifter).

Memory: 1.5 MB buffer + ~64 KB shifter ring + Impl, allocated via
`memalign` on MM exactly like Particules.

## Context-menu items (decisions deferred to Gabriel via options)

- **Time-change response**: Tape (doppler slew) [default] / Crossfade.
- **Slew rate** slider (doppler speed) — tunable constant exposed.
- **Random LFO rate** slider.
- **Input trim** (−12…+12 dB).
- **Envelope feedback tap**: post-envelope [default] / pre-envelope.
- (Plus the usual: quality mode also in menu for discoverability.)

## Build integration

- `src/beadsdelay/dsp/{include/beadsdelay_dsp/*.h, src/*.{h,cpp}}` — core.
- `src/beadsdelay/Echos.cpp` — VCV adapter (also compiled for MM).
- `src/plugin.cpp` / `plugin.hpp` — register model.
- `plugin.json` + `plugin-mm.json` — add module entry (vcv-add-module skill).
- `metamodule/CMakeLists.txt` — add sources; `metamodule/Echos_info.hh` if
  the native-info pattern requires it (follow Particules, which uses the
  VCV-adapter route — no native core needed).
- Panel: `panel-specs/echos.yaml` → SVG via vcv-panel-generate; PNG for MM.
- `tests/beadsdelay_dsp/` — Catch2 lane mirroring `tests/particules_dsp/`.
- Amend `tests/test_no_delay_mode.py`: scope its guard to the Particules
  core files only (drop `metamodule/CMakeLists.txt` from the list, with a
  comment) — it enforces the *old* engine's removal, not a ban on delay
  modules.

## Testing

- **TDD at the core level** (Catch2, host-side): delay-time accuracy
  (impulse in → impulse out at N samples), feedback decay rates, clocked
  subdivision tables, doppler slew behavior, freeze slicing indices,
  pitch-shifter ratio verification (zero-crossing/autocorrelation pitch
  estimate), NaN robustness, block-size invariance (1..64), quality-mode
  buffer durations.
- **Headless VCV**: `test-vcv-module-headless` skill — WAV through the
  module, verify delay taps/feedback in the output; compare VCV vs MM.
- **MetaModule simulator**: `build-simulator` skill; audio correctness and
  CPU measurement; iterate until the module fits the budget.
- GUI checks go to a user-run checklist at the end (per repo policy).

## Decision log (autonomous choices to review)

1. Name Échos/`Echos` over "Beads Delay" — family consistency + trademark
   caution. Trivial to change pre-release.
2. Dedicated FEEDBACK/BLEND CV jacks instead of hardware's assignable CV.
3. Always-stereo buffer (no mono time-doubling).
4. Same quality→duration mapping as Particules (4/8/32/16 s) rather than
   hardware's mono column (8/16/20/32 s) — shared infrastructure and plugin
   consistency win.
5. Per-sample feedback topology (standard delay loop) rather than
   Particules' one-block feedback — needed for Karplus-Strong.
6. TIME multiplier ranges and envelope shape family are tunable constants,
   several exposed as context sliders for later tuning.
7. Float `RecordingBuffer` reused from Particules rather than 4ms-style
   int16 storage — RAM is ample (~290 MB shared on MM), the float buffer
   is proven in this plugin, and it enables direct reuse of freeze
   declicking / deferred clear. int16 is the documented fallback if memory
   bandwidth ever shows up in profiling.
8. VCV-adapter route on MetaModule (like Particules) rather than a native
   CoreProcessor (like Loooop) — Particules is far heavier and ships fine
   through the adapter; one adapter serves both platforms.
