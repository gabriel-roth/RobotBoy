# Robot Boy regression tests

Offline, host-free C++ tests for the DSP/rendering code in `src/`. No Rack
engine, no MetaModule runtime — just plain `g++` executables exercising the
pure-DSP classes/headers directly.

## Running

```
cd tests && ./run.sh
```

`run.sh` builds and runs every `tests/{mf20,loooop,particules,onbetap,vespid}/test_*.cpp`
against `../src`, printing PASS/FAIL per assertion and exiting non-zero if
any binary fails or returns non-zero.

If a test needs a non-header-only `.cpp` from `src/` linked in (e.g. Loooop's
`LoopEngine.cpp`), drop a sibling `<test>.cpp.extra` file next to it listing
one extra source path per line, relative to `tests/`. `run.sh` picks it up
automatically — see `tests/loooop/test_loop_engine.cpp.extra` and
`tests/loooop/test_display_renderer.cpp.extra` for examples.

## Test lanes

This repo has three independent test lanes:

- **Lane 1 — `tests/run.sh`** — zero-dependency `g++` DSP tests (MF-20, Loooop,
  Particules, Onbetap, Vespid), plain-assert style, followed by a
  `python3 -m unittest discover` pass over `tests/test_*.py` (identity/build
  guard tests — plugin metadata, slug parity between `plugin.json` and
  `metamodule/plugin-mm.json`, no-delay-mode symbol removal, and head-color
  parity between `src/loooop/HeadColors.hpp` and `panel-specs/loooop.yaml`).
  Runs anywhere, no build system needed beyond a `python3` on `PATH`.
- **Lane 2 — `tests/particules_dsp/run.sh`** — the Particules granular-DSP
  Catch2 suite (CMake + CTest). Covers the granular / delay / reverb / quality
  / pitch DSP that powers Particules. Catch2 is vendored (amalgamated, v3.5.2),
  so the lane builds and runs fully offline. Run it with:

  ```
  ./tests/particules_dsp/run.sh
  ```
- **Lane 3 — `tests/retours_delay_dsp/run.sh`** — the Retours delay-DSP
  Catch2 suite, same CMake + CTest + vendored-Catch2 setup as Lane 2. Covers
  the echo engine, base-time/clocking, slicer, pitch shifter, envelope, and
  quality modes that power Retours. Run it with:

  ```
  ./tests/retours_delay_dsp/run.sh
  ```

## What isn't covered here

- Anything needing a live VCV/MetaModule host process or the headless
  simulator (wiring tests, click tests, patch-fixture playback) — out of
  scope for an offline `g++` harness.
- Any test instantiating the `Particules` Rack `Module` directly: its
  constructor calls `APP->engine->getSampleRate()`, which segfaults without
  a live Rack `Context`. Only its pure-DSP pieces are tested here.
