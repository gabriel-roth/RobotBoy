# beads_dsp Catch2 Test Lane — Design

**Date:** 2026-07-06
**Status:** Approved (design)
**Repo:** `~/Dev/Foobar` (worktree `beads-test-lane`)

## Goal

Add a second, self-contained test lane that runs the upstream beads_dsp Catch2
suite (16 test files, ~3,900 lines) against Foobar's **own** vendored
`src/vendor/beads_dsp`, giving the Particules DSP real regression coverage.

The Particules module is currently the least-tested of the four: Task 6 of the
combine project only ported the 49-line `test_pitch_notch_map`; everything else
needed Catch2 or a live Rack engine. This lane closes that gap.

## Constraints / non-goals

- **Test-layer only.** No changes to plugin source (`src/`), the VCV `Makefile`,
  or the MetaModule `CMakeLists.txt`. The `.dylib`/`.mmplugin` builds are
  untouched.
- **Lane 1 stays as-is.** The existing `tests/run.sh` (zero-dependency `g++`)
  is not modified.
- **Offline & reproducible.** No network at configure or build time. Catch2 is
  vendored, not fetched.
- **Test files ported verbatim.** The 16 upstream test `.cpp` files are copied
  unmodified so they can be re-synced from upstream later without reconciling
  local edits.
- **Test the vendored copy.** The lane compiles against Foobar's
  `src/vendor/beads_dsp`, not the original particules repo — otherwise the lane
  would test code Foobar doesn't ship.

## Key facts (verified)

- Upstream suite: `~/Dev/particules/nosuch_texture/tests/`. Targets **Catch2
  v3.5.2**, v3 modular include style (`<catch2/catch_test_macros.hpp>`,
  `<catch2/catch_approx.hpp>` — the only two Catch2 headers used across all
  tests), links `Catch2::Catch2WithMain`, uses `catch_discover_tests`.
- Upstream builds `beads_dsp` as a CMake `STATIC` lib
  (`beads_dsp/CMakeLists.txt`). Foobar did **not** vendor that `CMakeLists`, so
  the lane builds beads itself from `src/vendor/beads_dsp/src` (the same sources
  the plugin compiles: `src/*.cpp` + `src/*/*.cpp`).
- The two Particules-specific tests include
  `"particules_block_runtime.h"`/`"particules_density_control.h"` **via include
  dir**, not relative path — so an `-I src/particules` resolves them with no
  test-file edits. `control_conditioner.h` (needed by
  `test_control_conditioning.cpp`) is present at
  `src/vendor/beads_dsp/src/util/`.
- Catch2 v3.5.2 amalgamated files are already cached offline at
  `~/Dev/particules/nosuch_texture/build/_deps/catch2-src/extras/catch_amalgamated.{hpp,cpp}`.

## Architecture

Everything new lives under `tests/beads/`:

```
tests/beads/
├── CMakeLists.txt          # beads static lib + test exe + CTest wiring
├── run.sh                  # cmake configure + build + ctest (one command)
├── README.md               # optional; main docs go in tests/README.md
├── catch2/
│   ├── catch_amalgamated.hpp   # vendored Catch2 v3.5.2 (from cache extras/)
│   ├── catch_amalgamated.cpp   # Catch2 impl + default main()
│   └── catch2/
│       ├── catch_test_macros.hpp   # shim: #include "../catch_amalgamated.hpp"
│       └── catch_approx.hpp        # shim: #include "../catch_amalgamated.hpp"
└── test_*.cpp              # 16 files copied verbatim from upstream
```

### Catch2 via vendored amalgamated + shim headers

The amalgamated distribution is a single header, but the tests include the
modular v3 paths. Two shim headers under `catch2/catch2/` redirect the two
modular includes to the amalgamated header:

```cpp
// tests/beads/catch2/catch2/catch_test_macros.hpp
#pragma once
#include "../catch_amalgamated.hpp"
```
```cpp
// tests/beads/catch2/catch2/catch_approx.hpp
#pragma once
#include "../catch_amalgamated.hpp"
```

With `-I tests/beads/catch2` on the include path, `<catch2/catch_test_macros.hpp>`
resolves to the shim, which pulls in the amalgamated header (found relative to
the shim). This keeps the 16 test files byte-identical to upstream. The
amalgamated `.cpp` provides `main()`, replacing `Catch2::Catch2WithMain`.

### The CMake target (`tests/beads/CMakeLists.txt`)

- `add_library(foobar_beads_dsp STATIC …)` — globs
  `src/vendor/beads_dsp/src/*.cpp` + `src/vendor/beads_dsp/src/*/*.cpp`
  (relative to the repo root, resolved from `CMAKE_CURRENT_LIST_DIR/../../..`),
  include dirs `src/vendor/beads_dsp/{include,src}`, `-std=c++17` (matches the
  plugin's beads build).
- `add_executable(beads_tests <16 test .cpp> catch2/catch_amalgamated.cpp)`.
- `target_include_directories(beads_tests PRIVATE tests/beads/catch2,
  src/vendor/beads_dsp/{include,src}, src/particules)`.
- `target_link_libraries(beads_tests PRIVATE foobar_beads_dsp)`.
- CTest registration via a single `add_test(NAME beads_tests COMMAND beads_tests)`.
  (Upstream uses `catch_discover_tests`, but that needs the Catch2 CMake package,
  which the amalgamated distribution omits; the binary reports per-assertion
  results and returns non-zero on any failure, so a single CTest entry is the
  equivalent minimal integration.)
- `test_main.cpp` (an empty no-op upstream) is dropped.

### Running

`tests/beads/run.sh`:
```sh
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
cmake -B build -G "Unix Makefiles"
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`tests/README.md` gains a "Test lanes" section:
- **Lane 1 — `tests/run.sh`**: zero-dependency `g++`, plain-assert DSP tests
  (MF-20, Loooop, the small Particules pitch-map). Runs anywhere.
- **Lane 2 — `tests/beads/run.sh`**: CMake + CTest, the vendored Catch2 beads
  suite. Offline (Catch2 vendored). Covers the beads granular/delay/reverb/
  quality/pitch DSP that powers Particules.

## Test inventory (16 files, ported verbatim)

`test_buffer`, `test_grain`, `test_reverb`, `test_processor`,
`test_click_prevention`, `test_delay`, `test_quality_modes`, `test_auto_gain`,
`test_attenurandomizer`, `test_silence`, `test_pitch_quantizer`,
`test_pitch_lock`, `test_density_rate`, `test_particules_density_control`,
`test_control_conditioning`, `test_particules_block_runtime`.

## Success criteria

1. `tests/beads/run.sh` configures, builds, and runs with **no network access**.
2. `ctest` reports all discovered tests passing (exit 0).
3. Lane 1 (`tests/run.sh`) is unchanged and still green.
4. No files outside `tests/` are modified.

## Risks

- **Vendored beads drift.** If Foobar's `src/vendor/beads_dsp` diverged from the
  upstream the tests were written against, a test may fail. That is the lane
  doing its job — investigate the specific failure; do not edit tests to force
  green. The plugin builds are unaffected regardless.
- **Amalgamated/modular mismatch.** Mitigated by the shim headers; if a test
  turns out to include a third `<catch2/...>` header, add a matching shim
  (only two are used across the current suite).
