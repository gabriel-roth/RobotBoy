# Retours — MetaModule build + headless simulator verification

Task 12 of the retours delay plan. Covers: `.mmplugin` build, headless-simulator
DSP verification (echo delay-time math vs. measured output), and CPU
measurement.

Scripts and patch fixtures live in `tests/retours/mm_sim/` (`gen_burst.py`,
`read_mm_wav.py`, `gen_load_patch.py`, `retours_default.yml`,
`retours_load_16.yml`). WAV outputs are not committed (regenerate with the
commands below); they're multi-MB and fully reproducible.

## 1. `.mmplugin` build

Added to `metamodule/CMakeLists.txt`:
- `${SRC}/src/retours_delay/Retours.cpp` under "VCV-adapter modules".
- The four retours_delay DSP `.cpp` files individually (CMake here doesn't
  glob): `retours_processor.cpp`, `time/base_time.cpp`, `engine/echo_engine.cpp`,
  `pitch/rotary_shifter.cpp` (`mod/` and `env/` are header-only, per the task
  brief, so nothing to add there).
- Include dirs: `${SRC}/src/retours_delay`, `${SRC}/src/retours_delay/dsp/include`.

**Extra include dir needed, not in the original brief:** the first build
attempt failed with `fatal error: buffer/recording_buffer.h: No such file or
directory` from both `retours_processor.h` and `engine/echo_engine.h`. Retours
reuses Particules' `RecordingBuffer` (declared in
`src/particules/dsp/src/buffer/recording_buffer.h`, i.e. under `dsp/src/`, not
`dsp/include/`), and the existing `${SRC}/src/particules/dsp/include` include
dir doesn't reach it. The desktop build already handles this
(`vcv/Makefile:32-33`: `-I../src/retours_delay/dsp/include -I../src/retours_delay/dsp/src -I../src/particules/dsp/src`,
commented "mirrors tests/retours_delay_dsp/CMakeLists.txt's four include dirs").
Mirrored the same two extra include dirs
(`${SRC}/src/particules/dsp/src`, `${SRC}/src/retours_delay/dsp/src`) into
`metamodule/CMakeLists.txt` and the build went clean.

```
cd metamodule
cmake --fresh -B build -G Ninja \
  -DMETAMODULE_SDK_DIR=/Users/gabrielroth/Dev/metamodule-plugin-sdk \
  -DTOOLCHAIN_BASE_DIR=/Users/gabrielroth/Dev/opt/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin
cmake --build build
```

Result: clean build (no warnings from Retours code — the only warnings in the
whole target are pre-existing Particules NaN-macro ones, unrelated).
`RobotBoy.mmplugin` produced (695 KB). No literal `delay_engine`/`delay_mode`/
`DelayEngine` strings introduced anywhere (confirmed by re-running
`tests/test_no_delay_mode.py`, which is scoped to Particules-only files and
was already passing before this task; retours_delay's own file names/symbols
don't touch that guard).

## 2. Headless simulator

Registered the plugin as a simulator built-in (source-compiled — the
simulator can't load `.mmplugin` files), same technique as the prior
Vespid/Wasp task, pointed at this worktree:

```
cd /Users/gabrielroth/Dev/metamodule/simulator
cmake -B build-headless -GNinja -DHEADLESS=ON \
  -Dext_builtin_brand_paths="/Users/gabrielroth/Dev/RobotBoy/.worktrees/worktree-retours delay/metamodule" \
  -Dext_builtin_brand_libname="RobotBoy"
cmake --build build-headless
```

Unlike the Wasp task, no `-DCMAKE_CXX_FLAGS="-DSIMULATOR"` workaround was
needed — `simulator/CMakeLists.txt:307` now defines `SIMULATOR` and
`METAMODULE` unconditionally for the `simulator` target regardless of
`HEADLESS`, so the `#if defined(METAMODULE) && !defined(SIMULATOR)` guard in
`Retours.cpp` (mirroring `Particules.cpp`'s `memalign()` guard) took the
simulator-safe branch correctly on the first try.

Non-GUI simulator built-in (`build/`, not `build-headless/`) was also
reconfigured/rebuilt pointing at this worktree, confirming the plugin links
into the full GUI simulator too (not exercised further — headless is the
verification vehicle here).

## 3. Patch format

Slug is `RobotBoy:Retours` (plugin.json's top-level slug + Retours's `createModel`
slug — matches the precedent's gotcha notes: brand string is the plugin slug
"RobotBoy", not the display name "Robot Boy"). `module_id: 0` is always the
Hub/panel; the real module starts at `module_id: 1`.

Jack IDs (from `Retours.cpp`'s `enum InputId`/`OutputId`): `IN_L_INPUT=0`,
`IN_R_INPUT=1` map to `mapped_ins` panel jacks 0/1; `OUT_L_OUTPUT=0`,
`OUT_R_OUTPUT=1` map to `mapped_outs` panel jacks 0/1.

`static_knobs` used in `retours_default.yml`:
- `param_id: 1` (`DENSITY_PARAM`) = 0.409091
- `param_id: 5` (`FEEDBACK_PARAM`) = 0.3

TIME (`param_id: 2`), PITCH, SHAPE, QUALITY, DRY_WET are left at their
`configParam`/`configSwitch` defaults (0, 0.5, 0, 0, 0.5 respectively).

## 4. Delay-time math and verification

**Density → delay math** (unclocked/manual mode, `base_time.cpp:198-210`,
no clock connected to `CLOCK_INPUT` in this patch):

```
d      = clamp(|density_knob - 0.5| * 2, 0, 1)
base   = buffer_seconds * 2^(-kManualOctaves * d)     // kManualOctaves = 11
mult   = 2^(4 * time_knob)                             // = 1 at time_knob = 0
delay  = clamp(base * mult, kMinDelaySeconds, buffer_seconds)
```

`buffer_seconds` = `kBufferFrames / 48000` = `192000/48000` = 4.0 s at the
default Quality (0 = "Bright digital", decimation 1×, matching `Init()`'s
`kBufferSeconds`).

Solved for an exact 1.0 s delay: `4.0 * 2^(-11d) = 1.0` → `2^(-11d) = 0.25` →
`11d = 2` → `d = 2/11`. `density_knob = 0.5 - d/2 = 0.5 - 1/11 ≈ 0.409091`.
With `time_knob = 0` (mult = 1), expected delay = **exactly 1.000 s
(48000 samples @ 48 kHz)**.

**Test signal:** 20 ms Hann-windowed 440 Hz burst at t=0, ±2 V, 4.0 s total,
stereo (both channels identical), generated by `gen_burst.py` — a burst
rather than a continuous tone makes repeats trivial to locate in time.

**Measured result** (10 ms RMS envelope of the output, `audio_out.wav`):

| Region | Peak time | Peak level | Ratio to previous |
|---|---|---|---|
| Direct/dry burst | t≈0.010 s | 0.3967 | — |
| Echo 1 | t≈1.010 s | 0.3968 | ≈1.00 (dry+wet ≈ dry) |
| Echo 2 | t≈2.010 s | 0.1192 | 0.300 |
| Echo 3 | t≈3.010 s | 0.0358 | 0.300 |

Echoes land at **exactly 1.000 s spacing** (1.010, 2.010, 3.010 — all offset
from the burst's own 0.010 s peak-of-window by the same amount, i.e. true
1.000 s delay to the sample-block granularity of the 10 ms measurement
window), and decay geometrically at **ratio 0.300 per repeat**, matching the
`FEEDBACK_PARAM = 0.3` setting exactly. `min/max = -0.9846/0.9971`, **0 NaN, 0
Inf**. No crash across the full 4 s render.

To regenerate:
```
source ~/Dev/python-scripts/.venv/bin/activate
cd tests/retours/mm_sim
python3 gen_burst.py audio_in.wav
/Users/gabrielroth/Dev/metamodule/simulator/build-headless/simulator \
  -p retours_default.yml -n 192000 -i audio_in.wav -o audio_out.wav
python3 read_mm_wav.py audio_out.wav
```

## 5. CPU measurement

Single cabled `Retours` instance (`retours_default.yml`), silence input (signal
content doesn't affect per-sample cost), 100 simulated seconds
(`-n 4800000`) for a stable reading above timer-resolution noise:

| Run | Process time (100 s sim) | Host-relative load |
|---|---|---|
| 1 | 191 ms | 0.191% |
| 2 | 187 ms | 0.187% |
| 3 | 187 ms | 0.187% |

**~0.19% of one host core (Apple Silicon Mac)**, well under the task's ~15%
concern threshold — no optimization pass needed (linear tap2 / hoisted wrap
checks / LUT shifter windows from the plan are not applied; noted here as
deferred-not-needed rather than skipped-under-pressure).

As in the prior Vespid/Wasp task, this is a host-relative number, not a
real MetaModule (Cortex-A7) hardware measurement — no ARM MM device is
available in this environment, and there's still no shipped module benchmarked
on both this host and real MM hardware to calibrate a conversion factor.
Given the ~0.19% figure is roughly 25-75x below the 15% line (vs. Vespid's
4x-oversampling case, which straddled a 60% line and triggered a real design
decision), this margin is comfortable enough that the host/hardware gap
doesn't change the conclusion.

**16-instance load-test cross-check was inconclusive, not confirmatory:**
`gen_load_patch.py`'s technique (N modules, no cabling, comment inherited from
the Vespid task claiming "the patch player runs process() on every
loaded module regardless of cabling") gave non-monotonic numbers here — N=1
uncabled: 0.171%; N=16 uncabled: 0.014%; N=64 uncabled: 0.074% (all measured
over the same 100 s silence run) — inconsistent with linear scaling in
either direction. By contrast, the single **cabled** instance gave consistent
~0.17-0.19% across both a 4 s run (with real burst input) and a 100 s silence
run. This suggests Retours's cost is not (or not fully) incurred by an
uncabled instance on this simulator/patch-player version — possibly a
cabling-dependent fast path — so the load-test technique that worked for
Vespid does not transfer as a reliable amplifier here. Trusted the
consistent cabled single-instance number instead; flagging the discrepancy
as a simulator-behavior question for whoever next relies on
`gen_load_patch.py`'s "no cabling needed" assumption, rather than a Retours bug.

## Summary

| Check | Result |
|---|---|
| `.mmplugin` build | Clean; needed 2 extra include dirs beyond the brief (`particules/dsp/src`, `retours_delay/dsp/src`) to find `RecordingBuffer` |
| Headless simulator built-in | Clean, no `-DSIMULATOR` workaround needed this time |
| Delay-time math | DENSITY=0.409091, TIME=0 → predicted exactly 1.000 s; measured echoes at 1.010/2.010/3.010 s (1.000 s spacing) |
| Feedback decay | Predicted ratio 0.3/repeat; measured 0.300/0.300 |
| NaN/Inf | 0 / 0 |
| CPU (host-relative, single cabled instance) | ~0.19% of one core, 100 s sim — comfortably under the 15% threshold |
| `test_no_delay_mode.py` | Pass (unaffected — scoped to Particules files) |
| `tests/run.sh` | All pass (55 + 8 assertions/tests reported) |
