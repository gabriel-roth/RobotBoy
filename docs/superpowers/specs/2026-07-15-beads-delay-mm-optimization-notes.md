# MetaModule DSP optimization notes (from ~/Dev/4ms-vcv survey)

Survey of how 4ms writes DSP that runs on the MetaModule (Cortex-A7),
gathered 2026-07-15 for the beads-delay project. Repo surveyed:
`~/Dev/4ms-vcv/4ms-vcv`. Most relevant modules: **DLD / Looping Delay**
(`lib/CoreModules/4ms/core/looping-delay/`) and **Tapo / Tapographic
Delay** (`lib/CoreModules/4ms/core/tapo/`, vendors Mutable stmlib), plus
the rotary-head pitch shifter in `processors/pitchShift.h`.

## 1. Delay-line storage

- DLD: `int16_t` buffer, statically sized member (8 MB,
  `looping-delay/src/elements.hh:37`, `mocks/delay_buffer.hh:13`).
  DSP is 24-bit internally (`*256` on read, `/256` clip on write).
- Tapo: `short*` ring, **power-of-two size** so reads mask with
  `& (size-1)` instead of modulo (`tapo/audio_buffer.hh`). 4ms cut the
  buffer from hardware's 32 MB to 8 MB (`TapoCore.cc:449`).
- Never allocate in the audio callback; buffers are members or one-time
  allocations.

## 2. Interpolation & delay-time changes

- Tapo offers `Read` (none), `ReadLinear` (2-point), `ReadHermite`
  (4-point) side by side (`tapo/audio_buffer.hh:90-121`); taps use linear,
  the clean "repeat" head uses Hermite. Interpolation quality is a per-use
  CPU knob.
- Time-change smoothing, two strategies:
  - *DLD*: equal-power **crossfade between two read heads** (`buf` /
    `fade_buf`) on discrete time changes; mid-fade changes are queued
    (`looping_delay.hh:340-395`); mix via 4096-entry `epp_lut`.
  - *Tapo*: **per-block linear read-pointer ramp** (`tap.hh:125-145`,
    `time_increment` across a 64-sample block) → varispeed glide
    (doppler).

## 3. Sample-rate strategy

- Cores run at a fixed internal rate; `set_samplerate()` rescales
  coefficients rather than resampling audio. VCV-only buffer clear guarded
  `#ifdef VCVRACK` (`looping_delay.hh:75-78`).
- SDK ships NEON-tuned Hermite resamplers (`StreamResampler`,
  `BlockResampler`, `ResamplingInterleavedBuffer`) — see SDK
  `docs/dsp.md` — for bridging host rates or running lo-fi internal rates.

## 4. Block processing

- MM calls `update()` **per sample**; 4ms accumulates 64-sample blocks and
  runs the engine once per block (`TapoCore.cc:78-107`,
  `tapo/parameters.hh:36`); UI/param polling throttled to ~1 kHz.
- All control values smoothed with per-block linear ramps
  (`ParameterInterpolator`, `value += increment` per sample).

## 5. Math shortcuts

- LUTs: equal-power crossfade (`epp_lut.hh`), semitone→ratio
  (`stmlib/dsp/units.h:37-46`, two 257-entry tables), shared
  `InterpArray<float,2048>` tables for sin/tan/exp/log
  (`lib/cpputil/util/math_tables.hh`), V/oct and pot-taper LUTs in DLD.
- `SoftLimit(x) = x*(27+x²)/(27+9x²)` — cheap tanh
  (`tapo/stmlib/dsp/dsp.h:80-92`).
- Carmack `fast_rsqrt` (`rsqrt.h:52`); ARM `ssat/usat/vsqrt` intrinsics
  `#ifdef`-guarded with plain-C fallbacks (`dsp.h:94-136`).
- `ONE_POLE`, `SLOPE`, `SLEW`, `Crossfade` macros; Svf as DC blocker at
  10 Hz (`multitap_delay.cc:44`).
- Flags: `-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
  -mvectorize-with-neon-quad -fno-math-errno` — **not** full
  `-ffast-math`; they removed `-funsafe-math-optimizations`.

## 6. Memory constraints

- ~290 MB total plugin RAM on MM (SDK `docs/system-api.md`), shared across
  modules — multi-MB buffers fine, keep modest.
- Build-specific sizes via `#ifdef VCVRACK` / `#ifdef METAMODULE`.
- RobotBoy already has ARM FPU flush-to-zero
  (`src/particules/metamodule_fpu.h`) — denormals in decaying feedback
  tails are a real CPU hazard; use it.

## 7. Code organization

- Pure DSP core (no Rack/HW types) + thin `CoreProcessor` wrapper + 
  one-line VCV model shim. RobotBoy's Loooop follows the same split; 
  Particules uses the VCV-adapter route with a 64-sample block runtime, 
  which is also fine (and is what beads-delay will use).

## 8. Rotary-head pitch shifter (ready-made pattern)

`processors/pitchShift.h:18-51`: two read heads sweep a short delay line
180° out of phase; each amplitude-windowed by a sine LUT so the crossfade
hides the wrap; phase increment from an exp LUT (no pow per sample);
`MultireadDelayLine` fractional linear reads. This is exactly Beads'
"rotary head" shifter.

## 9. Vendored stmlib adaptations

- Everything wrapped in a module-specific nested namespace
  (`TapoDelay::stmlib`) to avoid symbol collisions with other
  Mutable-derived plugins in-process.
- int16 buffer + float math contract preserved; write dither
  `(Random::GetFloat()-0.5f)/8192.0f` (`multitap_delay.cc`).
- Intrinsics `#ifdef TEST`-guarded for desktop builds.

## Top 10 techniques applied to beads-delay

1. int16 or float ring buffer, power-of-two, member-allocated (we start
   from Particules' float `RecordingBuffer`; int16 is the fallback if
   memory bandwidth ever matters).
2. 64-sample block processing inside per-sample `update()`.
3. Per-block linear ramps on every control (feedback, mix, time, ratio).
4. Varispeed read-pointer ramp for time glide + dual-head equal-power
   crossfade for discrete jumps (both, menu-selectable).
5. Two-head sine-windowed rotary pitch shifter with LUT phase increment.
6. Interpolation quality per mode: Hermite main tap; linear acceptable for
   secondary taps.
7. Lo-fi modes at reduced effective rate (write decimation à la
   Particules).
8. `SoftLimit` rational curve + one-pole/Svf DC blocker in feedback.
9. LUTs / block-rate computation for all transcendentals.
10. FPU flush-to-zero on ARM; no allocation/exceptions in audio path;
    work buffers in Impl not stack.
