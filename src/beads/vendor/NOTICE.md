# Vendored Mutable Instruments code

This directory holds the DSP of Mutable Instruments **Beads**, designed and written by Émilie Gillet, plus the subset of Mutable's **stmlib** it depends on. Particules (granular mode), Retours (delay mode) and Ondes (wavetable voice) run on it.

- Upstream: <https://github.com/pichenettes/eurorack>, Beads source released in commit `d8f473915232af74347762b6365202bfd26f73f6` (vendored from `f4cfa7697095a28d292bff758beb51deb49b5a86`, identical `beads/` tree).
- stmlib submodule: `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`.
- Licence: MIT (see `LICENSE`, copied from stmlib; the Beads files carry the same notice in their headers).

## Local changes

All local changes are in `robotboy.patch` and are marked `// RobotBoy:` in the source. `update.sh /path/to/eurorack` recreates this directory from upstream and re-applies the patch.

1. `Processor` can pin granular or delay mode (`set_mode_lock`) instead of following the Size knob, skip the reverb (`set_reverb_enabled`), clear the active buffer (`ClearBuffer`), and report its active grain count (`num_active_grains`) and the per-frame gate of the last block (`gate_out`).
2. `Parameters::pitch_lock`: an optional quantizer applied to each grain's pitch, in semitones, when the grain starts.
3. The packed 12-bit delay-line items force the GCC bitfield layout on MinGW, and every item size is checked with a `static_assert`.
4. `Tap` skips the delay-line reads of a tap whose gain is zero (it still advances its pitch-shifter phase). The output is bit-identical; delay mode costs much less CPU, because most of its 16 taps are silent at any moment.
5. `stmlib.h` defines `M_PI` when a strict `-std=c++20` C library (MetaModule's newlib) hides it.
6. Read-only accessors for Lucarne's grain display: the delay line's write pointer and samples, each grain's position, speed, stereo position, envelope level, length and a per-slot start counter, and the active granular processor (`Processor::VisitGranular`). The output is bit-identical.
7. Tap and play-head accessors for Lucarne's Retours view (read-only).
8. `Reverb` sleeps: when the send is exactly 0, both filter states are exactly 0 and the last full pass over its memory wrote only zeros, it skips the tank and keeps only the level glides, write pointer and LFOs moving (`FxEngine::Advance`). The output is bit-identical, so it only saves CPU while Reverb is at 0 and the tail has decayed to nothing. Needs flush-to-zero (as VCV Rack sets it) for the tape media to reach sleep; without it a filter state sticks at the smallest denormal and the reverb just keeps running.

## Build notes

- Compile with `-DTEST` (selects stmlib's portable `Clip16`/`Sqrt` instead of ARM inline assembly, as VCV's Audible Instruments does) and `-Dstmlib=robotboy_stmlib` (keeps these stmlib symbols from colliding with other Mutable ports linked into the same binary, such as the MetaModule simulator).
- `synth.cc`, `wavetable_granular_player.h` and `wavetable_grain.h` are vendored for completeness; only the wavetable tables and `wavetable_voice.h` are used (by Ondes).
