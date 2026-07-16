# beads-delay (Échos) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Échos, a VCV Rack + MetaModule module emulating Mutable
Beads' hidden delay mode, per
`docs/superpowers/specs/2026-07-15-beads-delay-design.md`.

**Architecture:** Standalone DSP core `beadsdelay_dsp` (mirrors
`particules_dsp`: caller-allocated memory, placement Impl, 64-frame block
chunks, no audio-thread allocation) reusing `particules_dsp` components
(RecordingBuffer, QualityProcessor, Saturation, Random, Svf, interpolation).
Thin VCV adapter `src/beadsdelay/Echos.cpp` serves both VCV and MetaModule
(Particules pattern: 64-sample wrapper block runtime, `#ifdef METAMODULE`
branches). TDD via a new Catch2 lane `tests/beadsdelay_dsp/`.

**Tech Stack:** C++17 core (C++20 in MM build), Catch2 (vendored in
`tests/particules_dsp/catch2/`), Rack SDK, MetaModule plugin SDK, CMake.

## Global Constraints

- Prime directive: all work on the `worktree-beads-delay` worktree at
  `/Users/gabrielroth/Dev/RobotBoy/.worktrees/worktree-beads-delay`. Never
  touch the main checkout or other worktrees.
- No heap allocation, exceptions, or `std::pow/sin/exp/tanh` calls in the
  per-sample audio path. Transcendentals at block rate only (≤ once per
  64 frames).
- All big buffers live in the caller-provided memory block. Work buffers in
  Impl, not the audio stack.
- Reuse `particules_dsp` components by including their headers; do NOT copy
  their code, and do NOT modify files under `src/particules/` except where a
  task explicitly says so.
- `tests/test_no_delay_mode.py` (after Task 1 rescopes it) forbids delay
  symbols only inside `src/particules/dsp/` files — keep it passing.
- Commit messages: short, one sentence, ≤ 15 words, no AI attribution.
- Commit at the end of every task (and mid-task where steps say so).
- New core namespace: `beadsdelay_dsp`. Module slug `Echos`, display name
  `Échos`.
- Panel: straightforward layout, don't gold-plate; DSP is the priority.
- MetaModule CPU is the scarce resource — follow
  `docs/superpowers/specs/2026-07-15-beads-delay-mm-optimization-notes.md`.

## Key numeric decisions (single source of truth)

Constants live in `src/beadsdelay/dsp/include/beadsdelay_dsp/types.h`:

- `kBufferFrames = 192000` (4 s stereo at 48 kHz, HiFi). Decimation factors
  reuse `particules_dsp::DecimationFactorForQuality` (1/2/8/4 → 4/8/32/16 s).
- Manual base-time span: `kManualOctaves = 11.0f` (full buffer at noon down
  to ~2 ms at either extreme), exponential.
- DENSITY CV: −1 V/oct on time (positive volts shorten; `×2^−V`).
- Clock subdivision tables (of base interval), DENSITY knob at noon = 1/1:
  - CCW (knob 0.5→0.0): `{1/2, 1/4, 1/8, 1/16}` (4 zones)
  - CW (knob 0.5→1.0): `{1/2, 1/3, 1/4, 1/6, 1/8, 1/10, 1/12, 1/16}` (8 zones)
  - Zone hysteresis: 0.02 knob units.
- TIME multiplier: knob 0→1 maps 1×→16× (exponential, `16^knob`); snapped
  to `{1,2,3,4,6,8,12,16}` (nearest) in clocked mode; final delay clamped to
  buffer length. Frozen: TIME selects slice index `floor(knob * (nslices-1) + 0.5)`.
- Multi-tap (DENSITY CW side, manual mode only): tap 2 at
  `0.61803f × delay` (golden ratio), gain `0.7f`.
- Doppler slew: one-pole on delay-in-frames, coefficient from
  `kSlewSecondsDefault = 0.08f` (context slider 0.01–1.0 s).
- Crossfade jump mode: 1024-frame equal-power crossfade between old/new
  read offsets; retarget mid-fade queues (DLD pattern).
- Pitch shifter: ring of `kShifterSize = 4096` frames (stereo float),
  triangular windows, two heads 180° apart, bypass when
  `|semitones| < 0.25f`, ratio `exp2(semitones/12)` at block rate.
- SHAPE envelope: period = base time, synced to clock ticks. shape=0 flat;
  (0,⅓] flat→rect gate (duty 0.9→0.5, 5 ms edges); (⅓,⅔] rect→Hann;
  (⅔,1] Hann→slow-attack ramp.
- Slow random LFOs: default `kRandomLfoHz = 0.15f` (slider 0.02–2 Hz),
  cosine-interpolated two-point random, one instance per AR target with
  seeds 1,2,3.
- Feedback: knob 0→1 maps 0→1.1 (>1 for runaway), per-quality
  `Saturation::LimitFeedback`, Svf HP DC blocker at 10 Hz in the loop.
- AR semantics (delay flavor, continuous): CV patched & ar>0:
  `mod = ar*cv_norm`; CV patched & ar<0: `mod = lfo*(-ar)*|cv_norm|`;
  unpatched & ar>0: `mod = lfo*ar` (uniform); unpatched & ar<0:
  `mod = lfo³*(-ar)` (peaky). `cv_norm = volts/5` for TIME/SHAPE;
  PITCH: `mod_semitones = ar*volts*12` (1 V/oct at full CW).

## File Structure

```
src/beadsdelay/
  dsp/include/beadsdelay_dsp/
    types.h            # constants, enums, EchosParameters
    echos_dsp.h        # public EchosProcessor API
  dsp/src/
    echos_processor.h  # Impl (internal)
    echos_processor.cpp
    time/base_time.h/.cpp      # BaseTimeControl (manual/clock/mult/slices)
    engine/echo_engine.h/.cpp  # taps, feedback loop, slew/crossfade, freeze
    pitch/rotary_shifter.h/.cpp
    mod/slow_random_lfo.h      # header-only
    mod/ar_modulator.h         # header-only (delay-flavor attenurandomizer)
    env/repeat_envelope.h      # header-only
  Echos.cpp            # VCV adapter (both platforms)
  echos_block_runtime.h
tests/beadsdelay_dsp/  # Catch2 lane (CMake, run.sh, test_*.cpp)
panel-specs/echos.yaml → res/Echos.svg → metamodule/assets/Echos.png
```

---

### Task 1: Scaffold core library, test lane, guard-test rescope

**Files:**
- Create: `src/beadsdelay/dsp/include/beadsdelay_dsp/types.h`
- Create: `src/beadsdelay/dsp/include/beadsdelay_dsp/echos_dsp.h`
- Create: `src/beadsdelay/dsp/src/echos_processor.h`
- Create: `src/beadsdelay/dsp/src/echos_processor.cpp`
- Create: `tests/beadsdelay_dsp/CMakeLists.txt`
- Create: `tests/beadsdelay_dsp/run.sh` (chmod +x)
- Create: `tests/beadsdelay_dsp/test_processor_basics.cpp`
- Modify: `tests/test_no_delay_mode.py` (rescope)

**Interfaces (Produces — later tasks rely on exactly these):**

`types.h`:
```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "particules_dsp/types.h"   // StereoFrame, QualityMode, DecimationFactorForQuality

namespace beadsdelay_dsp {

using particules_dsp::StereoFrame;
using particules_dsp::QualityMode;

static constexpr size_t kBufferFrames = 192000;   // 4 s stereo @48k HiFi
static constexpr size_t kMaxBlockSize = 64;
static constexpr float  kManualOctaves = 11.0f;
static constexpr size_t kShifterSize = 4096;      // frames, power of two
static constexpr float  kMinDelaySeconds = 0.002f;
static constexpr float  kSlewSecondsDefault = 0.08f;
static constexpr float  kRandomLfoHz = 0.15f;
static constexpr int    kJumpCrossfadeFrames = 1024;
static constexpr float  kTap2Ratio = 0.61803f;
static constexpr float  kTap2Gain = 0.7f;
static constexpr float  kShifterBypassSemitones = 0.25f;

enum class TimeChangeMode : uint8_t { kTape = 0, kCrossfade = 1 };

struct EchosParameters {
    // Knobs (normalized)
    float density = 0.5f;          // 0..1, noon = 0.5
    float time = 0.0f;             // 0..1 → multiplier / slice select
    float pitch_semitones = 0.0f;  // -24..+24 (adapter applies notch map)
    float shape = 0.0f;            // 0..1, 0 = no envelope
    float feedback = 0.0f;         // 0..1
    float dry_wet = 0.5f;          // 0..1

    // CV (volts) + patched flags
    float density_cv = 0.0f;       // exponential, −1 V/oct on time
    float time_cv = 0.0f, pitch_cv = 0.0f, shape_cv = 0.0f;
    float feedback_cv = 0.0f, dry_wet_cv = 0.0f;   // added directly, /5 V
    bool  time_cv_connected = false, pitch_cv_connected = false,
          shape_cv_connected = false;

    // Attenurandomizers (-1..+1, 0 = noon)
    float time_ar = 0.0f, pitch_ar = 0.0f, shape_ar = 0.0f;

    // Clock / tap: sample offset of a rising edge within this block, -1 none
    int   clock_tick_offset = -1;
    bool  clock_connected = false;  // SEED jack patched
    bool  freeze = false;

    QualityMode quality = QualityMode::kHiFi;
    TimeChangeMode time_change_mode = TimeChangeMode::kTape;
    bool  envelope_pre_feedback = false;  // false = feedback taps post-envelope
    float input_trim_db = 0.0f;           // -12..+12
    float slew_seconds = kSlewSecondsDefault;  // 0.01..1.0
    float random_lfo_hz = kRandomLfoHz;        // 0.02..2
};

} // namespace beadsdelay_dsp
```

`echos_dsp.h`:
```cpp
#pragma once
#include "types.h"

namespace beadsdelay_dsp {

class EchosProcessor {
public:
    struct MemoryRequirements { size_t total_bytes; size_t alignment; };
    static MemoryRequirements GetMemoryRequirements(float sample_rate);

    void Init(void* memory, size_t memory_size, float sample_rate);
    void SetParameters(const EchosParameters& params);
    void Process(const StereoFrame* input, StereoFrame* output, size_t num_frames);
    void ClearBuffer();

    // Telemetry (block-rate; for panel lights)
    float BaseTimeSeconds() const;    // current base delay time
    bool  IsClocked() const;
    float DelayTimeSeconds() const;   // actual tap-1 delay after multiplier

private:
    struct Impl;
    Impl* impl_ = nullptr;
    void ProcessBlock(const StereoFrame* input, StereoFrame* output, size_t n);
};

} // namespace beadsdelay_dsp
```

- [ ] **Step 1: Write the failing smoke test**

`tests/beadsdelay_dsp/test_processor_basics.cpp`:
```cpp
#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <vector>
#include "beadsdelay_dsp/echos_dsp.h"

using namespace beadsdelay_dsp;

namespace {
struct Proc {
    void* mem = nullptr;
    EchosProcessor p;
    explicit Proc(float sr = 48000.f) {
        auto req = EchosProcessor::GetMemoryRequirements(sr);
        REQUIRE(req.total_bytes > 0);
        posix_memalign(&mem, req.alignment, req.total_bytes);
        p.Init(mem, req.total_bytes, sr);
    }
    ~Proc() { std::free(mem); }
};
} // namespace

TEST_CASE("silence in, silence out") {
    Proc proc;
    EchosParameters params;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(4800, StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out(4800, StereoFrame{1.f, 1.f});
    proc.p.Process(in.data(), out.data(), in.size());
    for (auto& f : out) {
        REQUIRE(f.l == Catch::Approx(0.f).margin(1e-6));
        REQUIRE(f.r == Catch::Approx(0.f).margin(1e-6));
    }
}

TEST_CASE("null memory Init is safe") {
    EchosProcessor p;
    p.Init(nullptr, 0, 48000.f);
    EchosParameters params;
    p.SetParameters(params);
    StereoFrame in{1.f, 1.f}, out{};
    p.Process(&in, &out, 1);   // must not crash
    REQUIRE(out.l == 0.f);
}

TEST_CASE("dry passthrough at dry_wet 0") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 0.f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(256), out(256);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = {std::sin(0.1f * i), std::cos(0.1f * i)};
    proc.p.Process(in.data(), out.data(), in.size());
    // allow smoothing settle over first 128 frames
    for (size_t i = 128; i < out.size(); ++i)
        REQUIRE(out[i].l == Catch::Approx(in[i].l).margin(0.02));
}
```

`tests/beadsdelay_dsp/CMakeLists.txt` (mirrors particules lane; vendored
Catch2 referenced from the sibling directory):
```cmake
cmake_minimum_required(VERSION 3.14)
project(robotboy_beadsdelay_dsp_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(BEADSDELAY_DSP ${REPO_ROOT}/src/beadsdelay/dsp)
set(PARTICULES_DSP ${REPO_ROOT}/src/particules/dsp)
set(PARTICULES_TESTS ${REPO_ROOT}/tests/particules_dsp)

file(GLOB_RECURSE BEADSDELAY_DSP_SRC CONFIGURE_DEPENDS ${BEADSDELAY_DSP}/src/*.cpp)
file(GLOB_RECURSE PARTICULES_DSP_SRC CONFIGURE_DEPENDS ${PARTICULES_DSP}/src/*.cpp)
add_library(robotboy_beadsdelay_dsp STATIC ${BEADSDELAY_DSP_SRC} ${PARTICULES_DSP_SRC})
target_include_directories(robotboy_beadsdelay_dsp PUBLIC
    ${BEADSDELAY_DSP}/include ${BEADSDELAY_DSP}/src
    ${PARTICULES_DSP}/include ${PARTICULES_DSP}/src)

add_library(catch2_amalgamated STATIC ${PARTICULES_TESTS}/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated PUBLIC ${PARTICULES_TESTS}/catch2)

file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)
add_executable(beadsdelay_dsp_tests ${TEST_SRC})
target_include_directories(beadsdelay_dsp_tests PRIVATE
    ${PARTICULES_TESTS}/catch2
    ${BEADSDELAY_DSP}/include ${BEADSDELAY_DSP}/src
    ${PARTICULES_DSP}/include ${PARTICULES_DSP}/src)
target_link_libraries(beadsdelay_dsp_tests PRIVATE robotboy_beadsdelay_dsp catch2_amalgamated)

enable_testing()
add_test(NAME beadsdelay_dsp_tests COMMAND beadsdelay_dsp_tests)
```

`tests/beadsdelay_dsp/run.sh` (copy particules run.sh, change comment):
```bash
#!/usr/bin/env bash
# Lane: the Échos delay-DSP Catch2 suite (CMake + CTest, offline, vendored Catch2).
set -euo pipefail
cd "$(dirname "$0")"
cmake -B build -G "Unix Makefiles"
cmake --build build -j
ctest --test-dir build --output-on-failure
```
`chmod +x tests/beadsdelay_dsp/run.sh`. Also add `tests/beadsdelay_dsp/build/`
to `.gitignore` if `build/` isn't already covered (it is — line 1 `build/`
matches any depth; verify with `git check-ignore tests/beadsdelay_dsp/build`).

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests/beadsdelay_dsp && ./run.sh`
Expected: CMake configure error or compile FAIL (`beadsdelay_dsp/echos_dsp.h` not found).

- [ ] **Step 3: Write minimal implementation**

Create `types.h` and `echos_dsp.h` exactly as in **Interfaces** above.

`src/beadsdelay/dsp/src/echos_processor.h`:
```cpp
#pragma once
// Internal header — defines EchosProcessor::Impl. Not public API.
#include "../include/beadsdelay_dsp/echos_dsp.h"
#include "buffer/recording_buffer.h"   // particules_dsp
#include "fx/saturation.h"
#include "quality/quality_processor.h"
#include "util/svf.h"

namespace beadsdelay_dsp {

struct EchosProcessor::Impl {
    particules_dsp::RecordingBuffer recording_buffer;
    particules_dsp::QualityProcessor quality_processor;
    particules_dsp::Saturation saturation;
    particules_dsp::StateVariableFilter feedback_hp_l, feedback_hp_r;

    EchosParameters params;
    float sample_rate = 48000.f;

    // Smoothed mix params (zipper prevention)
    float smoothed_dry_wet = 0.5f;
    float smoothed_feedback = 0.f;

    StereoFrame wet_buf[kMaxBlockSize];

    static constexpr size_t kAlignment = 16;
};

} // namespace beadsdelay_dsp
```

`echos_processor.cpp` minimal version: `GetMemoryRequirements` returns
`sizeof(Impl)` (aligned) + `RecordingBuffer::RequiredBytes(48000, 4.0, 2)`
— use the same fixed-frame-budget arithmetic as Particules: buffer bytes =
`(kBufferFrames + particules_dsp::kInterpolationTail) * 2 * sizeof(float)`.
`Init` placement-news Impl at the front of the memory block, inits the
recording buffer in the space after Impl, inits filters (HP 10 Hz) and
quality processor. Null/short memory → `impl_ = nullptr` and all methods
no-op (Process fills output with zeros). `Process` chunks into
`kMaxBlockSize` and for now implements dry/wet only:
```cpp
void EchosProcessor::ProcessBlock(const StereoFrame* in, StereoFrame* out, size_t n) {
    Impl& s = *impl_;
    // one-pole toward target once per block, applied per sample linearly
    for (size_t i = 0; i < n; ++i) {
        // placeholder wet = silence until Task 3
        StereoFrame wet{0.f, 0.f};
        float mix = s.smoothed_dry_wet;
        out[i].l = in[i].l * (1.f - mix) + wet.l * mix;
        out[i].r = in[i].r * (1.f - mix) + wet.r * mix;
    }
    particules_dsp::OnePole(s.smoothed_dry_wet, s.params.dry_wet, 0.05f);
}
```
Telemetry methods return 0 / false for now. `ClearBuffer` calls
`recording_buffer.ImmediateClear()`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd tests/beadsdelay_dsp && ./run.sh`
Expected: all 3 tests PASS.

- [ ] **Step 5: Rescope the guard test**

In `tests/test_no_delay_mode.py`, delete the line
`"metamodule/CMakeLists.txt",` from `paths` and add this comment above the
list: `# Guards the *old* Particules delay engine's removal (2026-07-07).`
`# Scoped to Particules core files only — the standalone beadsdelay module`
`# (2026-07-15) is a separate, intentional implementation.`
Run: `python3 -m unittest tests/test_no_delay_mode.py` → OK.

- [ ] **Step 6: Verify existing lanes still pass**

Run: `cd tests && ./run.sh` — expected PASS (new dir is not picked up by the
old lane; confirm no regressions).

- [ ] **Step 7: Commit**

```bash
git add src/beadsdelay tests/beadsdelay_dsp tests/test_no_delay_mode.py
git commit -m "beads-delay: scaffold Echos DSP core and test lane"
```

---

### Task 2: BaseTimeControl — manual/clocked base time, multiplier, slices

**Files:**
- Create: `src/beadsdelay/dsp/src/time/base_time.h`
- Create: `src/beadsdelay/dsp/src/time/base_time.cpp`
- Test: `tests/beadsdelay_dsp/test_base_time.cpp`

**Interfaces:**
- Consumes: `types.h` constants.
- Produces:
```cpp
#pragma once
#include "../../include/beadsdelay_dsp/types.h"

namespace beadsdelay_dsp {

// Computes base delay time and the tap-1 target delay, in HOST samples.
// Call Update() once per block. Conversion to buffer frames (decimation)
// happens in EchoEngine, not here.
class BaseTimeControl {
public:
    void Init(float sample_rate, float buffer_seconds);

    struct Result {
        float base_samples;      // base delay time
        float delay_samples;     // base × TIME multiplier, clamped to buffer
        float multiplier;        // resolved TIME multiplier
        bool  clocked;
        bool  multi_tap;         // DENSITY on CW side (manual mode)
        int   slice_count;       // buffer_samples / base (≥1), freeze use
        int   slice_index;       // TIME as slice selector, freeze use
    };

    // block_frames: host frames in this block (for clock timeout bookkeeping)
    // clock_tick_offset: rising-edge offset within block, -1 if none
    Result Update(float density_knob, float density_cv_volts,
                  float time_knob, bool clock_connected,
                  int clock_tick_offset, size_t block_frames);

    float BaseSeconds() const;
    bool  IsClocked() const;

private:
    float sample_rate_ = 48000.f;
    float buffer_samples_ = 192000.f;
    // clock state
    float samples_since_tick_ = 0.f;
    float clock_interval_ = 0.f;       // 0 = no interval known
    bool  clocked_ = false;
    int   subdivision_zone_ = -1;      // hysteresis state
    float last_base_samples_ = 0.f;
};

} // namespace beadsdelay_dsp
```

Behavior to implement (all block-rate; no per-sample cost):
- **Manual** (`!clock_connected`): deviation `d = |density_knob - 0.5| * 2`
  clamped 0..1; `base = buffer_samples * exp2(-kManualOctaves * d)`;
  then `base *= exp2(-density_cv_volts)`; clamp to
  `[kMinDelaySeconds * sr, buffer_samples]`. `multi_tap = density_knob > 0.55`
  (small dead zone above noon). `clocked_` decays: if no tick for 5 s and
  clock not connected → false.
- **Clocked** (`clock_connected` true and ≥2 ticks seen): interval = samples
  between last two ticks (accept 32..10 s×sr, ignore outliers >4× or <¼ of
  current interval unless two in a row agree — keep it simple: exponential
  smooth `interval += 0.5*(new - interval)` when within 4×, else hard reset).
  DENSITY selects subdivision zone from the tables in Global Constraints
  (with 0.02 hysteresis); `base = interval * subdivision`; DENSITY CV shifts
  zone index by `int(cv_volts)` zones, clamped (cheap, documented).
- **Multiplier**: manual `m = exp2(log2(16) * time_knob)` (1→16 continuous);
  clocked: snap to nearest of `{1,2,3,4,6,8,12,16}`.
- **Slices**: `slice_count = max(1, (int)(buffer_samples / base))`;
  `slice_index = round(time_knob * (slice_count - 1))`.
- Tap tempo: button taps arrive via `clock_tick_offset` with
  `clock_connected=false`; two taps within 10 s enter clocked mode with that
  interval; moving DENSITY by >0.05 from its position at the last tap exits
  back to manual. Store `density_at_last_tick_`.

- [ ] **Step 1: Write failing tests** (`test_base_time.cpp`)

```cpp
#include <catch2/catch_amalgamated.hpp>
#include "time/base_time.h"
using namespace beadsdelay_dsp;

static BaseTimeControl MakeBtc() {
    BaseTimeControl b;
    b.Init(48000.f, 4.0f);
    return b;
}

TEST_CASE("manual: noon = full buffer") {
    auto b = MakeBtc();
    auto r = b.Update(0.5f, 0.f, 0.f, false, -1, 64);
    REQUIRE(r.base_samples == Catch::Approx(192000.f).epsilon(0.01));
    REQUIRE_FALSE(r.clocked);
    REQUIRE_FALSE(r.multi_tap);
}

TEST_CASE("manual: extremes reach ~2 ms; CCW single tap, CW multi-tap") {
    auto b = MakeBtc();
    auto lo = b.Update(0.0f, 0.f, 0.f, false, -1, 64);
    REQUIRE(lo.base_samples <= 192000.f * std::exp2(-10.9f) * 1.1f);
    REQUIRE(lo.base_samples >= 0.002f * 48000.f * 0.9f);
    REQUIRE_FALSE(lo.multi_tap);
    auto hi = b.Update(1.0f, 0.f, 0.f, false, -1, 64);
    REQUIRE(hi.multi_tap);
}

TEST_CASE("density CV is -1V/oct") {
    auto b = MakeBtc();
    auto r0 = b.Update(0.25f, 0.f, 0.f, false, -1, 64);
    auto r1 = b.Update(0.25f, 1.f, 0.f, false, -1, 64);
    REQUIRE(r1.base_samples == Catch::Approx(r0.base_samples * 0.5f).epsilon(0.01));
}

TEST_CASE("clocked: interval from two ticks, subdivisions, snap multiplier") {
    auto b = MakeBtc();
    b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    // 24000 samples later (375 blocks of 64), second tick:
    for (int i = 0; i < 374; ++i) b.Update(0.5f, 0.f, 0.f, true, -1, 64);
    auto r = b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    REQUIRE(r.clocked);
    REQUIRE(r.base_samples == Catch::Approx(24000.f).epsilon(0.02));
    // fully CCW: 1/16 subdivision
    auto rdiv = b.Update(0.0f, 0.f, 0.f, true, -1, 64);
    REQUIRE(rdiv.base_samples == Catch::Approx(24000.f / 16.f).epsilon(0.02));
    // multiplier snaps: knob 0.5 → 16^0.5 = 4 → snapped 4
    auto rm = b.Update(0.5f, 0.f, 0.5f, true, -1, 64);
    REQUIRE(rm.multiplier == Catch::Approx(4.f));
}

TEST_CASE("freeze slicing math") {
    auto b = MakeBtc();
    // base = buffer/8: deviation d with exp2(-11 d)=1/8 → d=3/11
    float knob = 0.5f - 0.5f * (3.f / 11.f);
    auto r = b.Update(knob, 0.f, 1.0f, false, -1, 64);
    REQUIRE(r.slice_count == 8);
    REQUIRE(r.slice_index == 7);
}

TEST_CASE("subdivision zone hysteresis") {
    auto b = MakeBtc();
    b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    for (int i = 0; i < 374; ++i) b.Update(0.5f, 0.f, 0.f, true, -1, 64);
    b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    // park knob just below a CCW zone edge, wiggle within hysteresis: no change
    auto a1 = b.Update(0.376f, 0.f, 0.f, true, -1, 64);
    auto a2 = b.Update(0.374f, 0.f, 0.f, true, -1, 64);
    REQUIRE(a1.base_samples == Catch::Approx(a2.base_samples));
}
```

(Zone-edge value 0.375 assumes 4 equal CCW zones over [0, 0.5): edges at
0.125, 0.25, 0.375. If the implementation chooses different edges, adjust
the test to two points 0.002 apart straddling an actual edge.)

- [ ] **Step 2: Run to verify failure** — `./run.sh` → compile FAIL
      (`time/base_time.h` missing).

- [ ] **Step 3: Implement `BaseTimeControl`** per the behavior block above.
      Keep all state in the class; no statics. `Update` must be branch-light
      but it runs at block rate, so clarity wins over micro-optimizing.

- [ ] **Step 4: Run tests** — `./run.sh` → all PASS (fix mapping constants,
      not tests, unless a test contradicts the spec).

- [ ] **Step 5: Commit** —
      `git add src/beadsdelay tests/beadsdelay_dsp && git commit -m "beads-delay: base-time control with clock and subdivisions"`

---

### Task 3: EchoEngine — tap 1, per-sample feedback, doppler + crossfade

**Files:**
- Create: `src/beadsdelay/dsp/src/engine/echo_engine.h`
- Create: `src/beadsdelay/dsp/src/engine/echo_engine.cpp`
- Modify: `src/beadsdelay/dsp/src/echos_processor.h` (+ engine member)
- Modify: `src/beadsdelay/dsp/src/echos_processor.cpp` (wire signal path)
- Test: `tests/beadsdelay_dsp/test_echo_engine.cpp`

**Interfaces:**
- Consumes: `particules_dsp::RecordingBuffer` (`Write`,
  `ReadHermiteStereoFast`, `write_head()`, `size()`, `decimation_factor()`),
  `BaseTimeControl::Result`, `particules_dsp::Saturation::LimitFeedback`,
  `StateVariableFilter::ProcessHP`.
- Produces:
```cpp
#pragma once
#include "../../include/beadsdelay_dsp/types.h"
#include "buffer/recording_buffer.h"

namespace beadsdelay_dsp {

// Owns read-head positioning over the shared RecordingBuffer.
// All distances INTERNALLY in buffer frames (host samples ÷ decimation).
class EchoEngine {
public:
    void Init(particules_dsp::RecordingBuffer* buffer, float sample_rate);

    // Block-rate: set targets. delay_samples/tap2 in HOST samples.
    void SetTargets(float delay_samples, bool multi_tap,
                    TimeChangeMode mode, float slew_seconds);
    void NotifyFreeze(bool frozen, float slice_len_samples, int slice_index);

    // Per-sample: advance read position by 1/decimation host-sample and
    // read the wet tap(s). Returns wet (tap1 + kTap2Gain*tap2).
    StereoFrame ReadWet();

    float CurrentDelaySamples() const;   // slewed actual (host samples)

private:
    particules_dsp::RecordingBuffer* buf_ = nullptr;
    float sample_rate_ = 48000.f;
    float delay_frames_ = 4800.f;        // slewed, buffer frames
    float target_frames_ = 4800.f;
    float slew_coeff_ = 0.001f;          // per-sample one-pole
    bool  multi_tap_ = false;
    TimeChangeMode mode_ = TimeChangeMode::kTape;
    // crossfade-jump state (kCrossfade mode)
    float fade_from_frames_ = 0.f, fade_pos_ = 1.f, fade_step_ = 0.f;
    float queued_target_ = -1.f;
    // freeze state
    bool  frozen_ = false;
    float slice_start_ = 0.f, slice_len_frames_ = 1.f, slice_phase_ = 0.f;
    float frozen_anchor_ = 0.f;          // write head at freeze
    float read_subsample_ = 0.f;         // accumulates 1/decimation steps
};

} // namespace beadsdelay_dsp
```

Core math (implement exactly):
- Conversion: `frames = host_samples / buf_->decimation_factor()`.
- **Tape mode**: per sample `delay_frames_ += slew_coeff_ * (target_frames_ - delay_frames_)`
  with `slew_coeff_ = 1 - exp(-1 / (slew_seconds * sample_rate))` computed
  in `SetTargets` (block rate). Read position =
  `write_pos_continuous - delay_frames_` where `write_pos_continuous =
  write_head + decimation_counter/decimation` — approximate with an
  internally-tracked float `read_subsample_` accumulating
  `1/decimation` per ReadWet() call and re-syncing to `write_head()` at
  block boundaries (SetTargets). Wrap into [0, size) before
  `ReadHermiteStereoFast`.
- **Crossfade mode**: `delay_frames_` jumps are executed as: keep reading at
  `fade_from_frames_`, also read at `target_frames_`, output equal-power mix
  `sqrt(1-t)/sqrt(t)` (use `std::sqrt` — block… no: per-sample; use
  `w1*w1 + w2*w2 = 1` via precomputed per-sample linear `t` and
  `w1 = cosf`… **No transcendentals per sample**: linear crossfade of gains
  `g_new = t, g_old = 1 - t` is acceptable for correlated material and is
  what we ship; note as tunable).
  `fade_step_ = 1/kJumpCrossfadeFrames`; a retarget mid-fade stores
  `queued_target_` and runs when the current fade ends (DLD pattern).
- **Feedback loop lives in the processor**, not the engine (engine only
  reads). Processor per-sample order:
  1. `wet = engine.ReadWet()`  (raw taps)
  2. (later tasks insert shifter/envelope here)
  3. `fb = saturation.LimitFeedback(wet * smoothed_feedback, quality)`
  4. `fb.l = feedback_hp_l.ProcessHP(fb.l)` (10 Hz, likewise r)
  5. `write_frame = quality_in(input_trimmed + fb)`; `buffer.Write(write_frame)`
  6. `out = dry*(1-mix) + wet_final*mix` (linear mix; equal-power later if
     it ever matters — hardware crossfade behavior unknown)
- Freeze handled in Task 7 (stubs return unfrozen behavior; keep the
  NotifyFreeze signature now so the interface is stable).

- [ ] **Step 1: Write failing tests** (`test_echo_engine.cpp`) — these test
  through the PUBLIC processor API (impulse → delayed impulse), which pins
  the whole loop, plus engine-level slew checks:

```cpp
#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <vector>
#include <cmath>
#include "beadsdelay_dsp/echos_dsp.h"
using namespace beadsdelay_dsp;

namespace {
struct Proc {
    void* mem = nullptr; EchosProcessor p;
    explicit Proc(float sr = 48000.f) {
        auto req = EchosProcessor::GetMemoryRequirements(sr);
        posix_memalign(&mem, req.alignment, req.total_bytes);
        p.Init(mem, req.total_bytes, sr);
    }
    ~Proc() { std::free(mem); }
};
// density knob for a target base time (manual mode inverse mapping)
float KnobForSeconds(float seconds, float buffer_seconds = 4.f) {
    float d = -std::log2(seconds / buffer_seconds) / 11.0f; // kManualOctaves
    return 0.5f - 0.5f * d;   // CCW side
}
int FindPeak(const std::vector<StereoFrame>& v, int from) {
    int best = from; float mag = 0.f;
    for (int i = from; i < (int)v.size(); ++i)
        if (std::fabs(v[i].l) > mag) { mag = std::fabs(v[i].l); best = i; }
    return best;
}
} // namespace

TEST_CASE("impulse comes back at the set delay time") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f;          // wet only
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.1f);   // 100 ms → 4800 samples
    params.time = 0.f;             // 1× multiplier
    proc.p.SetParameters(params);
    // settle smoothing, then impulse
    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[9600] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    int peak = FindPeak(out, 9700);
    REQUIRE(peak == Catch::Approx(9600 + 4800).margin(48)); // ±1 ms
}

TEST_CASE("feedback produces decaying repeats") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.5f;
    params.density = KnobForSeconds(0.05f);  // 2400 samples
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[4800] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    int p1 = FindPeak(out, 4900);            // first repeat
    float a1 = std::fabs(out[p1].l);
    int p2 = FindPeak(out, p1 + 1200);       // second repeat
    float a2 = std::fabs(out[p2].l);
    REQUIRE(p2 - p1 == Catch::Approx(2400).margin(48));
    REQUIRE(a2 < a1);
    REQUIRE(a2 > a1 * 0.2f);                 // roughly fb-proportional
}

TEST_CASE("multi-tap adds an earlier tap on the CW side") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f; params.feedback = 0.f;
    params.density = 1.f - (1.f - KnobForSeconds(0.1f));  // CW mirror: 0.5+0.5*d
    { float d = -std::log2(0.1f / 4.f) / 11.f; params.density = 0.5f + 0.5f * d; }
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[9600] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    // golden-ratio tap at 0.618*4800 ≈ 2967 before the main tap
    int t2 = FindPeak(out, 9700);
    REQUIRE(t2 == Catch::Approx(9600 + 2967).margin(60));
    int t1 = FindPeak(out, 9600 + 3600);
    REQUIRE(t1 == Catch::Approx(9600 + 4800).margin(60));
}

TEST_CASE("tape mode: delay-time jump glides (no instant jump)") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f; params.feedback = 0.f;
    params.density = KnobForSeconds(0.1f);
    params.slew_seconds = 0.2f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> settle(4800, StereoFrame{0.f,0.f});
    std::vector<StereoFrame> out(settle.size());
    proc.p.Process(settle.data(), out.data(), settle.size());
    float before = proc.p.DelayTimeSeconds();
    params.density = KnobForSeconds(0.2f);
    proc.p.SetParameters(params);
    proc.p.Process(settle.data(), out.data(), 2400); // 50 ms later
    float mid = proc.p.DelayTimeSeconds();
    REQUIRE(mid > before * 1.05f);
    REQUIRE(mid < 0.19f);          // still slewing, not arrived
}

TEST_CASE("NaN input does not poison the buffer") {
    Proc proc;
    EchosParameters params; params.dry_wet = 1.f; params.feedback = 0.9f;
    params.density = KnobForSeconds(0.01f);
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(9600, StereoFrame{0.f, 0.f});
    in[100] = {NAN, INFINITY};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    for (size_t i = 4800; i < out.size(); ++i)
        REQUIRE(std::isfinite(out[i].l));
}
```

- [ ] **Step 2: Run to verify failure** — `./run.sh` → FAIL (no engine).

- [ ] **Step 3: Implement** `EchoEngine` + processor wiring per the core
  math block. NaN guard: sanitize input frames at the top of ProcessBlock
  (`!isfinite → 0`) and flush feedback state if it ever goes non-finite
  (check once per block, not per sample). Input trim: linear gain computed
  block-rate from `input_trim_db`.

- [ ] **Step 4: Run tests** — all PASS, including Task 1 & 2 suites.

- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: echo engine with per-sample feedback and doppler slew"`

---

### Task 4: Rotary-head pitch shifter

**Files:**
- Create: `src/beadsdelay/dsp/src/pitch/rotary_shifter.h`
- Create: `src/beadsdelay/dsp/src/pitch/rotary_shifter.cpp`
- Modify: `echos_processor.h/.cpp` (insert into wet path)
- Test: `tests/beadsdelay_dsp/test_rotary_shifter.cpp`

**Interfaces:**
- Produces:
```cpp
#pragma once
#include "../../include/beadsdelay_dsp/types.h"

namespace beadsdelay_dsp {

// Classic dual-head "rotary" pitch shifter (Clouds/Beads style).
// Own small ring buffer; memory provided by Impl (member array, 4096
// stereo frames = 32 KB, fine inside the Impl allocation).
class RotaryShifter {
public:
    void Init(float sample_rate);
    void SetRatio(float ratio);        // block rate; 1.0 = bypass request
    bool Bypassed() const;
    StereoFrame Process(StereoFrame in);   // per sample

private:
    float buf_l_[kShifterSize];
    float buf_r_[kShifterSize];
    size_t write_ = 0;
    float phase_ = 0.f;      // 0..1 head sweep
    float phase_inc_ = 0.f;  // (1 - ratio) / kShifterSize, per sample
    bool  bypass_ = true;
    float bypass_xfade_ = 1.f;  // declick on engage/disengage, 256-sample ramp
};

} // namespace beadsdelay_dsp
```

Per-sample Process (when not bypassed):
```cpp
// triangular windows, two heads a half-period apart
float p1 = phase_;
float p2 = phase_ + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
float w1 = 2.f * (p1 < 0.5f ? p1 : 1.f - p1);   // tri window 0..1..0
float w2 = 2.f * (p2 < 0.5f ? p2 : 1.f - p2);
float d1 = p1 * (kShifterSize - 4);
float d2 = p2 * (kShifterSize - 4);
// linear-interp reads at write_ - d1, write_ - d2 (mask & (kShifterSize-1))
// out = read1*w1 + read2*w2  (per channel)
// write input to ring, advance write_, phase_ += phase_inc_, wrap to [0,1)
```
`SetRatio(r)`: `phase_inc_ = (1.f - r) / kShifterSize`; bypass when
`|1 - r| < ratio_epsilon` where the processor computes
`r = exp2(semitones/12)` block-rate and treats
`|semitones| < kShifterBypassSemitones` as bypass. Bypass transitions ramp
`bypass_xfade_` over 256 samples mixing dry-through vs shifted to avoid
clicks. When bypassed and ramp complete, Process returns input unchanged
(and skips ring writes — but MUST keep writing the ring during the
disengage ramp; simplest: keep writing the ring whenever not fully
bypassed).

Processor wet path becomes: `wet = shifter.Process(engine.ReadWet())` with
attenurandomized pitch (Task 6 adds AR; for now raw `pitch_semitones`).

- [ ] **Step 1: Failing tests** (`test_rotary_shifter.cpp`):
```cpp
#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include <vector>
#include "pitch/rotary_shifter.h"
using namespace beadsdelay_dsp;

// crude pitch estimate: count zero crossings of a sine after shifting
static float MeasureRatio(float semitones) {
    RotaryShifter s;
    s.Init(48000.f);
    s.SetRatio(std::exp2(semitones / 12.f));
    float freq = 440.f / 48000.f;
    int crossings = 0; float prev = 0.f;
    const int N = 48000;
    for (int i = 0; i < N; ++i) {
        float x = std::sin(2.f * 3.14159265f * freq * i);
        StereoFrame y = s.Process({x, x});
        if (i > 4096) {  // skip warmup
            if (prev <= 0.f && y.l > 0.f) crossings++;
            prev = y.l;
        }
    }
    float measured = crossings / ((N - 4096) / 48000.f);
    return measured / 440.f;
}

TEST_CASE("octave up shifts frequency by ~2x") {
    REQUIRE(MeasureRatio(12.f) == Catch::Approx(2.f).epsilon(0.06));
}
TEST_CASE("octave down shifts frequency by ~0.5x") {
    REQUIRE(MeasureRatio(-12.f) == Catch::Approx(0.5f).epsilon(0.06));
}
TEST_CASE("ratio 1 is exact passthrough after ramp") {
    RotaryShifter s; s.Init(48000.f); s.SetRatio(1.f);
    for (int i = 0; i < 512; ++i) s.Process({0.f, 0.f});
    StereoFrame y = s.Process({0.7f, -0.3f});
    REQUIRE(y.l == 0.7f);
    REQUIRE(y.r == -0.3f);
}
```
Plus a processor-level test in the same file: impulse → wet with
`pitch_semitones = 12` still produces output (non-silence) at roughly the
delay time (margin 4096 samples for shifter latency), proving integration.

- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement** shifter + processor insertion.
- [ ] **Step 4: Run — PASS** (zero-crossing tolerance is loose on purpose;
      windowed shifters warble).
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: rotary-head pitch shifter in wet path"`

---

### Task 5: SHAPE repeat envelope

**Files:**
- Create: `src/beadsdelay/dsp/src/env/repeat_envelope.h` (header-only)
- Modify: `echos_processor.h/.cpp`
- Test: `tests/beadsdelay_dsp/test_repeat_envelope.cpp`

**Interfaces:**
```cpp
#pragma once
#include <cmath>
#include "../../include/beadsdelay_dsp/types.h"
#include "util/dsp_utils.h"   // particules_dsp::Crossfade, cosine table

namespace beadsdelay_dsp {

// Tempo-synced amplitude envelope on the wet path. Phase period = base
// delay time; phase advances per sample, resyncs to 0 on clock ticks.
class RepeatEnvelope {
public:
    void Init(float sample_rate) { sample_rate_ = sample_rate; phase_ = 0.f; }
    // block rate:
    void SetPeriodSamples(float period) { inc_ = period > 1.f ? 1.f / period : 1.f; }
    void SetShape(float shape) { shape_ = shape; }   // 0..1
    void SyncPhase() { phase_ = 0.f; }
    // per sample: returns gain 0..1
    float Next() {
        phase_ += inc_; if (phase_ >= 1.f) phase_ -= 1.f;
        if (shape_ <= 0.001f) return 1.f;
        return Gain(phase_);
    }
private:
    float Gain(float ph) const;   // the 3-segment morph (see plan constants)
    float sample_rate_ = 48000.f, phase_ = 0.f, inc_ = 0.f, shape_ = 0.f;
};

} // namespace beadsdelay_dsp
```
`Gain(ph)` morph, `s = shape_`:
- `s ∈ (0, ⅓]`: `t = s*3`; gate with duty `0.9 − 0.4*t`, 5 ms edges
  (edge fraction = `0.005f * sample_rate_ * inc_` of phase), crossfaded
  with flat: `Crossfade(1.f, gate, t)`.
- `s ∈ (⅓, ⅔]`: `t = (s-⅓)*3`; `Crossfade(gate(duty 0.5), hann, t)` where
  `hann = 0.5 − 0.5*cos(2π·ph)` — use `particules_dsp` cosine table, no
  `std::cos` per sample.
- `s ∈ (⅔, 1]`: `t = (s-⅔)*3`; `Crossfade(hann, ramp², t)` with
  `ramp = ph` (slow attack, drops at wrap).

Processor: `wet *= env.Next()` after the shifter; feedback source is
post-envelope by default, pre-envelope when
`params.envelope_pre_feedback` (i.e. capture `fb_src` before applying env).
Envelope phase syncs on clock ticks (`clock_tick_offset >= 0` at block
start — sample-offset precision not required; sync at block boundary).

- [ ] **Step 1: Failing tests**: (a) `shape=0` → gains all 1 (wet impulse
  train unchanged vs Task 3 baseline); (b) `shape=0.25` with 100 ms period:
  process a constant 1.0 input with dry_wet=1, feedback=0, density at
  ~100 ms; after settle, the wet output must be near-zero in the last 30%
  of each period (gate closed) and near-full early in the period —
  measure by RMS over phase windows using DelayTimeSeconds() to locate
  period boundaries; (c) unit-test `Gain()` directly: monotone duty
  shrink, hann at s=⅔ symmetric around 0.5.

Include the actual test code in the file; follow the structure of Task 3's
tests (Proc fixture). Direct `Gain()` tests instantiate `RepeatEnvelope`,
`SetShape`, `SetPeriodSamples(4800)`, step `Next()` 4800× collecting gains,
then assert window properties (max gain > 0.95 in first half; for s=0.25,
gain < 0.05 in last 20% of the period).

- [ ] **Step 2: Run — FAIL.**  
- [ ] **Step 3: Implement.**  
- [ ] **Step 4: Run — PASS.**  
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: tempo-synced repeat envelope on SHAPE"`

---

### Task 6: Slow random LFOs + attenurandomizer modulation

**Files:**
- Create: `src/beadsdelay/dsp/src/mod/slow_random_lfo.h`
- Create: `src/beadsdelay/dsp/src/mod/ar_modulator.h`
- Modify: `echos_processor.h/.cpp` (modulate TIME/PITCH/SHAPE at block rate)
- Test: `tests/beadsdelay_dsp/test_ar_modulator.cpp`

**Interfaces:**
```cpp
// slow_random_lfo.h
#pragma once
#include "random/random.h"
#include "util/dsp_utils.h"

namespace beadsdelay_dsp {

// Two-point cosine-interpolated random, advanced at block rate.
class SlowRandomLfo {
public:
    void Init(particules_dsp::Random* rng, uint32_t salt) {
        rng_ = rng; salt_ = salt; from_ = 0.f; to_ = 0.f; phase_ = 1.f;
    }
    void SetRate(float hz, float sample_rate) { rate_hz_ = hz; sr_ = sample_rate; }
    // advance by n samples, return value in [-1, 1]
    float Next(size_t n) {
        phase_ += rate_hz_ / sr_ * n;
        if (phase_ >= 1.f) { phase_ -= (int)phase_; from_ = to_; to_ = rng_->NextBipolar(); }
        float t = 0.5f - 0.5f * std::cos(phase_ * particules_dsp::kPi); // block rate: std::cos OK
        return from_ + (to_ - from_) * t;
    }
private:
    particules_dsp::Random* rng_ = nullptr;
    uint32_t salt_ = 0;
    float from_ = 0.f, to_ = 0.f, phase_ = 1.f, rate_hz_ = 0.15f, sr_ = 48000.f;
};

// ar_modulator.h — delay-flavor attenurandomizer (continuous, block rate)
#pragma once
#include "slow_random_lfo.h"

namespace beadsdelay_dsp {
struct ArModulator {
    SlowRandomLfo lfo;
    // returns modulation in normalized units (caller scales)
    float Process(float ar, float cv_norm, bool cv_connected, size_t block_frames) {
        float l = lfo.Next(block_frames);
        if (ar == 0.f) return cv_connected ? 0.f : 0.f;   // noon: nothing
        if (cv_connected)
            return ar > 0.f ? ar * cv_norm : (-ar) * l * std::fabs(cv_norm);
        return ar > 0.f ? ar * l : (-ar) * (l * l * l);   // uniform vs peaky
    }
};
} // namespace beadsdelay_dsp
```
Processor wiring (block rate, before BaseTimeControl.Update / SetRatio /
SetShape):
- `time_knob_eff = clamp(params.time + time_mod, 0, 1)` with
  `time_mod = ar_time.Process(params.time_ar, params.time_cv/5, connected, n)`.
- `pitch_semi_eff = clamp(params.pitch_semitones + pitch_mod_semi, -24, 24)`,
  `pitch_mod_semi = params.pitch_ar > 0 && connected ?
   params.pitch_ar * params.pitch_cv * 12 : ar_pitch.Process(...) * 24`
  (unpatched/CCW randomization spans ±24 st scaled by AR; CV+CW is 1 V/oct
  at full CW — exactly the Global Constraints AR semantics).
- `shape_eff = clamp(params.shape + shape_mod, 0, 1)`.
- FEEDBACK/BLEND CV (no AR): `feedback_eff = clamp(params.feedback +
  params.feedback_cv / 5.f, 0.f, 1.f)`, same for dry/wet.
- Three `ArModulator` members seeded with salts 1, 2, 3 sharing one
  `particules_dsp::Random` member.

- [ ] **Step 1: Failing tests**: (a) AR at noon + no CV → `DelayTimeSeconds`
  stable across 100 blocks (variance < 1e-6); (b) `time_ar = -1`, no CV →
  delay time wanders: collect `DelayTimeSeconds()` over 20 s of processing,
  REQUIRE stddev > 0; (c) `pitch_ar = 1` + `pitch_cv = 1.0` (connected) →
  effective ratio ≈ octave up: reuse the zero-crossing measure through the
  full processor on a 440 Hz sine with delay ~50 ms, wet-only; (d) unit:
  `ArModulator::Process(0.5, 1.0, true, 64) == 0.5`;
  `Process(-1, 0, false, 64)` values stay within [-1, 1] and change over
  many calls. Write the real code following prior fixtures.
- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement** (both headers + processor wiring).
- [ ] **Step 4: Run — PASS.**
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: slow-random attenurandomizers on time, pitch, shape"`

---

### Task 7: FREEZE beat slicer

**Files:**
- Modify: `src/beadsdelay/dsp/src/engine/echo_engine.h/.cpp` (implement the
  freeze path stubbed in Task 3)
- Modify: `echos_processor.cpp` (freeze plumbing:
  `recording_buffer.NotifyFreeze`, stop writes, slice params from
  BaseTimeControl)
- Test: `tests/beadsdelay_dsp/test_freeze_slicer.cpp`

**Interfaces:** already fixed in Task 3 (`EchoEngine::NotifyFreeze(bool,
float slice_len_samples, int slice_index)`). Processor calls it on freeze
edges and per block while frozen (slice length/index may change live).

Behavior:
- Freeze edge on: `frozen_anchor_ = write_head` (buffer frames);
  `buf_->NotifyFreeze(true)` (declick fade at seam); writes stop (processor
  skips `Write`; feedback loop keeps running into the void — i.e. computed
  but discarded, so unfreeze doesn't pop).
- While frozen: slice k = `slice_index` (clamped to `slice_count-1`);
  read region start `slice_start_ = wrap(frozen_anchor_ - (k+1)*slice_len_frames)`;
  `slice_phase_` advances by `1/decimation` per sample and wraps at
  `slice_len_frames_`; read position = `slice_start_ + slice_phase_`.
  Seam declick: 64-frame linear crossfade with region start (read both ends
  during the last 64 frames of the slice). PITCH/SHAPE stay live.
- Unfreeze: `buf_->NotifyFreeze(false)` (write crossfade), resume normal
  read positioning (tape-slew from wherever the frozen read was — reuse the
  existing retarget path).

- [ ] **Step 1: Failing tests**: (a) record a 10 Hz sine for 2 s, freeze,
  feed silence for 4 s → output stays non-silent (RMS > 0.05) while wet=1;
  (b) frozen slice loops with period = base time: autocorrelate 2 s of
  frozen output, peak lag ≈ base samples (margin 2%); (c) TIME knob change
  while frozen selects a different slice: freeze after recording a ramp
  (sample value = index/N), read slice at time=0 vs time=1, mean output
  values differ; (d) unfreeze → live input reappears within 200 ms.
  Real code per prior fixture patterns.
- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run — PASS (all suites).**
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: freeze beat-slicer with slice select on TIME"`

---

### Task 8: Quality modes (decimation, character, feedback limiting, wow)

**Files:**
- Modify: `echos_processor.cpp`
- Test: `tests/beadsdelay_dsp/test_quality_modes.cpp`

**Interfaces:** consumes `particules_dsp::QualityProcessor`
(`ProcessInput/ProcessOutput/GetPitchModulation`),
`RecordingBuffer::SetDecimationFactor`,
`particules_dsp::DecimationFactorForQuality`,
`Saturation::LimitFeedback(frame, mode)` (already wired in Task 3 — this
task passes the *current mode* instead of kHiFi everywhere).

Behavior (all follows Particules' proven pattern —
`src/particules/dsp/src/particules_processor.cpp` is the reference; read
its quality handling before implementing):
- On quality change: 8192-sample output duck/crossfade + `Clear()`
  (deferred) + `TickClear(per-block chunk)` each block + new decimation
  factor. Copy the Particules approach (constants included).
- `ProcessInput` before writes; `ProcessOutput` on wet after shifter,
  before envelope.
- Tape wow/flutter: `GetPitchModulation(mode, block_frames)` → multiply
  into the read-position delta per block (engine gets a
  `SetReadRateScale(float)` — add this small block-rate setter to
  EchoEngine: read advance becomes `scale/decimation` per sample).
- Base time scales with buffer seconds: effective buffer duration =
  `kBufferFrames * decimation / sample_rate`; BaseTimeControl must be
  re-inited (or given a setter `SetBufferSeconds(float)`) on quality
  change — add that setter.

- [ ] **Step 1: Failing tests**: (a) effective delay doubles in kClouds:
  same density knob, impulse delay measured (as Task 3) is 2× the kHiFi
  value; (b) kTape mode output on a 1 kHz sine shows pitch wobble:
  measure zero-crossing intervals variance > HiFi's; (c) feedback at 1.0
  in kHiFi stays bounded (|out| ≤ 1.5 after 10 s of noise input);
  (d) quality switch mid-stream produces no sample with |out| > 2 and no
  NaN. Real code.
- [ ] **Step 2: Run — FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run — PASS.**
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: quality modes with decimation and per-mode feedback"`

---

### Task 9: Hardening — block invariance, params edge cases, telemetry

**Files:**
- Modify: `echos_processor.cpp` (fixes only as tests demand)
- Test: `tests/beadsdelay_dsp/test_hardening.cpp`

- [ ] **Step 1: Failing tests**: (a) block-size invariance: same 2 s input
  processed in chunks of 1, 7, 64, 512 → identical outputs within 1e-4
  (feed identical parameter streams; compare sample-by-sample);
  (b) every parameter at min/max simultaneously (all 4 corners of
  density×time, pitch ±24, shape 1, feedback 1, freeze toggling every
  block) for 5 s of noise → all outputs finite, no assert;
  (c) `ClearBuffer()` mid-feedback → tail dies within 200 ms;
  (d) telemetry: `BaseTimeSeconds()`/`DelayTimeSeconds()` match the
  impulse-measured delay within 5%; `IsClocked()` false→true after two
  ticks. Real code.
- [ ] **Step 2: Run — expect (a) or (b) to FAIL** (typical culprits:
  block-rate smoothing coefficients that depend on n, per-block LFO
  advance). Fix by making coefficients n-aware (`coeff = 1-exp(-n/(tau*sr))`
  per block) exactly as Particules' `dry_wet_coeff_frames` cache does.
- [ ] **Step 3: Fix until PASS.**
- [ ] **Step 4: Full suite run**: `cd tests && ./run.sh` AND
  `cd tests/beadsdelay_dsp && ./run.sh` AND
  `cd tests/particules_dsp && ./run.sh` — all green.
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: block-size invariance and edge-case hardening"`

---

### Task 10: VCV adapter (Echos.cpp), registration, persistence, menu

**Files:**
- Create: `src/beadsdelay/Echos.cpp`
- Create: `src/beadsdelay/echos_block_runtime.h`
- Modify: `src/plugin.hpp` (add `extern Model* modelEchos;`)
- Modify: `src/plugin.cpp` (register in BOTH branches: `p->addModel(modelEchos);`)
- Modify: `plugin.json` (add module entry)
- Modify: `metamodule/plugin-mm.json` (mirror entry — match its format)
- Modify: `vcv/Makefile` — add after the particules lines (18-25):
  `SOURCES += $(wildcard ../src/beadsdelay/*.cpp)`
  `SOURCES += $(wildcard ../src/beadsdelay/dsp/src/*.cpp)`
  `SOURCES += $(wildcard ../src/beadsdelay/dsp/src/*/*.cpp)`
  and an include flag if the Makefile has FLAGS/CFLAGS lines for
  particules includes (check nearby lines; mirror them for
  `../src/beadsdelay/dsp/include`)

**Interfaces:**
- Consumes: `EchosProcessor` public API (Task 1), `pitchKnobToSemitones`
  (`src/particules/pitch_notch_map.hpp` via `plugin.hpp`),
  `PitchParamQuantity` and `kQualityColors` from `plugin.hpp`,
  `particules::EnableMetaModuleFlushToZero()`
  (`src/particules/metamodule_fpu.h`).
- Produces: `Model* modelEchos`, module slug `"Echos"`.

Structure — follow `src/particules/Particules.cpp` closely (read it first);
differences:
- Param enum: `FREEZE_PARAM, DENSITY_PARAM, TIME_PARAM, PITCH_PARAM,
  SHAPE_PARAM, FEEDBACK_PARAM, DRY_WET_PARAM, TIME_AR_PARAM, PITCH_AR_PARAM,
  SHAPE_AR_PARAM, QUALITY_PARAM, SEED_PARAM (momentary button), PARAMS_LEN`.
- Inputs: `IN_L, IN_R, DENSITY_CV, TIME_CV, PITCH_CV, SHAPE_CV,
  FEEDBACK_CV, DRY_WET_CV, SEED_INPUT, FREEZE_INPUT, INPUTS_LEN`.
  Outputs: `OUT_L, OUT_R`. Lights: `QUALITY_R/G/B, FREEZE_BUTTON_LIGHT,
  SEED_LIGHT`.
- `configParam(DENSITY_PARAM, 0,1,0.5,"Density")`,
  `configParam(TIME_PARAM, 0,1,0,"Time")`,
  `configParam<PitchParamQuantity>(PITCH_PARAM, 0,1,0.5,"Pitch")`,
  `configParam(SHAPE_PARAM, 0,1,0,"Shape")`,
  `configParam(FEEDBACK_PARAM, 0,1,0,"Feedback")`,
  `configParam(DRY_WET_PARAM, 0,1,0.5,"Dry/wet")`, ARs −1..1 default 0,
  `configButton(SEED_PARAM, "Tap tempo")`, FREEZE + QUALITY exactly as
  Particules (including the `#ifdef METAMODULE` 4-position QUALITY switch
  + `QualityMmSwitch` widget reusing `res/quality_*.svg` frame paths — the
  PNGs already exist in `metamodule/assets/`).
- `echos_block_runtime.h`: template block accumulator with
  `PushInputSample/BlockReady/InputBuffer/CommitProcessedBlock/
  ReadOutputSample` (copy the data-path parts of
  `ParticulesBlockRuntime`, drop grain-LED/trigger-pulse machinery) plus
  `NoteClockEdgeSample(bool high, int index_in_block)` capturing the FIRST
  rising-edge offset per block → `int TakeClockTickOffset()` (-1 if none).
- process(): per-sample — FTZ arm, freeze gate Schmitt, SEED jack Schmitt +
  SEED button edge → both feed `NoteClockEdgeSample`, push input
  (`getVoltage()*0.2f`, finite-guard, R normalized to L), read output
  (`*5.f`, R-unpatched sums to L — copy Particules lines 430-434), block
  boundary → populate `EchosParameters` (pitch via cached
  `pitchKnobToSemitones` — copy the cache pattern), `SetParameters`,
  `Process`, commit. Lights: FREEZE binary; QUALITY color from
  `kQualityColors`; SEED_LIGHT blinks: brightness =
  `phase < 0.1 ? 1 : 0` with phase advancing by
  `blockFrames / (BaseTimeSeconds()*sr)` per block.
- Context menu (`appendContextMenu`, wrap mutations in the same
  `withMenuUndo` pattern — copy the template): Quality submenu (4 items),
  Time-change response (Tape/Crossfade), Envelope feedback tap
  (Post/Pre), sliders: Input trim (−12..+12 dB), Doppler slew (0.01–1 s,
  log), Random LFO rate (0.02–2 Hz, log) — model each on
  `ManualGainQuantity`/`ManualGainSlider` from Particules.cpp, guarded
  `#ifndef METAMODULE` for the slider widgets (MM menus can't host
  sliders; expose Tape/Crossfade + envelope tap as plain toggle items,
  which MM supports).
- `dataToJson/dataFromJson`: quality_state_, time_change_mode,
  envelope_pre_feedback, input_trim_db, slew_seconds, random_lfo_hz.
- `onReset`: defaults + `clear_requested_` pattern (atomic, applied at
  block boundary) — copy Particules.
- Widget: `#ifndef METAMODULE` loads `res/Echos.svg`; positions are
  placeholders on a 12 HP grid for now (Task 11 finalizes) — use 4 columns
  x = 8.6/23.6/38.6/53.6 mm, knob rows y = 42/62/82 mm, jack rows
  y = 100/113 mm; `createModel<Echos, EchosWidget>("Echos")`.
- plugin.json entry (after Ondes):
  `{ "slug": "Echos", "name": "Échos", "description": "Beads-style delay: clockable, freezable, pitch-shifting.", "tags": ["Delay", "Effect"] }`

- [ ] **Step 1: Write the adapter + registration** (no TDD harness for
  Rack glue; the compiler is the test).
- [ ] **Step 2: Build VCV**: `make -C vcv -j8` (repo pattern per memory:
  no build-install.sh here; check `vcv/Makefile` exists, else `make` at
  root — whichever Loooop uses). Expected: clean compile. An `res/Echos.svg`
  missing at runtime is fine for compile; create a placeholder SVG now:
  copy `res/Ondes.svg` to `res/Echos.svg` (Task 11 replaces it).
- [ ] **Step 3: Install + headless smoke**: copy dylib/json/res into the
  Rack2 plugins dir (per `robotboy-vcv-install` memory pattern), then run
  the `test-vcv-module-headless` skill's host with a WAV through Echos:
  verify a delayed copy appears (the skill doc describes invocation).
  If the headless host needs a fixture file, add
  `tests/headless/echos_smoke.json` per the skill's convention.
- [ ] **Step 4: Run all DSP test lanes again** (guard against accidental
  particules edits): `cd tests && ./run.sh`.
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: VCV adapter, registration, context menu"`

---

### Task 11: Panel

**Files:**
- Create: `panel-specs/echos.yaml`
- Create: `res/Echos.svg` (generated)
- Modify: `src/beadsdelay/Echos.cpp` (sync positions)
- Create: `metamodule/assets/Echos.png` (generated)

Use the **vcv-panel-generate skill** (read it first — it documents the YAML
schema, generator invocation, and PNG export). Model the YAML on
`panel-specs/ondes.yaml`. Requirements:
- 12 HP. Title `ÉCHOS`. House theme (auto-inherited).
- Row 1: big knobs DENSITY, TIME, PITCH. Row 2: SHAPE, FEEDBACK, BLEND.
- Row 3: trimpots TIME AR, PITCH AR, SHAPE AR (aligned under their knobs
  conceptually; label `{blank}`, connectors from CV jacks).
- Row 4 (jacks): DENSITY, TIME, PITCH, SHAPE CV. Row 5 (jacks): FB, BLEND,
  SEED, FREEZE. Row 6: IN L, IN R, OUT L, OUT R.
- Buttons: FREEZE (top-left, light), SEED (top-right, light), QUALITY
  (top-center, RGB light) — mirror Particules' header arrangement.
- Remember (memory note): positions live in the .cpp — after generating,
  transfer the emitted mm coordinates into `mm2px(Vec(...))` calls in
  `EchosWidget`, and regenerate the preview with `--open` if checking
  (preview.py gotcha).
- PNG for MM: follow the skill/`vcv-to-metamodule` conventions used for
  `Ondes.png` (same pixel density).

- [ ] **Step 1: Write `panel-specs/echos.yaml`** per above.
- [ ] **Step 2: Generate SVG + preview**; iterate until no overlaps
  (generator warnings clean).
- [ ] **Step 3: Sync positions into Echos.cpp**; build VCV again; PASS.
- [ ] **Step 4: Export `metamodule/assets/Echos.png`.**
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: panel SVG, positions, MM asset"`

---

### Task 12: MetaModule build + simulator smoke test

**Files:**
- Modify: `metamodule/CMakeLists.txt` (add adapter + dsp sources + include
  dir `${SRC}/src/beadsdelay/dsp/include`)
- Modify: `metamodule/plugin-mm.json` (the MM module list; add Echos entry
  matching the existing entries' format)

Steps:
- [ ] **Step 1: Add sources** to `target_sources(RobotBoy ...)`:
  `${SRC}/src/beadsdelay/Echos.cpp` under "VCV-adapter modules" and all
  `${SRC}/src/beadsdelay/dsp/src/**/*.cpp` files individually (list them;
  CMake here doesn't glob). Add include dirs.
- [ ] **Step 2: Build**: use the **vcv-to-metamodule / build-vcv-plugin
  skill conventions** — `cmake -B metamodule/build ...` per
  `metamodule/CMakeLists.txt` header (SDK path default is set). Expected:
  `metamodule-plugins/RobotBoy.mmplugin` produced.
- [ ] **Step 3: Simulator**: use the **build-simulator skill** to run the
  headless MetaModule simulator with a patch containing Echos (create the
  patch per the skill's docs; feed the test WAV; capture output WAV).
  Verify: delayed signal present (rerun the same analysis as the VCV
  headless test), no crash, CPU number recorded — the skill documents how
  to read module CPU load. **Record the CPU % in the worklog.**
- [ ] **Step 4: If CPU > ~15% of a core at 48 kHz**, apply in order:
  (1) linear instead of Hermite for tap 2, (2) hoist per-sample wrap
  checks, (3) LUT the shifter windows. Re-measure after each. Do not
  restructure beyond that without noting it in the worklog.
- [ ] **Step 5: Run `python3 -m unittest tests/test_no_delay_mode.py`**
  (CMakeLists no longer guarded, but confirm) and the full `tests/run.sh`.
- [ ] **Step 6: Commit** —
  `git commit -m "beads-delay: MetaModule build and simulator smoke test"`

---

### Task 13: Verification sweep + docs

**Files:**
- Modify: `CHANGELOG.md` (under Unreleased/next version: add Échos)
- Modify: `README.md` (module list — follow existing entries' format)
- Modify: `docs/superpowers/plans/2026-07-15-beads-delay-worklog.md`
- Create: `docs/superpowers/plans/2026-07-15-beads-delay-user-checklist.md`

- [ ] **Step 1: Full test sweep**: all four lanes (root `tests/run.sh`,
  `particules_dsp`, `beadsdelay_dsp`, `test_no_delay_mode.py`), VCV build,
  MM build. All green — fix anything red before proceeding.
- [ ] **Step 2: A/B sanity render**: through the headless VCV host, render
  (a) 1 kHz-pluck Karplus patch (density near min, feedback 0.95),
  (b) clocked-eighth-note echo patch, (c) freeze-slicer patch; listen-check
  files land in scratchpad; note anything anomalous in the worklog.
- [ ] **Step 3: Write the user checklist** (GUI-sim checks are user-run per
  repo policy): panel renders in Rack, knob tooltips sane, menu items
  persist through save/reload, MM screenshot, MM knob mapping, audible
  check of the three demo patches, decide on the Échos name.
- [ ] **Step 4: Update CHANGELOG + README + worklog** (decisions, CPU
  numbers, deviations).
- [ ] **Step 5: Commit** —
  `git commit -m "beads-delay: verification sweep, changelog, user checklist"`

---

## Self-review checklist (done during planning)

- Spec coverage: manual/clocked/tap base time ✓ (T2), multiplier + slices ✓
  (T2/T7), multi-tap ✓ (T3), doppler + crossfade ✓ (T3), per-sample
  feedback + per-quality limiting ✓ (T3/T8), rotary shifter + notches
  (notch map in adapter) ✓ (T4/T10), SHAPE envelope ✓ (T5), ARs + slow
  random LFOs ✓ (T6), freeze slicer ✓ (T7), quality modes ✓ (T8), CPU
  strategy ✓ (T12 + global constraints), panel ✓ (T11), context menu ✓
  (T10), tests ✓ (throughout), guard rescope ✓ (T1).
- Known deliberate deviations are listed in the spec's decision log.
- Type consistency: `EchosParameters`/`EchosProcessor` fixed in T1;
  `BaseTimeControl::Result` fixed in T2; `EchoEngine` fixed in T3 (freeze
  signature pre-declared); all later tasks consume those exact names.
