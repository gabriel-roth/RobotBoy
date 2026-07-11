# Robot Boy regression tests

Offline, host-free C++ tests for the DSP/rendering code in `src/`. No Rack
engine, no MetaModule runtime — just plain `g++` executables exercising the
pure-DSP classes/headers directly. This is a safety net for the merge, not a
full test suite: it only covers what was portable from each source repo.

## Running

```
cd tests && ./run.sh
```

`run.sh` builds and runs every `tests/{mf20,loooop,particules}/test_*.cpp`
against `../src`, printing PASS/FAIL per assertion and exiting non-zero if
any binary fails or returns non-zero.

If a test needs a non-header-only `.cpp` from `src/` linked in (e.g. Loooop's
`LoopEngine.cpp`), drop a sibling `<test>.cpp.extra` file next to it listing
one extra source path per line, relative to `tests/`. `run.sh` picks it up
automatically — see `tests/loooop/test_loop_engine.cpp.extra` and
`tests/loooop/test_display_renderer.cpp.extra` for examples.

## What's ported

| Dir | Test | Source repo | Covers |
|---|---|---|---|
| `mf20/` | `test_mf20.cpp` | `~/Dev/filter/test_mf20.cpp` | `MF20Filter.hpp` (Korg MS-20 OTA/K35 filter emulation) — header-only, 30 assertions |
| `mf20/` | `test_module_dsp.cpp` | `~/Dev/filter/test_module_dsp.cpp` | `dsp_utils.hpp` (`OnePoleSmoother` etc.) — header-only, 8 assertions |
| `loooop/` | `test_loop_engine.cpp` | `~/Dev/Loooop/tests/loop_engine_test.cpp` | `dsp/LoopEngine.{hpp,cpp}` — record/play, speed, reverse, one-shot, jitter, overdub, per-head params, peak/display snapshots |
| `loooop/` | `test_display_renderer.cpp` | `~/Dev/Loooop/tests/display_renderer_test.cpp` | `display/LoopWaveformRenderer.{hpp,cpp}` — waveform/lane rendering, stereo bands, level-aware height |
| `particules/` | `test_pitch_notch_map.cpp` | `~/Dev/particules/tests/test_pitch_notch_map.cpp` | `pitch_notch_map.hpp` — pitch-knob-to-semitone mapping, notch round-trips, monotonicity |

Only include paths were changed (pointing at the new `src/` locations);
test logic and assertions are untouched.

## Test lanes

This repo has two independent test lanes:

- **Lane 1 — `tests/run.sh`** — zero-dependency `g++` DSP tests (MF-20, Loooop,
  and the small Particules pitch-map), plain-assert style, followed by a
  `python3 -m unittest discover` pass over `tests/test_*.py` (identity/build
  guard tests — plugin metadata, slug parity between `plugin.json` and
  `metamodule/plugin-mm.json`, and no-delay-mode symbol removal). Runs
  anywhere, no build system needed beyond a `python3` on `PATH`.
- **Lane 2 — `tests/particules_dsp/run.sh`** — the Particules granular-DSP
  Catch2 suite (CMake + CTest). Covers the granular / delay / reverb / quality
  / pitch DSP that powers Particules. Catch2 is vendored (amalgamated, v3.5.2),
  so the lane builds and runs fully offline. Run it with:

  ```
  ./tests/particules_dsp/run.sh
  ```

## What was intentionally skipped, and why

- **`~/Dev/Loooop/test/`** (singular, not `tests/`): Python scripts
  (`*_wiring_test.py`, `mm_click_test.py`, `sync_positions_test.py`) that
  drive a live VCV/MetaModule host or the headless simulator against real
  `.wav`/patch fixtures. These need a running host process, not just the
  DSP headers — out of scope for an offline `g++` harness. Not ported.
- **Any test instantiating the `Particules` Rack `Module`**: its constructor
  calls `APP->engine->getSampleRate()`, which segfaults without a live Rack
  `Context`. `~/Dev/particules/tests/` only ever had the one pure-DSP test
  (`test_pitch_notch_map.cpp`), so nothing was actually excluded here — but
  the constraint is why no Particules module-level test exists in this repo.
- **`~/Dev/filter/test_poly.cpp`**: exists in the source repo but wasn't in
  this task's named scope (`test_mf20.cpp` / `test_module_dsp.cpp` only).
  It is self-contained pure DSP (`VoiceEngine` in `engine.hpp`, no Rack
  dependency) and would likely port cleanly with the same include-path
  treatment — a reasonable candidate to add in a future pass, deliberately
  left out here to stay in scope.
- **`~/Dev/particules/nosuch_texture/tests/`**: a much larger Catch2 suite
  (`test_grain.cpp`, `test_reverb.cpp`,
  `test_pitch_quantizer.cpp`, `test_auto_gain.cpp`, `test_buffer.cpp`, etc.)
  covering the `beads_dsp` engine that lives in
  `src/particules/dsp/`. It's real DSP coverage and squarely the kind of
  thing this task's "prefer pure-DSP tests" guidance points at — but it (a)
  wasn't in the brief's named paths, (b) depends on Catch2, which isn't
  wired into this repo's build at all, and (c) is ~14 files, materially
  larger than a "port the existing tests" pass. Left unported and flagged
  here as the best candidate for a dedicated follow-up task (vendor Catch2
  or rewrite the assertions against this repo's plain `check()`/`report()`
  harness style, fix its `beads/...` and `grain/...` include paths against
  `src/particules/dsp/include` and `src/particules/dsp/src`).
- **Ondes/Retours-dependent tests**: none of the source repos' test
  directories referenced those excluded modules, so there was nothing to
  filter out on that basis.

## Current results

All 5 ported test binaries build and pass: 201 `ok:`/`PASS` lines across
MF-20 (30 + 8), Loooop (2 suites), and Particules — `run.sh` exits 0.
