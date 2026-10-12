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
4. `stmlib.h` defines `M_PI` when a strict `-std=c++20` C library (MetaModule's newlib) hides it.

## Build notes

- Compile with `-DTEST` (selects stmlib's portable `Clip16`/`Sqrt` instead of ARM inline assembly, as VCV's Audible Instruments does) and `-Dstmlib=robotboy_stmlib` (keeps these stmlib symbols from colliding with other Mutable ports linked into the same binary, such as the MetaModule simulator).
- `synth.cc`, `wavetable_granular_player.h` and `wavetable_grain.h` are vendored for completeness; only the wavetable tables and `wavetable_voice.h` are used (by Ondes).
