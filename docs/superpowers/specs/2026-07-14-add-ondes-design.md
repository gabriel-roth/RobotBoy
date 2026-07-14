# Add Ondes to Robot Boy — Design

**Date:** 2026-07-14
**Branch:** `add-ondes`

## Goal

Extract the **Ondes** module (a wavetable oscillator) from the archived
`archive/particules` repo and add it as a first-class module of the Robot Boy
plugin, alongside Loooop, Löp, MF-20, and Particules. Build for both VCV Rack
and MetaModule. Ship a manual doc and a README entry.

This task was run autonomously (no approval checkpoints per the user's
standing instruction); the decisions below were made and documented rather
than asked.

## Source material

`archive/particules/` (untouched — we copy out of it) contains:

- `src/Ondes.cpp` — the module: 5 params, 3 inputs, 1 output.
- `src/RackWavetableProvider.hpp` — adapts `WavetableData` to the DSP's
  `WavetableProvider` interface.
- `src/WavetableData.hpp` — 24 banks × 8 waveforms × 256 samples of
  Plaits-derived wavetable data (`static constexpr`, `namespace WavetableData`).
- `nosuch_texture/beads_dsp/src/wavetable/wavetable_oscillator.{h,cpp}` — the
  oscillator DSP (bilinear bank×wave morph, linear phase interpolation), in the
  archive's `beads` namespace.
- `res/Ondes.svg` — 30.48 mm (6 HP) panel. Control positions live on a hidden
  `components` layer; the visible background draws only text labels (ONDES,
  PITCH, BANK, POSITION, OUT) and an OUT box — **no knob rings are drawn**.

## What Ondes does

A morphing wavetable oscillator:

- **Pitch** knob (V/oct-notched, ±24 st via the shared `PitchParamQuantity`) +
  **V/oct** input.
- **Bank** knob + CV (with attenuverter): selects across the 24 banks.
- **Position** knob + CV (with attenuverter): selects the waveform within a bank.
- Bank and Position are bilinearly interpolated across the four neighbouring
  corner waveforms; phase is linearly interpolated. Mono output scaled to ±5 V.

## Key decisions

1. **Stock Rack widgets, not BogAudio.** The archive used `bogaudio::KnobNN`
   / `Port24` wrappers backed by BogAudio SVG assets. Robot Boy's existing
   modules (e.g. Particules) use stock Rack widgets, and there is no
   `res/bogaudio` asset directory here. Because the panel draws no knob rings,
   widget style is purely cosmetic sizing. We map to stock widgets matching the
   archive's pixel sizes and Robot Boy's conventions:
   - Pitch (was `Knob38`, 38 px) → `RoundLargeBlackKnob` (38 px)
   - Bank / Position (was `Knob29`, 29 px) → `RoundBlackKnob` (30 px)
   - Bank / Position CV amount (was `Knob16`, 16 px) → `Trimpot` (~15 px)
   - All ports (was `Port24`, 24 px) → `PJ301MPort` (~24 px)
   - Screws → `ScrewBlack` (two: top-left, bottom-right — as the archive did)

   This removes the BogAudio dependency entirely (no vendored widget header, no
   `res/bogaudio` assets). Control positions from the panel's `components`
   layer are preserved exactly.

2. **Port the DSP into the `particules_dsp` namespace.** Robot Boy already
   vendored the granular DSP as `particules_dsp` (renamed from `beads`). The
   wavetable oscillator needs `Clamp`, `Crossfade`, `SemitonesToRatio`,
   `InterpolateLinear` — all already present in
   `src/particules/dsp/src/util/{dsp_utils,interpolation}.h` under
   `particules_dsp`. So we port the oscillator into `particules_dsp` rather than
   reintroducing a second `beads` namespace. `StereoFrame` also already exists
   there.

3. **Extend the vendored `types.h`.** The oscillator's provider interface needs
   `WavetableProvider` and `kWavetableSize`, which the archive kept in its
   `beads/types.h` but which are absent from Robot Boy's
   `src/particules/dsp/include/particules_dsp/types.h`. Add both there (in
   `particules_dsp`). `kWavetableSize = 256`.

4. **File layout** (all new files under `src/particules/`):
   - `dsp/src/wavetable/wavetable_oscillator.h` + `.cpp` — ported oscillator.
   - `WavetableData.hpp` — copied verbatim (kept in its own
     `namespace WavetableData`; included by exactly one TU).
   - `RackWavetableProvider.hpp` — implements `particules_dsp::WavetableProvider`.
   - `Ondes.cpp` — the module + widget.
   - `res/Ondes.svg` — copied verbatim to the repo-root `res/` (the canonical
     panel dir; `vcv/res` is synced from it at build time).

5. **Registration.** Add `extern Model* modelOndes;` to `src/plugin.hpp`.
   Register it in **both** entry points in `src/plugin.cpp`: the VCV `init()`
   and the MetaModule `init_RobotBoy()` (via `p->addModel(modelOndes)`, the same
   adapter path Particules uses).

6. **Plugin metadata.** Add an Ondes entry to `plugin.json` (slug `Ondes`,
   name `Ondes`, description `Wavetable oscillator`, tags `Oscillator`,
   `Wavetable`). `vcv/plugin.json` is regenerated from the root copy at build
   time, so no manual edit there.

7. **Build wiring.**
   - **VCV** (`vcv/Makefile`): no change needed. It already globs
     `../src/particules/*.cpp` (picks up `Ondes.cpp`) and
     `../src/particules/dsp/src/*/*.cpp` (picks up `wavetable/*.cpp`).
   - **MetaModule** (`metamodule/CMakeLists.txt`): CMake lists sources
     explicitly, so add `Ondes.cpp` and `wavetable/wavetable_oscillator.cpp`.
   - **MetaModule** (`metamodule/plugin-mm.json`): add the Ondes entry to
     `MetaModuleIncludedModules`.
   - **MetaModule** asset: generate `metamodule/assets/Ondes.png` from
     `res/Ondes.svg` (same pipeline used for the other module PNGs).

8. **Version.** Leave `plugin.json` at `2.0.1`. A version bump belongs to the
   `release-robotboy` flow, which is out of scope here. Noted for the user.

## Out of scope / follow-ups

- **`param_ranges.json`** in the separate `yml-to-vcv` project (the
  `add-vcv-plugin-params` skill) — a different repo; not touched here.
- **Version bump / GitHub release** — user's `release-robotboy` flow.
- **Screenshots** — the manual and README get placeholder image references;
  the user captures real screenshots (GUI-sim rendering is a manual step per
  project convention).

## Verification

- **Hard gate:** `make -C vcv` builds `plugin.dylib` cleanly with Ondes
  registered; `plugin.json` validates (4 → 5 modules).
- **DSP smoke test:** a small headless check that the ported oscillator
  produces non-silent, bounded output for a mid-pitch / mid-bank / mid-position
  setting (guards against a botched namespace port).
- **MetaModule:** attempt the CMake build; if it compiles and links, good.
  GUI-sim behaviour is verified by the user, not by an agent (project
  convention — GUI checks go to a user checklist).
- Panel/GUI appearance and knob feel: user checklist item.

## Testing strategy

The oscillator is deterministic pure DSP. We add a minimal standalone test
(compiled outside Rack) that instantiates `particules_dsp::WavetableOscillator`
with `RackWavetableProvider`, runs a block at a fixed pitch/bank/position, and
asserts output is non-zero and within ±1 before the ±5 V scale. No Rack/GUI
dependency, so it runs in CI-style isolation. The module wiring itself (param
counts, registration) is verified by the successful VCV build and `plugin.json`
schema.
