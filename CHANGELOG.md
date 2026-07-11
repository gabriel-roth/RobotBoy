# Changelog

All notable changes to **Robot Boy** are documented here. The version in
`plugin.json` is the source of truth; adjust the heading below if you bump it
before tagging.

## [2.0.1] — 2026-07-11

First public release of the combined **Robot Boy** plugin for
[VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule),
collecting four modules from one source tree.

### Loooop / Löp — stereo RAM looper (four playheads / single playhead)
- Overdub write modes: **Add, Replace, Layer, Decay**, plus a **Lock** state that
  makes Record a no-op on the loop contents.
- Punch-in/out write-gain ramps (click-free overdub start/stop).
- Catmull-Rom playback interpolation; one-shot end fade.
- Smoothed level, pan, and dry/wet (~2 ms).
- **Grid** quantization (Off / 4 / 8 / 16 / 32 / 64) on both modules, with
  per-playhead **Exclude from Grid**.
- Panel rework: five-state **Overdub** LED button and a **Grid** knob (Loooop and
  Löp), commands-first context menu, color-named playheads, dark screws, purple
  Löp loop-display strip.
- Static-waveform display caching.
- Sample-rate changes now **preserve a recorded loop** on both VCV and MetaModule.
- Fixes: stale one-shot retrigger ramp on a fresh trigger; Decay tone-filter seed
  when switching write mode mid-pass.

> **Patch compatibility:** Löp patches/presets saved before this release load
> with Overdub/Grid scrambled (the old Write-mode param was absorbed and IDs
> shifted). Re-save any Löp patches you keep.

### MF-20 — Korg MS-20-style filter
- OTA and Korg35 filter revisions with drive.
- C1 quadratic knees on the K35 forward clip (less aliasing at high drive/cutoff).
- Cutoff CV can now sweep **below 20 Hz down to the ~1 Hz core floor**, so deep
  negative CV nearly closes the filter (the knob range still floors at 20 Hz).

### Particules — granular texture processor
- Scale-aware **pitch lock** with root selection.
- Grain-count LED and live input-level readout.
- Grain-trigger pulse on the R output; dry-follows-gain option.
- Reverb idle sleep (CPU drop when reverb sits at 0).
- Context-menu **undo** (VCV).
- Grain-pool overflow now **steals the oldest grain** so the newest events always
  sound at saturation, instead of dropping the trigger.
- NaN-hardening across CV ingestion (density, pitch, position, feedback, dry/wet,
  reverb), with liveness and recovery pinned by tests.

### Packaging & build
- One source tree builds both the VCV Rack plugin and the MetaModule
  `RobotBoy.mmplugin`.
- `plugin.json` gains `sourceUrl` and `manualUrl`; per-module tags and
  descriptions for the VCV Library.
- Version is single-sourced from `plugin.json` (MetaModule CMake derives it);
  `-std=c++20` scoped to C++ only; `make clean` now removes escaped object files.

[2.0.1]: https://github.com/gabriel-roth/RobotBoy/releases/tag/v2.0.1
