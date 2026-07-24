# Changelog

## [Unreleased]

- **Retours** — tap tempo now holds indefinitely, like a normal tap-tempo control: a tempo set by tapping or from a patched clock sticks through a stopped clock, an unpatched cable, or an Interval move, until you use the new **Clear saved tempo** right-click item (or set a new tempo).
- **Retours** — the Clock light now tracks the clock: when clocked it blinks once per beat, locked to incoming ticks, instead of free-running at the subdivided base Interval (which strobed too fast to read at fast tempos on a subdivided knob).
- **Particules & Retours** — the right-click **Clear buffer** item is greyed out when the recording buffer is already empty.
- **Retours** — removed the **Input trim** and **Doppler slew** menu sliders. Input trim is baked at 0 dB (unity) and the Doppler slew at 0.285 s.
- **Retours** — removed the **Envelope feedback tap** menu option. The feedback tap is permanently post-envelope, matching hardware Beads (the Shape envelope shapes the fed-back signal, so gated repeats compound).
- **Vespid** — removed the **Input trim** and **Output level** menu items. Both were unity by default and are now fixed there (no gain staging in the menu).
- **Particules/Retours** — recording buffer now packs samples at true bit width and channel count — Sunny/Scorched run at 24 kHz with real 12-bit/8-bit µ-law storage; mono input doubles buffer length (up to 64 s).
- **Loooop** — Level is now mix-only, matching the manual: each head's own
  Out L/R always carries the full-level signal, so a head can be removed from
  the Mix (Level fully CCW) while still feeding its own outputs.
- **Onbetap** — Polivoks-style stereo multimode filter, new module.
- **Vespid** — new module: EDP Wasp-style CMOS state-variable filter, with
  British (original '78 Wasp, verge-of-oscillation) and German (Doepfer A-124
  self-oscillation mod, true self-oscillation) character modes.
- **Retours** — new module: a delay based on the hidden delay mode of Mutable
  Instruments Beads. Manual, clocked, or tap-tempo base time with
  subdivisions (INTERVAL, down to audio rates for Karplus-Strong playing),
  delay-time multiplier that becomes a beat slicer under SLICE (TIME),
  rotary-head pitch shifter inside the feedback path (PITCH, ±24 st with
  notches), tempo-synced repeat envelope (SHAPE), per-quality feedback
  limiting, the four Beads quality modes, and slow-random
  attenurandomizers. VCV Rack + MetaModule.
- **Ondes** — new module: a morphing wavetable oscillator inspired by the
  wavetable mode of Mutable Instruments Beads. 24 banks of 8 waveforms
  (derived from Plaits), swept by BANK and POSITION knobs with CV
  attenuverters and interpolation on both axes, a pitch knob notched at
  octaves/fifths/unison, a V/oct input, and a live waveform display.
  VCV Rack + MetaModule.
- Particules: fixed a macOS-simulator portability issue in the `memalign()`
  usage guard (real firmware and VCV Rack behavior unchanged).
- **Particules & Retours** — fixed an inverted quality-mode fidelity ladder
  inherited from the upstream Beads DSP: Sunny tape and Scorched cassette had
  their decimation swapped. Now degradation increases monotonically Bright →
  Cold → Sunny → Scorched (buffer 4/8/16/32 s), so Scorched cassette is the
  most-degraded, longest mode. Anti-aliasing filters were re-tuned to each
  mode's new rate, Sunny tape gained gentle wow/flutter, and the internal
  quality enum was renamed to match the Beads labels.
- **Loooop & Löp — param IDs now match between VCV and MetaModule.** The VCV
  `ParamId` order was reordered to be identical to the MetaModule `Elem` /
  `Elements` order, so a patch authored on one host maps its knob values onto
  the correct controls on the other. Previously the two orders diverged (VCV
  kept Crossfade and the per-head Trigger/Speed-V-Oct/Grid-exclude menu params
  interleaved per head; MetaModule grouped them into a trailing "Options"
  block), so a VCV patch loaded on MetaModule scrambled every knob from the
  Crossfade slot onward. **Patches saved with an earlier VCV build will load
  with shifted param values** (same class of break as the earlier globals-first
  reorder); MetaModule-native patches are unaffected.

## [2.0.1] — 2026-07-11

Initial release.

Robot Boy is a plugin for [VCV Rack](https://vcvrack.com) and the
[4ms MetaModule](https://4mscompany.com/metamodule), collecting four modules:

- **Loooop** — stereo RAM looper with four independent playheads
- **Löp** — single-playhead version of Loooop
- **MF-20** — Korg MS-20-style filter (OTA and Korg35 revisions)
- **Particules** — granular texture processor

[2.0.1]: https://github.com/gabriel-roth/RobotBoy/releases/tag/v2.0.1
