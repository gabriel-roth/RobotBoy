# Changelog

## [Unreleased]

- **Ondes** — the context menu can now disable whole waveform-bank groups (**1 - Sines**, **2 - Formants**, **3 - Braids**). Disabling a group removes its banks from the Bank knob's range and spreads the remaining groups across the full knob travel; at least one group must stay enabled.
- **Ondes** — the Pitch knob is now a plain linear +-24 semitone control. It no longer stretches extra knob travel around octave/fifth/unison landmarks the way it used to (and the way Particules' and Retours' pitch knobs still do).
- **Onbetap** — the Oversampling menu drops the **1x** option; it's now **CPU efficient** (2x) / **high quality** (4x), and 2x is the default on both VCV Rack and MetaModule (previously MetaModule defaulted to 1x). A patch saved with the old 1x setting now opens at 2x.
- **MetaModule builds now require firmware v2.3 or later.** The plugin is built against SDK v2.3, which lets the firmware catch a plugin that runs out of memory instead of crashing. Adding a Loooop or Löp to a patch with too little memory free now fails cleanly with "Not enough memory to load module" rather than taking the patch down with it. Firmware v2.2 and earlier will refuse to load this build.
- **Loooop & Löp** — changing the sample rate can no longer crash the module. Each looper reserves a 60-second stereo record buffer (about 23 MB at 48 kHz, and twice that at 96 kHz), and on a busy patch the larger buffer may not fit. Previously that killed the module — and on the desktop build it could crash outright, reading from a buffer that had already been freed. Now the looper keeps the buffer it has and carries on with a shorter maximum loop time, telling you the new limit on MetaModule and logging it in VCV Rack.
- **Retours** — fixed a bug where patching only **In L** (mono input, including every Karplus-Strong pluck patch) muted the module for about a quarter-second right after loading and silently detuned the delay time an octave low once it unmuted. The very first sound into a freshly loaded mono patch used to get swallowed entirely.

- **Vespid on MetaModule** — locked to standard accuracy and the charcoal panel. The **Accuracy** and **Panel** context-menu items are gone from the MetaModule build (both remain in VCV Rack), and a patch carrying High accuracy or the gold panel no longer unlocks them there.
- **Vespid on MetaModule** — locked to 1×, where the processor has the least headroom to spare (Vespid has no other oversampling option). Vespid's **Auto** entry is gone on MetaModule (it would only ever have resolved to 1× there); a patch saved with Auto opens at 1×. VCV Rack is unchanged — Vespid still defaults to Auto.
- **Retours** — tap tempo now holds indefinitely, like a normal tap-tempo control: a tempo set by tapping or from a patched clock sticks through a stopped clock, an unpatched cable, or an Interval move, until you use the new **Clear saved tempo** context-menu item (or set a new tempo).
- **Retours** — the Clock light now tracks the clock: when clocked it blinks once per beat, locked to incoming ticks, instead of free-running at the subdivided base Interval (which strobed too fast to read at fast tempos on a subdivided knob).
- **Particules & Retours** — the context-menu **Clear buffer** item is greyed out when the recording buffer is already empty.
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
  (derived from Plaits, in three menu-toggleable groups — Sines, Formants,
  Braids), swept by BANK and POSITION knobs with CV attenuverters and
  interpolation on both axes, a linear ±24 st pitch knob, a V/oct input,
  and a live waveform display. VCV Rack + MetaModule.
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
