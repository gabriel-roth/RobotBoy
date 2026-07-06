# beads_dsp Catch2 Test Lane — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a self-contained CMake/CTest "lane 2" that runs the upstream beads_dsp Catch2 suite against Foobar's vendored `src/vendor/beads_dsp`.

**Architecture:** A new `tests/beads/` dir holds 16 upstream test files copied verbatim, a vendored Catch2 v3.5.2 amalgamated header/impl with two shim headers (so the tests' modular `<catch2/...>` includes resolve to the amalgamated header unmodified), and a `CMakeLists.txt` that builds beads as a static lib + the test exe and registers it with CTest. Offline; no network.

**Tech Stack:** CMake (≥3.14), CTest, Catch2 v3.5.2 (vendored amalgamated), C++17, the vendored `src/vendor/beads_dsp`.

## Global Constraints

- **Test-layer only.** Modify nothing outside `tests/`. Do NOT touch `src/`, `vcv/Makefile`, or `metamodule/CMakeLists.txt`.
- **Lane 1 unchanged.** Do NOT modify `tests/run.sh` or any existing lane-1 test.
- **Offline.** No network at configure or build time — Catch2 is vendored, not fetched. Copy the amalgamated files from the local cache `~/Dev/particules/nosuch_texture/build/_deps/catch2-src/extras/`.
- **Test files verbatim.** Copy the 16 upstream `.cpp` files unmodified (no edits to includes or bodies). If something won't resolve, fix it via CMake include dirs or shim headers — never by editing a test file.
- **Test the vendored copy.** Build against `src/vendor/beads_dsp`, NOT `~/Dev/particules`.
- **Catch2 version:** v3.5.2.
- **Working directory:** the worktree `~/Dev/Foobar/.worktrees/beads-test-lane`. All paths below are relative to that repo root.

---

## Source reference

- Upstream tests + CMake: `~/Dev/particules/nosuch_texture/tests/`
- Catch2 v3.5.2 amalgamated (local, offline): `~/Dev/particules/nosuch_texture/build/_deps/catch2-src/extras/catch_amalgamated.{hpp,cpp}`
- Vendored beads DSP: `src/vendor/beads_dsp/{include,src}` (same sources the plugin compiles: `src/*.cpp` + `src/*/*.cpp`)
- Particules glue headers needed by two tests: `src/particules/particules_block_runtime.h`, `src/particules/particules_density_control.h`

The 16 test files (from upstream `add_executable`, excluding the no-op `test_main.cpp`):
`test_buffer.cpp test_grain.cpp test_reverb.cpp test_processor.cpp test_click_prevention.cpp test_delay.cpp test_quality_modes.cpp test_auto_gain.cpp test_attenurandomizer.cpp test_silence.cpp test_pitch_quantizer.cpp test_pitch_lock.cpp test_density_rate.cpp test_particules_density_control.cpp test_control_conditioning.cpp test_particules_block_runtime.cpp`

---

## Task 1: Stand up the beads lane and get it green

**Files:**
- Create: `tests/beads/catch2/catch_amalgamated.hpp` (copied), `tests/beads/catch2/catch_amalgamated.cpp` (copied)
- Create: `tests/beads/catch2/catch2/catch_test_macros.hpp` (shim), `tests/beads/catch2/catch2/catch_approx.hpp` (shim)
- Create: `tests/beads/test_*.cpp` × 16 (copied verbatim)
- Create: `tests/beads/CMakeLists.txt`
- Create: `tests/beads/run.sh`

**Interfaces:**
- Consumes: `src/vendor/beads_dsp/{include,src}`, `src/particules/*.h`.
- Produces: `tests/beads/run.sh` → configures, builds, and runs the suite via CTest; exits non-zero on any test failure.

- [ ] **Step 1: Vendor Catch2 amalgamated + create shim headers**

```bash
cd ~/Dev/Foobar/.worktrees/beads-test-lane
mkdir -p tests/beads/catch2/catch2
CACHE=~/Dev/particules/nosuch_texture/build/_deps/catch2-src/extras
cp "$CACHE/catch_amalgamated.hpp" tests/beads/catch2/catch_amalgamated.hpp
cp "$CACHE/catch_amalgamated.cpp" tests/beads/catch2/catch_amalgamated.cpp
printf '#pragma once\n#include "../catch_amalgamated.hpp"\n' > tests/beads/catch2/catch2/catch_test_macros.hpp
printf '#pragma once\n#include "../catch_amalgamated.hpp"\n' > tests/beads/catch2/catch2/catch_approx.hpp
```

Verify the amalgamated header is v3.5.2:
```bash
grep -m1 "Catch2 v3" tests/beads/catch2/catch_amalgamated.hpp
```
Expected: a line naming `Catch2 v3.5.2`.

- [ ] **Step 2: Copy the 16 test files verbatim**

```bash
cd ~/Dev/Foobar/.worktrees/beads-test-lane
SRC=~/Dev/particules/nosuch_texture/tests
for f in test_buffer test_grain test_reverb test_processor test_click_prevention \
         test_delay test_quality_modes test_auto_gain test_attenurandomizer test_silence \
         test_pitch_quantizer test_pitch_lock test_density_rate test_particules_density_control \
         test_control_conditioning test_particules_block_runtime; do
  cp "$SRC/$f.cpp" "tests/beads/$f.cpp"
done
ls tests/beads/test_*.cpp | wc -l   # expect 16
```
Do NOT edit these files. Do NOT copy `test_main.cpp`.

- [ ] **Step 3: Write `tests/beads/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.14)
project(foobar_beads_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(BEADS ${REPO_ROOT}/src/vendor/beads_dsp)

# beads DSP as a static lib — the same sources the plugin compiles.
file(GLOB_RECURSE BEADS_SRC CONFIGURE_DEPENDS ${BEADS}/src/*.cpp)
add_library(foobar_beads_dsp STATIC ${BEADS_SRC})
target_include_directories(foobar_beads_dsp PUBLIC ${BEADS}/include ${BEADS}/src)

# Catch2 (vendored amalgamated single-header + impl).
add_library(catch2_amalgamated STATIC ${CMAKE_CURRENT_LIST_DIR}/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated PUBLIC ${CMAKE_CURRENT_LIST_DIR}/catch2)

# Test executable — 16 verbatim upstream test files.
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)
add_executable(beads_tests ${TEST_SRC})
target_include_directories(beads_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/catch2      # shim <catch2/...> -> amalgamated
    ${BEADS}/include
    ${BEADS}/src
    ${REPO_ROOT}/src/particules)          # particules_block_runtime.h etc.
target_link_libraries(beads_tests PRIVATE foobar_beads_dsp catch2_amalgamated)

# CTest: a single entry that runs the whole Catch2 binary. (catch_discover_tests
# needs the Catch2 CMake package, which the amalgamated distribution omits; the
# binary itself reports per-assertion results and returns non-zero on failure.)
enable_testing()
add_test(NAME beads_tests COMMAND beads_tests)
```

- [ ] **Step 4: Write `tests/beads/run.sh` and make it executable**

```bash
cat > tests/beads/run.sh <<'EOF'
#!/usr/bin/env bash
# Lane 2: the beads_dsp Catch2 suite (CMake + CTest, offline, vendored Catch2).
set -euo pipefail
cd "$(dirname "$0")"
cmake -B build -G "Unix Makefiles"
cmake --build build -j
ctest --test-dir build --output-on-failure
EOF
chmod +x tests/beads/run.sh
```

- [ ] **Step 5: Configure + build**

Run:
```bash
cd ~/Dev/Foobar/.worktrees/beads-test-lane/tests/beads
cmake -B build -G "Unix Makefiles" 2>&1 | tail -5
cmake --build build -j 2>&1 | tail -25
```
Expected: `beads_tests` links with no errors, no network access. If a test fails to compile for a missing include, fix it by adding the right directory to `target_include_directories` (Step 3) — NOT by editing the test file. If a beads source needs C++20, bump `CMAKE_CXX_STANDARD` to 20 and note it. If a beads `.cpp` under `src/` turns out not to belong in the lib (e.g. it has its own `main`), switch the glob to an explicit source list matching `vcv/Makefile`'s beads list and note it.

- [ ] **Step 6: Run the suite via CTest**

Run:
```bash
cd ~/Dev/Foobar/.worktrees/beads-test-lane/tests/beads
ctest --test-dir build --output-on-failure 2>&1 | tail -20
echo "EXIT: ${PIPESTATUS[0]}"
```
Expected: the `beads_tests` CTest entry passes; the Catch2 summary reports all assertions/test cases passing; EXIT 0.

If a **test assertion** fails (not a compile error): this may be real — Foobar's vendored beads may have diverged from the upstream the test expects. STOP and report the specific failing test case and assertion. Do NOT edit the test to make it pass. The controller decides whether it's a real regression or a vendoring difference to investigate.

- [ ] **Step 7: Confirm the build dir is ignored, then commit**

The lane's `build/` is CMake output. Confirm it won't be committed:
```bash
cd ~/Dev/Foobar/.worktrees/beads-test-lane
git check-ignore -q tests/beads/build || printf '\n# beads test-lane CMake output\ntests/beads/build/\n' >> .gitignore
git add .gitignore tests/beads/CMakeLists.txt tests/beads/run.sh \
        tests/beads/test_*.cpp tests/beads/catch2/
git status --short   # verify tests/beads/build/ is NOT staged
git commit -m "Add beads_dsp Catch2 test lane (lane 2)"
```

---

## Task 2: Document both test lanes

**Files:**
- Modify: `tests/README.md`

**Interfaces:**
- Consumes: the lane created in Task 1.
- Produces: user-facing docs describing how to run each lane.

- [ ] **Step 1: Add a "Test lanes" section to `tests/README.md`**

Read the current `tests/README.md` first to match its heading style, then insert this section near the top (after the intro, before "What's ported"):

```markdown
## Test lanes

This repo has two independent test lanes:

- **Lane 1 — `tests/run.sh`** — zero-dependency `g++` DSP tests (MF-20, Loooop,
  and the small Particules pitch-map), plain-assert style. Runs anywhere, no
  build system needed.
- **Lane 2 — `tests/beads/run.sh`** — the vendored beads_dsp Catch2 suite
  (CMake + CTest). Covers the granular / delay / reverb / quality / pitch DSP
  that powers Particules. Catch2 is vendored (amalgamated, v3.5.2), so the lane
  builds and runs fully offline. Run it with:

  ```
  ./tests/beads/run.sh
  ```
```

- [ ] **Step 2: Commit**

```bash
cd ~/Dev/Foobar/.worktrees/beads-test-lane
git add tests/README.md
git commit -m "Document both test lanes in tests/README.md"
```

---

## Self-review notes

- **Spec coverage:** layout + shims (Task 1 Steps 1–4); beads static lib + test exe + CTest (Step 3); offline vendored Catch2 (Step 1, from local cache); 16 verbatim tests (Step 2); run.sh (Step 4); green criterion (Step 6); docs/both-lanes (Task 2). Lane-1-unchanged and test-layer-only are Global Constraints enforced by the commit scoping in Task 1 Step 7 / Task 2 Step 2.
- **Deviation from spec (intentional):** spec mentioned `catch_discover_tests`; the plan uses a single `add_test` because the amalgamated Catch2 has no CMake package. Same observable outcome (ctest pass/fail on any failure). The spec's CMake bullet is reconciled to match.
- **Failure handling:** compile errors → fix via CMake include dirs, never test edits (Step 5); assertion failures → STOP and report as a possible real regression (Step 6), per the spec's risk section.
