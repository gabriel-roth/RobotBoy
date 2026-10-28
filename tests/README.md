# Robot Boy regression tests

Offline, host-free C++ tests for the DSP/rendering code in `src/`. No Rack
engine, no MetaModule runtime — just plain `g++` executables exercising the
pure-DSP classes/headers directly.

## Running

```
cd tests && ./run.sh
```

`run.sh` builds and runs every `tests/{mf20,loooop,onbetap,vespid,lucarne,shapes}/test_*.cpp`
against `../src`, then the Python guard tests and the Beads engine suite
(lane 2 below), and exits non-zero if anything fails.

`tests/shapes/reference.hpp` holds ground-truth vectors generated from the original JS prototype of the Skylark and Weaver engines (not in this repo) by `tests/shapes/make_reference.js`, which is kept for provenance.

If a test needs a non-header-only `.cpp` from `src/` linked in (e.g. Loooop's
`LoopEngine.cpp`), drop a sibling `<test>.cpp.extra` file next to it listing
one extra source path per line, relative to `tests/`. `run.sh` picks it up
automatically — see `tests/loooop/test_loop_engine.cpp.extra` and
`tests/loooop/test_display_renderer.cpp.extra` for examples.

## Test lanes

This repo has two test lanes; `tests/run.sh` runs both:

- **Lane 1 — `tests/run.sh`** — zero-dependency `g++` DSP tests (MF-20, Loooop,
  Onbetap, Vespid, Lucarne), plain-assert style, followed by a
  `python3 -m unittest discover` pass over `tests/test_*.py` (identity/build
  guard tests — plugin metadata, slug parity between `plugin.json` and
  `metamodule/plugin-mm.json`, and head-color
  parity between `src/loooop/HeadColors.hpp` and `panel-specs/loooop.yaml`).
  Runs anywhere, no build system needed beyond a `python3` on `PATH`.
- **Lane 2 — `tests/beads_engine/run.sh`** — the Catch2 suite for the
  Beads engine behind Particules, Retours and Ondes (CMake + CTest, vendored
  Catch2 v3.5.2, fully offline): the vendored, patched `beads::Processor`,
  the resampler, the engine host (native-rate streaming, triggers, freeze,
  reconfiguration, NaN robustness, delay timing), the Beads knob laws, Lock
  pitch and gain control, and Ondes' wavetable voice. Run it alone with:

  ```
  ./tests/beads_engine/run.sh
  ```

## What isn't covered here

- Anything needing a live VCV/MetaModule host process or the headless
  simulator (wiring tests, click tests, patch-fixture playback) — out of
  scope for an offline `g++` harness.
- Any test instantiating the `Particules` Rack `Module` directly: its
  constructor calls `APP->engine->getSampleRate()`, which segfaults without
  a live Rack `Context`. Only its pure-DSP pieces are tested here.
