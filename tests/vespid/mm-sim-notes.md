# Vespid — MetaModule build + headless simulator verification

Task 5 of the Wasp-filter plan. Covers: `.mmplugin` build, headless-simulator
DSP verification (WAV + spectrum), CPU measurement across oversampling
factors, and the resulting MM-side defaults/menu changes.

Scripts and patch fixtures used below live in `tests/vespid/mm_sim/`
(`gen_saw.py`, `read_mm_wav.py`, `gen_load_patch.py`, `vespid_*.yml`). WAV outputs
are not committed (regenerate with the commands below); they're multi-MB and
fully reproducible.

## 1. `.mmplugin` build

`METAMODULE` is defined for the whole `RobotBoy` static-lib target by the SDK's
`create_plugin()` (`metamodule-plugin-sdk/plugin.cmake:105`,
`target_compile_definitions(${LIB_NAME} PRIVATE METAMODULE)`, where `LIB_NAME`
is our `SOURCE_LIB RobotBoy` from `metamodule/CMakeLists.txt`) — confirmed by
grepping `Particules.cpp`'s existing `#ifdef METAMODULE` guards, which compile
correctly in the same target. Vespid's own `#ifndef METAMODULE` slider
guards are therefore exercised correctly by this build.

```
cd metamodule
cmake --fresh -B build -G Ninja \
  -DMETAMODULE_SDK_DIR=/Users/gabrielroth/Dev/metamodule-plugin-sdk \
  -DTOOLCHAIN_BASE_DIR=/Users/gabrielroth/Dev/opt/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin
cmake --build build
```

Result: clean build, no Vespid-related errors (the module's new source
has no `try/catch/throw`/iostream patterns needing MM-incompatibility fixes).
`RobotBoy.mmplugin` produced (703 KB); `tar -tf` shows
`RobotBoy/Vespid.png` and `RobotBoy/Vespid-gold.png` bundled
correctly (metamodule/assets and metamodule/plugin-mm.json already had
Vespid entries from a prior task — no scaffolding needed).

Also rebuilt the VCV Rack `.dylib` (`make -C vcv RACK_DIR=$HOME/Dev/Rack-SDK`)
to confirm the `#ifdef METAMODULE` menu split didn't break the desktop build —
compiles clean (pre-existing unrelated deprecation warnings from
`createIndexSubmenuItem`'s lambda captures, not from this change).

## 2. Headless simulator

Registered the plugin as a simulator built-in (source-compiled, not
`.mmplugin` — the simulator can't load `.mmplugin` files):

```
cd /Users/gabrielroth/Dev/metamodule/simulator
cmake --fresh --preset headless \
  -Dext_builtin_brand_paths="/Users/gabrielroth/Dev/RobotBoy/.worktrees/worktree-wasp/metamodule" \
  -Dext_builtin_brand_libname="RobotBoy" \
  -DCMAKE_CXX_FLAGS="-DSIMULATOR"
cmake --build build-headless
```

**Simulator-repo gap found (not a Vespid bug):** `simulator/CMakeLists.txt`
only does `add_compile_definitions(SIMULATOR)` in the *non-headless* branch
(line 31); the `HEADLESS` branch never defines `SIMULATOR` project-wide. This
breaks `Particules.cpp:187` (`#if defined(METAMODULE) && !defined(SIMULATOR)`,
guarding a `memalign()` call that doesn't exist on macOS) — the headless build
took the real-hardware branch and failed with "use of undeclared identifier
'memalign'". Worked around it **without touching the simulator repo**
(kept it clean, per the build-simulator skill's "keep this checkout on clean
main" rule) by passing `-DCMAKE_CXX_FLAGS="-DSIMULATOR"` at configure time —
confirmed via `git status`/`git submodule status` before and after that the
metamodule checkout stayed untouched. This is a pre-existing simulator-repo
issue outside Vespid's scope; flagging here rather than fixing it there.

**Patch-format gotchas hit while building the test patch (worth recording for
next time):**
- The headless `WavReader` (`simulator/src/headless/wav_file.hh`) is a strict,
  minimal parser: RIFF/WAVEfmt / fmt-chunk (0x10 or 0x12 bytes, format 1=PCM or
  3=float32) / `data` chunk immediately after — no `fact`/`LIST` chunks
  tolerated. `scipy.io.wavfile` inserts a `fact` chunk for float32 data, which
  this reader chokes on. `gen_saw.py` hand-rolls the exact byte layout instead.
- `HostFileIO::normalize_path` mishandles absolute paths (an input starting
  with `/` becomes `"." + path`, which doesn't resolve) — pass `-p`/`--in`/`--out`
  as paths **relative to the simulator directory** (e.g.
  `../../RobotBoy/.worktrees/worktree-wasp/tests/vespid/mm_sim/foo.yml`),
  matching the README's own example.
- **Module ID 0 is always the Hub/panel module** (`patch_player.hh`:
  `modules[0] = ModuleFactory::create(PanelDef::typeID)`; the user-module loop
  starts at `i = 1`). A `module_slugs: {0: 'RobotBoy:Vespid'}` patch
  silently instantiates *nothing* — `module_id 0`'s slug is discarded, and
  wiring a panel jack to `module_id 0` instead echoes the raw panel input
  (`get_output(Jack)`: `if (out.module_id == 0) return panel_in_vals[...]`).
  This bit us hard: an initial "default settings" test showed a suspicciously
  exact 2x-gain passthrough with *identical* spectra for every patch variant,
  including a deliberately bogus module slug — the tell that nothing was
  actually being instantiated. Real user modules must start at `module_id: 1`.
- The registered brand string is the **plugin's `slug`** (`plugin.json`'s
  top-level `"slug": "RobotBoy"`), not the display brand name ("Robot Boy"
  with a space) — confirmed by reading `Plugin.cpp:23`
  (`brand = model->plugin->slug`) and `ext_plugin_builtin.hh`
  (`init_RobotBoy(&internal_plugins.emplace_back("RobotBoy"))`). Correct
  patch slug string: `RobotBoy:Vespid`.

Once those were fixed, patches loaded cleanly with no "not found" module
errors and real (non-hub-echo) DSP output.

## 3. WAV / spectrum verification

Test signal: naive (non-bandlimited) 110 Hz sawtooth, ±5 V, 48 kHz, 3 s,
generated by `gen_saw.py`, fed to Audio In L+R (both channels identical, so
the patch runs Vespid's true-stereo path, not the R-normalled-to-L
shortcut). Output: LP L/R captured via `mapped_outs`.

**Default patch** (`vespid_default.yml`: Tame, Res=0, Freq≈750 Hz default, no
`static_knobs`/`vcvModuleStates` overrides — auto oversampling):
- Non-silent (min/max ≈ -4.9/+6.2 V), 0 NaN, 0 Inf.
- Spectrum (relative to the 110 Hz fundamental, 0 dB reference): 220 Hz
  -11.3 dB, 440 Hz -26.8 dB, 880 Hz -36.5 dB, 2200 Hz -57.5 dB, 8800 Hz
  -90.1 dB — steep rolloff far exceeding the raw saw's natural -6 dB/octave,
  confirming genuine lowpass action around the ~750 Hz cutoff.

**Screaming + Resonance 0.95 patch** (`vespid_res_screaming.yml`:
`static_knobs: {param_id: 1 (RES_PARAM), value: 0.95}`,
`vcvModuleStates: {data: '{"screaming": true}'}`):
- Non-silent (min/max ≈ -4.9/+6.4 V), 0 NaN, 0 Inf.
- Spectrum shows a clear resonant bump: 550 Hz is attenuated only -0.5 dB
  (vs. -32 dB at the same frequency in the default/no-resonance patch) while
  330/440/660 Hz sit at -7 to -10 dB — the expected emphasis ridge near the
  cutoff at high resonance, distinct from the monotonic default rolloff.

Both spectra re-verified after the auto-OS-cap code change (§5) with auto now
resolving to 2x on MM instead of 4x — same qualitative shape, confirming 2x is
still musically adequate for both patches.

To regenerate:
```
source ~/Dev/python-scripts/.venv/bin/activate
cd tests/vespid/mm_sim
python3 gen_saw.py audio_in.wav
cd /Users/gabrielroth/Dev/metamodule/simulator
D=../../RobotBoy/.worktrees/worktree-wasp/tests/vespid/mm_sim
build-headless/simulator -p $D/vespid_default.yml -n 144000 --in $D/audio_in.wav --out $D/audio_out_default.wav
build-headless/simulator -p $D/vespid_res_screaming.yml -n 144000 --in $D/audio_in.wav --out $D/audio_out_res_screaming.wav
```

## 4. CPU measurement (1x / 2x / 4x oversampling)

The headless binary reports "Effective load (single core)" = wall-clock
process time / simulated playback time, on **this host** (Apple Silicon Mac),
not real MetaModule (Cortex-A7) hardware — there's no ARM MM device available
in this environment. Single-instance measurements at normal (few-second)
durations were too fast to resolve (0-3 ms for 3 s of audio, i.e. below timer
resolution) — bumped to 300 simulated seconds (`-n 14400000`, silence input,
since the filter's per-sample cost isn't signal-dependent) for a stable
reading, true-stereo patch (both L and R connected, the more expensive path):

| Oversampling | Process time (300 s sim) | Host-relative load |
|---|---|---|
| 1x (`vespid_os1.yml`) | ~2090 ms | ~0.70% |
| 2x (`vespid_os2.yml`) | ~7067 ms | ~2.36% |
| 4x (`vespid_default.yml`, pre-cap auto) | ~15433 ms | ~5.14% |
| 4x, mono (R normalled to L, `vespid_os4_mono.yml`) | ~7836 ms | ~2.61% |

Each figure is the average of 2 repeated runs (all within ~1% of each other —
stable). A 16-instance load-test patch (`vespid_load16_os{1,2,4}.yml`,
`gen_load_patch.py`) cross-checked that per-instance cost scales linearly with
instance count (no shared-overhead artifact skewing the single-instance
numbers).

**Observation:** cost does *not* scale linearly with the oversampling factor
(4x costs ~7.4x what 1x costs, not 4x; 2x costs ~3.4x what 1x costs, not 2x).
Reading `engine.hpp`/`wasp_dsp_utils.hpp` explains why: each oversampling step
adds `HalfbandUp`/`HalfbandDown` stages on top of the extra `WaspFilter::process()`
calls, and those halfband filters are a 47-tap direct-form FIR implemented via
a literal array-shift history buffer (`wasp_dsp_utils.hpp:71-104`,
`for (i = 46; i > 0; --i) hist[i] = hist[i-1]`) rather than a ring buffer —
the code comment there explicitly calls out the unclaimed 2x multiply-count
savings from the taps' even-index zeros as "cheap enough here." At 4x this adds
3 upsample + 9 downsample calls per channel per host sample, each doing two
O(47) shifts — real, measurable overhead, not just noise. **This FIR
inefficiency is a legitimate follow-up efficiency item** (switching to a ring
buffer, and/or skipping the known-zero taps, would cut this cost roughly in
half) but is out of scope for this task (touches shared DSP-core code from
Tasks 1-4, not something to change unreviewed while just trying to measure it).

**No real-hardware calibration point was available** to convert the above
host-relative percentages into true MetaModule-core percentages. The one
cross-referenceable data point in this repo (`docs/superpowers/specs/2026-07-09-feature-and-perf-backlog-design.md:320`)
gives the real MM Cortex-A7's per-sample budget as ~16,700 cycles at 48 kHz
(≈800 MHz core), but that's a cycle *budget*, not a host-to-target performance
ratio — there's no shipped module's CPU number benchmarked on both this host
and real MM hardware to calibrate against. Given a plausible (if
unconfirmed) 5-15x raw-throughput gap between this Apple Silicon core and the
MP1's Cortex-A7 for this kind of scalar/branchy floating-point code, the
measured 4x/stereo worst case (~5.14% of this host's core) scales to roughly
25-75% of one real MM core — straddling the task's 60% "too hot" line with
real uncertainty on both sides, not a comfortable margin.

## 5. Decision: cap MM auto-oversampling at 2x

Given §4's straddling uncertainty (no confirmed comfortable margin under 60%,
and a real ~7x cost jump from 1x to 4x that is not just measurement noise),
the safer default is chosen: **MetaModule's Auto oversampling policy is capped
at 2x** (was 4x, matching desktop) via a compile-time
`#if defined(METAMODULE)` branch in `updateOversampling()`
(`src/Vespid.cpp`). 1x/2x/4x remain manually selectable from the
Oversampling context-menu on both hosts (that submenu already used
`createIndexSubmenuItem`, which is portable — no `#ifdef` needed there) for
users who know their patch/CPU budget can take 4x. This mirrors the project's
own precedent for a similarly-inconclusive CPU question
(`2026-07-09-feature-and-perf-backlog-design.md`'s Loooop P2 decision:
"napkin math... too small to justify... profiling session," keep the safe
default) — an on-device profiling pass on real MM hardware would sharpen this
if/when available, but isn't a blocker for shipping the conservative default.

2x was re-verified (§3) to still produce a correct, sane-sounding lowpass and
resonant emphasis, so this is not a functional regression — just a lower
default aliasing-suppression ceiling on MM.

## 6. MM discrete menu fallbacks (Input trim / Inverter bandwidth)

MM users previously had **no menu access at all** to Input trim or Inverter
bandwidth — both were `ui::Slider`-based Quantities gated
`#ifndef METAMODULE` (MetaModule's context menu has no slider widget). Added
MM-side discrete-choice fallbacks in `src/Vespid.cpp`'s
`appendContextMenu()`, following the exact precedent in
`src/particules/Particules.cpp` (`ManualGainItem::createChildMenu()`'s
`#ifdef METAMODULE` 0-32 dB list vs. the `#else` `ManualGainSlider`):

- **Input trim**: {-12, -6, 0, +6, +12 dB} via `createIndexSubmenuItem`,
  snapping to the nearest of the five values when displaying current state.
- **Inverter bandwidth**: {60, 80, 120, 200, 300 kHz}, same pattern.

Persistence is unchanged: both the MM discrete menu and the VCV slider read/
write the same `_inputTrimDb`/`_fPole` floats and the same JSON keys
(`inputTrimDb`, `fPole`) via `dataToJson`/`dataFromJson` — patches roundtrip
identically between hosts, and existing `.vcv`/MM patch files are unaffected
(no field renamed or reinterpreted).

## Summary

| Check | Result |
|---|---|
| `.mmplugin` build | Clean, Vespid compiles with no MM-incompatible patterns |
| VCV `.dylib` build | Clean, unaffected by the `#ifdef METAMODULE` menu split |
| Simulator built-in + headless build | Clean (after the `-DSIMULATOR` headless-config workaround, simulator repo untouched) |
| WAV output, default settings | Non-silent, band-limited lowpass around ~750 Hz, 0 NaN/Inf |
| WAV output, Screaming + Res 0.95 | Non-silent, resonant emphasis near cutoff, 0 NaN/Inf |
| CPU, 1x/2x/4x (host-relative, stereo) | ~0.70% / ~2.36% / ~5.14% of one host core (300 s sim) |
| MM default decision | Auto capped at 2x on MetaModule (`#if defined(METAMODULE)`); manual 1x/2x/4x still available |
| MM menu gap closed | Input trim + Inverter bandwidth discrete submenus added for MM |
