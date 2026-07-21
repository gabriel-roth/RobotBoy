# Retours Quality-Mode Feedback Degradation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Retours' four Quality modes audibly distinct as feedback accumulates: Bright stacks clean and brickwalls; Sunny/Scorched repeats get progressively darker and warmly saturated instead of converging on digital clipping.

**Architecture:** Three coordinated fixes. (1) Shared `particules_dsp::Saturation`: normalize the asymmetric soft-clip so every `LimitFeedback` curve has unity small-signal slope (kills Sunny's hidden ×0.9 decay haircut), soften Scorched's feedback limiter from a hard clip to tanh, and add a new `SaturateWrite` stage whose tape-mode ceilings (≈0.65 Sunny / ≈0.45 Scorched) sit well below the storage codec's ±1 clamp. (2) Shared `QualityProcessor` gains a per-instance tape output-LP cutoff override (defaults preserve current Particules voicing). (3) Retours wires both in: `SaturateWrite` on the input+feedback sum before the buffer write, and lower tape tone cutoffs (6.5 kHz Sunny / 2.8 kHz Scorched) suited to a multi-pass feedback loop.

**Tech Stack:** C++17, Catch2 (vendored), CMake test lanes at `tests/particules_dsp/run.sh` and `tests/retours_delay_dsp/run.sh`.

## Investigation background (why these fixes)

Measured on main with offline renders (feedback knob 0.75 → loop gain ≈0.833, 250 ms delay, wet-only):

- Per-repeat darkening exists only above ~5 kHz. At 2.2 kHz all modes lose <0.2 dB/repeat: Sunny's loop filters are 10 kHz in + 10 kHz out; Scorched's are 10 kHz in + 5 kHz out (2-pole each).
- With a continuous source, feedback accumulation lands every mode on a hard ±1 clip: Bright 66% of steady-state samples >0.985, Cold 37%, Sunny 12%, Scorched 6%. Cold/Sunny clip in the int12 codec (`sample_codec.h` clamps writes), Scorched in `LimitFeedback`'s `HardClip` + mu-law codec clamp. All modes therefore share the same "digital crunch."
- `Saturation::Process` (per-mode drive) is dead code — never called by Retours or Particules. The vendored original applied `MuLawCompress` in `ProcessInput` (real in-loop tape saturation); commit e06d2c0 moved mu-law into the storage codec and lost that stage.
- Sunny's `LimitFeedback` is `AsymmetricSoftClip(input * 0.9f)`; small-signal gain ≈0.9 → extra −0.9 dB/repeat vs other modes (measured −2.5 vs −1.6 dB/repeat).

## Design decisions (made autonomously; document, don't re-ask)

1. **Shared-vs-Retours scope.** `LimitFeedback` curve fixes go in shared code — the ×0.9 haircut and hard-clip-instead-of-tape are defects wherever they run, and Particules' feedback path benefits identically. Particules' tests only assert bounded/monotonic/sign-preserving, which the new curves satisfy. The tape tone cutoffs, by contrast, become **per-instance voicing**: a cutoff right for Particules' one-pass grain path is too mild for a delay that re-filters every round trip, so `QualityProcessor` keeps its current defaults and Retours overrides. `SaturateWrite` is added to shared `Saturation` but only Retours calls it.
2. **Write-path saturation shape:** `SoftClip(drive·x)/drive` — unity small-signal slope (no loop-gain change for quiet repeats, so decay rates stay honest), ceiling `1/drive` below the codec clamp (so accumulation compresses warmly instead of hard-clipping). Sunny drive 1.4 with 1.1× extra positive drive (asymmetric, tape bias); Scorched drive 2.2, symmetric.
3. **Bright and Cold unchanged in the write path.** Bright's brickwall is per spec ("clean brickwall"); Cold keeps its existing SoftClip limiter + int12 character.
4. **Particules' audible change is limited to** feedback-limiter curve shape (Sunny slightly slower decay, Scorched softer limiting). Tone voicing is untouched. Note this in the worklog.

## Global Constraints

- Commit messages: short, one sentence, ≤15 words, **no AI attribution / no Co-Authored-By lines**.
- Never touch or flag kMidi code.
- Both test lanes must pass at every commit: `./tests/particules_dsp/run.sh` and `./tests/retours_delay_dsp/run.sh`.
- Test files are picked up by CMake `file(GLOB test_*.cpp)` — new test files need no CMakeLists edit (verify the glob exists in the suite you touch).
- All work in worktree `/Users/gabrielroth/Dev/RobotBoy/.claude/worktrees/retours-quality-degradation` on branch `worktree-retours-quality-degradation`.
- Threshold-tuning rule for the integration tests (Task 3): thresholds below were derived from measured baselines and filter math. During the RED step, record the measured value on unfixed code; during GREEN, record the fixed value. If a threshold sits closer than 1.5 dB to either measured side, move it to the midpoint of the two measurements and note the change in the worklog. Do not weaken a threshold merely to make a failing implementation pass.

---

### Task 1: Shared saturation curves — normalize, soften Scorched, add SaturateWrite

**Files:**
- Modify: `src/particules/dsp/src/fx/saturation.h`
- Modify: `src/particules/dsp/src/fx/saturation.cpp`
- Create: `tests/particules_dsp/test_saturation_curves.cpp`

**Interfaces:**
- Consumes: `particules_dsp::SoftClip(float)` from `src/particules/dsp/src/util/dsp_utils.h` (tanh-like, exact ±1 clamp for |x|≥3).
- Produces: `float Saturation::SaturateWrite(float, QualityMode)`, `StereoFrame Saturation::SaturateWrite(StereoFrame, QualityMode)`, constants `Saturation::kSunnyWriteDrive = 1.4f`, `Saturation::kScorchedWriteDrive = 2.2f`. Task 3 calls the StereoFrame overload from Retours.

- [ ] **Step 1: Write the failing test**

Create `tests/particules_dsp/test_saturation_curves.cpp`:

```cpp
#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include "fx/saturation.h"

using namespace particules_dsp;
using Catch::Approx;

// The old Sunny limiter was AsymmetricSoftClip(input * 0.9f): small-signal
// gain ~0.99 positive / ~0.81 negative, i.e. a hidden ~0.9x level trim that
// made Sunny repeats decay ~0.9 dB/repeat faster than every other mode at
// the same feedback knob. The fix keeps the asymmetric tape character but
// normalizes both branches to unity small-signal slope.
TEST_CASE("Saturation: Sunny feedback limiter has unity small-signal gain") {
    Saturation sat;
    sat.Init();
    for (float x : {0.01f, -0.01f, 0.05f, -0.05f}) {
        float y = sat.LimitFeedback(x, QualityMode::kSunnyTape);
        REQUIRE(y / x == Approx(1.0f).margin(0.03f));
    }
}

TEST_CASE("Saturation: Scorched feedback limiter is soft, not a hard clip") {
    Saturation sat;
    sat.Init();
    // unity small-signal slope
    REQUIRE(sat.LimitFeedback(0.01f, QualityMode::kScorchedCassette) / 0.01f
            == Approx(1.0f).margin(0.03f));
    // A hard clip returns exactly 1.0 at 1.5; tanh returns ~0.93.
    float y = sat.LimitFeedback(1.5f, QualityMode::kScorchedCassette);
    REQUIRE(y < 0.95f);
    REQUIRE(y > 0.7f);
    // still bounded and monotonic
    float prev = -1.1f;
    for (float x = -3.f; x <= 3.f; x += 0.1f) {
        float v = sat.LimitFeedback(x, QualityMode::kScorchedCassette);
        REQUIRE(std::fabs(v) <= 1.0f);
        REQUIRE(v >= prev);
        prev = v;
    }
}

TEST_CASE("Saturation: SaturateWrite ceilings sit below the codec clamp") {
    Saturation sat;
    sat.Init();
    // Digital modes pass through untouched.
    REQUIRE(sat.SaturateWrite(1.7f, QualityMode::kBrightDigital) == 1.7f);
    REQUIRE(sat.SaturateWrite(1.7f, QualityMode::kColdDigital) == 1.7f);
    // Tape modes: unity small-signal slope (quiet repeats decay honestly)...
    for (auto mode : {QualityMode::kSunnyTape, QualityMode::kScorchedCassette}) {
        REQUIRE(sat.SaturateWrite(0.01f, mode) / 0.01f == Approx(1.0f).margin(0.05f));
        REQUIRE(sat.SaturateWrite(-0.01f, mode) / -0.01f == Approx(1.0f).margin(0.05f));
    }
    // ...but hot sums (input + feedback reaches ~2) land well under the
    // storage codec's +/-1 clamp, so the codec never hard-clips tape audio.
    REQUIRE(sat.SaturateWrite(2.0f, QualityMode::kSunnyTape) <= 0.66f);
    REQUIRE(sat.SaturateWrite(-2.0f, QualityMode::kSunnyTape) >= -0.72f);
    REQUIRE(std::fabs(sat.SaturateWrite(2.0f, QualityMode::kScorchedCassette)) <= 0.46f);
    // Sunny is asymmetric: positive peaks saturate earlier (magnetic bias).
    REQUIRE(sat.SaturateWrite(2.0f, QualityMode::kSunnyTape)
            < -sat.SaturateWrite(-2.0f, QualityMode::kSunnyTape));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.claude/worktrees/retours-quality-degradation
./tests/particules_dsp/run.sh
```

Expected: compile error — `SaturateWrite` is not a member of `Saturation`. (If you want to see the curve tests fail too, comment the SaturateWrite test out temporarily and observe "unity small-signal gain" fail at x = −0.01 with ratio ≈0.81 and the Scorched hard-clip test fail with y == 1.0. Not required.)

- [ ] **Step 3: Implement**

`src/particules/dsp/src/fx/saturation.h` — replace the class body:

```cpp
// Soft-clip / tape saturation curves per quality mode.
//
// Quality mode saturation behavior:
//   HiFi:      Hard clip at +/-1.0 (brickwall feedback limiter)
//   Clouds:    Soft clip using tanh-like curve, medium drive
//   CleanLoFi: Medium tape saturation (asymmetric soft clip)
//   Tape:      Soft tanh limiting; deep write-path drive via SaturateWrite
class Saturation {
public:
    void Init();

    // Apply saturation curve based on quality mode
    float Process(float input, QualityMode mode);
    StereoFrame Process(StereoFrame input, QualityMode mode);

    // Feedback limiting per quality mode. Every curve has unity
    // small-signal slope: the limiter bounds the loop without adding or
    // shedding loop gain at low level (a hidden gain trim here skews the
    // mode's decay rate against the others at the same feedback knob).
    float LimitFeedback(float input, QualityMode mode);
    StereoFrame LimitFeedback(StereoFrame input, QualityMode mode);

    // Write-path tape saturation, applied to the input+feedback sum just
    // before it is recorded. Digital modes pass through. Tape modes use
    // SoftClip(drive*x)/drive: unity small-signal slope, ceiling 1/drive —
    // deliberately below the storage codec's +/-1 clamp so accumulated
    // feedback compresses onto a warm tanh ceiling instead of hard-clipping
    // in the codec. Re-recording through this every pass is what makes
    // tape-mode echoes progressively more saturated.
    float SaturateWrite(float input, QualityMode mode);
    StereoFrame SaturateWrite(StereoFrame input, QualityMode mode);

    // Tape write drives (ceiling = 1/drive; Sunny's positive branch gets a
    // further 1.1x for bias asymmetry).
    static constexpr float kSunnyWriteDrive = 1.4f;
    static constexpr float kScorchedWriteDrive = 2.2f;

private:
    // Asymmetric soft clip for tape character: positive peaks saturate
    // earlier (ceiling 1/1.1) than negative (ceiling 1); both branches
    // keep unity small-signal slope.
    static float AsymmetricSoftClip(float x);
    // SoftClip(drive*x)/drive — unity slope at 0, ceiling 1/drive.
    static float NormalizedSoftClip(float x, float drive);
};
```

`src/particules/dsp/src/fx/saturation.cpp` — replace `AsymmetricSoftClip`, the Sunny and Scorched cases of `LimitFeedback(float, QualityMode)`, and append `NormalizedSoftClip` + `SaturateWrite`:

```cpp
// ---------------------------------------------------------------------------
// NormalizedSoftClip: SoftClip(drive*x)/drive. Unity small-signal slope,
// output ceiling 1/drive.
// ---------------------------------------------------------------------------
float Saturation::NormalizedSoftClip(float x, float drive) {
    return SoftClip(x * drive) / drive;
}

// ---------------------------------------------------------------------------
// Asymmetric soft clip for tape character.
// Positive peaks saturate earlier (mimicking magnetic bias asymmetry) but
// both branches keep unity small-signal slope — the old form multiplied the
// negative branch by 0.9 *inside* the tanh with no normalization, which
// acted as a hidden level trim on everything passing through.
// ---------------------------------------------------------------------------
float Saturation::AsymmetricSoftClip(float x) {
    return x >= 0.0f ? NormalizedSoftClip(x, 1.1f) : SoftClip(x);
}
```

In `LimitFeedback(float, QualityMode)`:

```cpp
        case QualityMode::kSunnyTape:
            // Asymmetric tape limiting, unity small-signal gain (the old
            // *0.9f trim made Sunny decay ~0.9 dB/repeat faster than the
            // other modes at the same feedback knob).
            return AsymmetricSoftClip(input);

        case QualityMode::kScorchedCassette:
            // Soft tanh bound — "grungy tape saturation" per the spec, not
            // a hard wall. The write path adds the deep per-pass drive
            // (SaturateWrite); this just bounds the feedback sum warmly.
            return SoftClip(input);
```

Append after the `LimitFeedback` StereoFrame overload:

```cpp
// ---------------------------------------------------------------------------
// SaturateWrite: write-path tape drive (see header). Digital modes pass
// through; tape ceilings (1/1.54 and 1/1.4 for Sunny +/-, 1/2.2 Scorched)
// stay below the storage codec's +/-1 clamp on purpose.
// ---------------------------------------------------------------------------
float Saturation::SaturateWrite(float input, QualityMode mode) {
    switch (mode) {
        case QualityMode::kBrightDigital:
        case QualityMode::kColdDigital:
            return input;
        case QualityMode::kSunnyTape:
            return input >= 0.0f
                       ? NormalizedSoftClip(input, kSunnyWriteDrive * 1.1f)
                       : NormalizedSoftClip(input, kSunnyWriteDrive);
        case QualityMode::kScorchedCassette:
            return NormalizedSoftClip(input, kScorchedWriteDrive);
    }
    return input;
}

StereoFrame Saturation::SaturateWrite(StereoFrame input, QualityMode mode) {
    return {
        SaturateWrite(input.l, mode),
        SaturateWrite(input.r, mode)
    };
}
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
./tests/particules_dsp/run.sh
```

Expected: all tests pass (new file included via the suite's `file(GLOB test_*.cpp)`).

- [ ] **Step 5: Run the Retours suite too (shared code changed)**

```bash
./tests/retours_delay_dsp/run.sh
```

Expected: all pass. (Retours picks up the new LimitFeedback curves; its existing tests assert bounds, not exact curves.)

- [ ] **Step 6: Commit**

```bash
git add src/particules/dsp/src/fx/saturation.h src/particules/dsp/src/fx/saturation.cpp tests/particules_dsp/test_saturation_curves.cpp
git commit -m "Saturation: unity-gain limiter curves, soft Scorched bound, add SaturateWrite"
```

---

### Task 2: QualityProcessor per-instance tape tone cutoffs

**Files:**
- Modify: `src/particules/dsp/src/quality/quality_processor.h`
- Modify: `src/particules/dsp/src/quality/quality_processor.cpp`
- Create: `tests/particules_dsp/test_tape_voicing.cpp`

**Interfaces:**
- Produces: `void QualityProcessor::SetTapeToneCutoffs(float sunny_output_hz, float scorched_output_hz)` — stores per-instance output-LP cutoffs for the two tape modes; call after `Init()`, before processing. Defaults (set in `Init()`) remain `kSunnyTapeOutputLpHz` = 10 kHz and `kScorchedOutputLpHz` = 5 kHz so Particules is unaffected. Task 3 calls this from Retours' `Init`.

- [ ] **Step 1: Write the failing test**

Create `tests/particules_dsp/test_tape_voicing.cpp`:

```cpp
#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include <vector>
#include "quality/quality_processor.h"
#include "util/dsp_utils.h"

using namespace particules_dsp;

namespace {
// Steady-state amplitude of a 4 kHz sine after ProcessOutput in the given
// mode. Skips the 64-sample mode crossfade plus filter settle, then
// measures peak over one second.
float SteadyAmp4k(QualityProcessor& qp, QualityMode mode) {
    const float sr = 48000.0f;
    float peak = 0.0f;
    for (int i = 0; i < 52800; ++i) {
        float s = std::sin(kTwoPi * 4000.0f * static_cast<float>(i) / sr);
        StereoFrame out = qp.ProcessOutput({s, s}, mode);
        if (i >= 4800) peak = std::max(peak, std::fabs(out.l));
    }
    return peak;
}
} // namespace

TEST_CASE("QualityProcessor: tape tone cutoff override darkens Scorched output") {
    QualityProcessor stock;
    stock.Init(48000.0f);
    float amp_stock = SteadyAmp4k(stock, QualityMode::kScorchedCassette);

    QualityProcessor voiced;
    voiced.Init(48000.0f);
    voiced.SetTapeToneCutoffs(6500.0f, 2800.0f);
    float amp_voiced = SteadyAmp4k(voiced, QualityMode::kScorchedCassette);

    // 2-pole Butterworth at 4 kHz: fc=5 kHz -> ~-1.5 dB, fc=2.8 kHz ->
    // ~-7.1 dB. Require at least 4 dB extra attenuation from the override.
    float extra_db = 20.0f * std::log10(amp_stock / amp_voiced);
    REQUIRE(extra_db > 4.0f);

    // Defaults unchanged: a second stock instance matches the first.
    QualityProcessor stock2;
    stock2.Init(48000.0f);
    REQUIRE(SteadyAmp4k(stock2, QualityMode::kScorchedCassette)
            == Catch::Approx(amp_stock).epsilon(0.01));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
./tests/particules_dsp/run.sh
```

Expected: compile error — `SetTapeToneCutoffs` is not a member of `QualityProcessor`.

- [ ] **Step 3: Implement**

`src/particules/dsp/src/quality/quality_processor.h`:

Add to the public section (after `GetPitchModulation`):

```cpp
    // Override the tape modes' OUTPUT tone-LP cutoffs (Hz). Defaults (set
    // in Init) are kSunnyTapeOutputLpHz / kScorchedOutputLpHz, which suit
    // Particules' one-pass grain path. A feedback delay re-applies the
    // output LP every round trip, so Retours voices these lower. Call
    // after Init() and before processing: values take effect at the next
    // output-mode change (fresh instances always see one, since
    // prev_output_mode_ starts at kBrightDigital which has no output LP).
    void SetTapeToneCutoffs(float sunny_output_hz, float scorched_output_hz);
```

Add to the private section (after `current_output_cutoff_hz_`):

```cpp
    // Per-instance tape output-LP voicing (see SetTapeToneCutoffs).
    float sunny_output_hz_ = kSunnyTapeOutputLpHz;
    float scorched_output_hz_ = kScorchedOutputLpHz;

    // Output LP cutoff for a mode, honoring the per-instance voicing.
    float OutputCutoffForMode(QualityMode mode) const;
```

Note: the constants `kSunnyTapeOutputLpHz`/`kScorchedOutputLpHz` are declared in a `public:` block *below* the first `private:` block in this header — the default member initializers above compile because the constants are members of the same class. Keep the new fields in the first private block (with the other filter state).

`src/particules/dsp/src/quality/quality_processor.cpp`:

1. Delete the free function `static float OutputCutoffForMode(QualityMode mode)` (lines 54-60) and add the member version in its place:

```cpp
float QualityProcessor::OutputCutoffForMode(QualityMode mode) const {
    switch (mode) {
        case QualityMode::kSunnyTape:        return sunny_output_hz_;
        case QualityMode::kScorchedCassette: return scorched_output_hz_;
        default:                             return sunny_output_hz_;
    }
}

void QualityProcessor::SetTapeToneCutoffs(float sunny_output_hz, float scorched_output_hz) {
    sunny_output_hz_ = sunny_output_hz;
    scorched_output_hz_ = scorched_output_hz;
}
```

2. In `Init()`, reset the voicing to defaults before the filter setup lines that reference cutoffs (insert right after `sample_rate_ = sample_rate;`):

```cpp
    sunny_output_hz_ = kSunnyTapeOutputLpHz;
    scorched_output_hz_ = kScorchedOutputLpHz;
```

3. Still in `Init()`, replace the two output-filter default-cutoff lines and the cached-cutoff line to go through the fields:

```cpp
    output_lp_l_.SetFrequencyHz(sunny_output_hz_, sample_rate_);
    output_lp_r_.SetFrequencyHz(sunny_output_hz_, sample_rate_);
```

and at the bottom of `Init()`:

```cpp
    current_output_cutoff_hz_ = sunny_output_hz_;
```

(`ProcessOutput`'s existing call `OutputCutoffForMode(mode)` now resolves to the member function; no change needed there. `InputCutoffForMode` stays a static free function — input LPs are anti-alias filters pinned to the decimated Nyquist, not voicing.)

- [ ] **Step 4: Run tests to verify they pass**

```bash
./tests/particules_dsp/run.sh
```

Expected: all pass.

- [ ] **Step 5: Run the Retours suite (shared header changed)**

```bash
./tests/retours_delay_dsp/run.sh
```

Expected: all pass (Retours doesn't call the setter yet; defaults preserve behavior).

- [ ] **Step 6: Commit**

```bash
git add src/particules/dsp/src/quality/quality_processor.h src/particules/dsp/src/quality/quality_processor.cpp tests/particules_dsp/test_tape_voicing.cpp
git commit -m "QualityProcessor: per-instance tape output-LP cutoff voicing"
```

---

### Task 3: Wire Retours — write saturation + tape voicing + integration tests

**Files:**
- Modify: `src/retours_delay/dsp/src/retours_processor.cpp`
- Create: `tests/retours_delay_dsp/test_quality_degradation.cpp`

**Interfaces:**
- Consumes: `Saturation::SaturateWrite(StereoFrame, QualityMode)` (Task 1), `QualityProcessor::SetTapeToneCutoffs(float, float)` (Task 2).
- Produces: nothing consumed later; final DSP behavior.

- [ ] **Step 1: Write the failing tests**

Create `tests/retours_delay_dsp/test_quality_degradation.cpp`:

```cpp
#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include "retours_delay_dsp/retours_dsp.h"
#include "util/dsp_utils.h"

using namespace retours_delay_dsp;

// Integration coverage for the per-quality feedback degradation character
// (2026-07 fix round): tape modes must darken repeats measurably and
// accumulate onto a soft saturation ceiling instead of the storage codec's
// hard +/-1 clamp, and Sunny's decay rate must match Bright's at the same
// feedback knob (the old limiter hid a ~0.9x level trim).
namespace {

constexpr float kSr = 48000.f;
constexpr float kDelayS = 0.25f;

struct Proc {
    void* mem = nullptr;
    RetoursProcessor p;
    explicit Proc(float sr = kSr) {
        auto req = RetoursProcessor::GetMemoryRequirements(sr);
        posix_memalign(&mem, req.alignment, req.total_bytes);
        p.Init(mem, req.total_bytes, sr);
    }
    ~Proc() { std::free(mem); }
};

// Manual-mode density knob for a target delay given the mode's effective
// buffer duration (Bright 4 s, Cold 8 s, Sunny 16 s, Scorched 32 s stereo).
float KnobForSeconds(float seconds, float buffer_seconds) {
    float d = -std::log2(seconds / buffer_seconds) / 11.0f;  // kManualOctaves
    return 0.5f - 0.5f * d;
}

double Goertzel(const std::vector<StereoFrame>& v, size_t from, size_t to,
                double freq_hz) {
    double omega = 2.0 * 3.14159265358979 * freq_hz / kSr;
    double sc = 0.0, ss = 0.0;
    for (size_t i = from; i < to; ++i) {
        double a = omega * static_cast<double>(i);
        sc += static_cast<double>(v[i].l) * std::cos(a);
        ss += static_cast<double>(v[i].l) * std::sin(a);
    }
    size_t n = to - from;
    return n ? 2.0 * std::sqrt(sc * sc + ss * ss) / static_cast<double>(n) : 0.0;
}

double Db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// Render a 100 ms, 220 Hz sawtooth burst (amplitude 0.6 = 3 Vpk) into a
// 250 ms wet-only delay at feedback knob 0.75, after a 2.5 s settle that
// covers the quality transition and delay slew. Repeat k then occupies the
// window starting k*250 ms after the burst.
struct BurstRender {
    std::vector<StereoFrame> out;
    size_t settle;
    size_t burst;
};

BurstRender RenderBurst(QualityMode mode, float buffer_seconds) {
    const size_t settle_n = static_cast<size_t>(2.5f * kSr);
    const size_t burst_n = static_cast<size_t>(0.10f * kSr);
    const int n_windows = 13;
    const size_t total_n =
        settle_n + burst_n +
        static_cast<size_t>((n_windows + 1) * kDelayS * kSr);

    Proc proc;
    RetoursParameters p;
    p.dry_wet = 1.f;
    p.feedback = 0.75f;
    p.time = 0.f;
    p.density = KnobForSeconds(kDelayS, buffer_seconds);
    p.quality = mode;
    proc.p.SetParameters(p);

    std::vector<StereoFrame> in(total_n, StereoFrame{0.f, 0.f});
    const size_t fade = static_cast<size_t>(0.005f * kSr);
    for (size_t i = 0; i < burst_n; ++i) {
        double ph = std::fmod(220.0 * static_cast<double>(i) / kSr, 1.0);
        float s = 0.6f * static_cast<float>(2.0 * ph - 1.0);
        float g = 1.f;
        if (i < fade) g = static_cast<float>(i) / static_cast<float>(fade);
        if (burst_n - i < fade)
            g = static_cast<float>(burst_n - i) / static_cast<float>(fade);
        in[settle_n + i] = {s * g, s * g};
    }
    BurstRender r;
    r.out.resize(total_n);
    r.settle = settle_n;
    r.burst = burst_n;
    proc.p.Process(in.data(), r.out.data(), total_n);
    return r;
}

// Goertzel magnitude of freq inside repeat window k (1-based).
double RepeatMag(const BurstRender& r, int k, double freq) {
    size_t w0 = r.settle +
                static_cast<size_t>(k * kDelayS * kSr) +
                static_cast<size_t>(0.005f * kSr);
    size_t w1 = w0 + r.burst - static_cast<size_t>(0.01f * kSr);
    return Goertzel(r.out, w0, w1, freq);
}

// Continuous 220 Hz saw (amplitude 0.5) for 10 s, wet-only, feedback 0.75;
// stats over the last 2 s where the loop has settled.
struct SteadyStats {
    double clip_frac;
    double peak;
};

SteadyStats RenderSteady(QualityMode mode, float buffer_seconds) {
    const size_t settle_n = static_cast<size_t>(2.5f * kSr);
    const size_t play_n = static_cast<size_t>(10.f * kSr);
    const size_t total_n = settle_n + play_n;

    Proc proc;
    RetoursParameters p;
    p.dry_wet = 1.f;
    p.feedback = 0.75f;
    p.time = 0.f;
    p.density = KnobForSeconds(kDelayS, buffer_seconds);
    p.quality = mode;
    proc.p.SetParameters(p);

    std::vector<StereoFrame> in(total_n, StereoFrame{0.f, 0.f});
    for (size_t i = settle_n; i < total_n; ++i) {
        double t = static_cast<double>(i - settle_n) / kSr;
        double ph = std::fmod(220.0 * t, 1.0);
        float s = 0.5f * static_cast<float>(2.0 * ph - 1.0);
        in[i] = {s, s};
    }
    std::vector<StereoFrame> out(total_n);
    proc.p.Process(in.data(), out.data(), total_n);

    size_t w0 = total_n - static_cast<size_t>(2.f * kSr);
    SteadyStats st{0.0, 0.0};
    size_t clip = 0;
    for (size_t i = w0; i < total_n; ++i) {
        double a = std::fabs(static_cast<double>(out[i].l));
        st.peak = std::max(st.peak, a);
        if (a > 0.985) ++clip;
    }
    st.clip_frac = static_cast<double>(clip) / static_cast<double>(total_n - w0);
    return st;
}

} // namespace

// -----------------------------------------------------------------------
// (A1) Scorched repeats darken fast in the presence band. Differential =
// extra HF (3.3 kHz) loss between repeats 1 and 4 beyond the level decay
// (measured at 440 Hz). With the 2.8 kHz tone LP this is ~-14 dB over the
// three passes; the old 5 kHz voicing gave only ~-2.3 dB.
// -----------------------------------------------------------------------
TEST_CASE("degradation: Scorched repeats darken fast in the presence band") {
    auto r = RenderBurst(QualityMode::kScorchedCassette, 32.f);
    double hf_drop = Db(RepeatMag(r, 1, 3300.0)) - Db(RepeatMag(r, 4, 3300.0));
    double lf_drop = Db(RepeatMag(r, 1, 440.0)) - Db(RepeatMag(r, 4, 440.0));
    REQUIRE(hf_drop - lf_drop > 8.0);
}

// -----------------------------------------------------------------------
// (A2) Sunny's very first repeat is measurably mellower than Bright's:
// spectral tilt (7.92 kHz vs 440 Hz) gap of ~8 dB with the 6.5 kHz
// voicing vs ~4 dB with the old 10 kHz voicing (decimation accounts for
// the baseline gap).
// -----------------------------------------------------------------------
TEST_CASE("degradation: Sunny first repeat is mellower than Bright's") {
    auto sunny = RenderBurst(QualityMode::kSunnyTape, 16.f);
    auto bright = RenderBurst(QualityMode::kBrightDigital, 4.f);
    double tilt_sunny =
        Db(RepeatMag(sunny, 1, 7920.0)) - Db(RepeatMag(sunny, 1, 440.0));
    double tilt_bright =
        Db(RepeatMag(bright, 1, 7920.0)) - Db(RepeatMag(bright, 1, 440.0));
    REQUIRE(tilt_bright - tilt_sunny > 6.0);
}

// -----------------------------------------------------------------------
// (B) Tape modes accumulate onto the soft write-saturation ceiling
// (0.65/0.71 Sunny, 0.45 Scorched), never the codec's hard +/-1 clamp.
// Pre-fix these measured 12.4% (Sunny) and 5.5% (Scorched) of steady-state
// samples pinned above 0.985. Bright must still brickwall by design.
// -----------------------------------------------------------------------
TEST_CASE("degradation: tape modes settle on a soft ceiling, not the codec clamp") {
    auto sunny = RenderSteady(QualityMode::kSunnyTape, 16.f);
    REQUIRE(sunny.clip_frac < 0.005);
    REQUIRE(sunny.peak < 0.9);

    auto scorched = RenderSteady(QualityMode::kScorchedCassette, 32.f);
    REQUIRE(scorched.clip_frac < 0.005);
    REQUIRE(scorched.peak < 0.9);

    auto bright = RenderSteady(QualityMode::kBrightDigital, 4.f);
    REQUIRE(bright.clip_frac > 0.3);
}

// -----------------------------------------------------------------------
// (C) Sunny's decay rate matches Bright's at the same feedback knob.
// Measured over repeats 8..12 (below the saturation knee). Pre-fix the
// hidden 0.9x limiter trim made Sunny ~0.9 dB/repeat faster. This may
// already pass once Task 1's shared curve fix lands — it pins that fix at
// the integration level.
// -----------------------------------------------------------------------
TEST_CASE("degradation: Sunny decay rate matches Bright at the same knob") {
    auto sunny = RenderBurst(QualityMode::kSunnyTape, 16.f);
    auto bright = RenderBurst(QualityMode::kBrightDigital, 4.f);
    auto slope = [](const BurstRender& r) {
        return (Db(RepeatMag(r, 8, 440.0)) - Db(RepeatMag(r, 12, 440.0))) / 4.0;
    };
    REQUIRE(std::fabs(slope(sunny) - slope(bright)) < 0.5);
}
```

- [ ] **Step 2: Run tests to verify the new ones fail (record values)**

```bash
./tests/retours_delay_dsp/run.sh 2>&1 | tee /tmp/red_run.txt
```

Expected: A1, A2, B fail (Retours isn't wired yet — LimitFeedback improvements from Task 1 alone don't lower cutoffs or add write saturation). C may already pass (Task 1's shared fix). **Record the measured values from the failure output in the worklog** (Global Constraints threshold rule).

- [ ] **Step 3: Wire Retours**

`src/retours_delay/dsp/src/retours_processor.cpp`:

1. Add after the `kBufferSeconds` constant near the top:

```cpp
// Retours tape-tone voicing: a feedback delay re-applies the tape output
// LP every round trip, so the shared defaults that suit Particules'
// one-pass grain path (10 kHz Sunny / 5 kHz Scorched) barely darken
// repeats below ~4 kHz. Voice them lower here: Sunny mellows gradually,
// Scorched murks within a few repeats.
static constexpr float kSunnyToneCutoffHz = 6500.0f;
static constexpr float kScorchedToneCutoffHz = 2800.0f;
```

2. In `Init()`, right after `impl_->quality_processor.Init(sample_rate);`:

```cpp
    impl_->quality_processor.SetTapeToneCutoffs(kSunnyToneCutoffHz,
                                                kScorchedToneCutoffHz);
```

3. In `ProcessBlock()`, immediately after the `to_write = s.quality_processor.ProcessInput(...)` statement:

```cpp
        // Tape write saturation: compress the input+feedback sum onto a
        // soft ceiling below the storage codec's hard +/-1 clamp, so
        // accumulated feedback lands on warm tanh compression instead of
        // digital clipping. Re-recording through this each pass is what
        // makes tape echoes progressively more saturated.
        to_write = s.saturation.SaturateWrite(to_write, s.active_quality);
```

- [ ] **Step 4: Run tests to verify they pass (record values)**

```bash
./tests/retours_delay_dsp/run.sh
```

Expected: all pass, including the four new tests. To record GREEN-side measured values for the worklog, temporarily print them or compute from a quick probe run in Task 4 (the probe prints full tables; that satisfies the recording requirement).

- [ ] **Step 5: Run the Particules suite (no shared changes here, sanity)**

```bash
./tests/particules_dsp/run.sh
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/retours_delay/dsp/src/retours_processor.cpp tests/retours_delay_dsp/test_quality_degradation.cpp
git commit -m "Retours: tape write saturation and lower tape tone voicing"
```

---

### Task 4: Validation probes, VCV build check, worklog + user checklist

**Files:**
- Create: `docs/superpowers/plans/2026-07-21-retours-quality-degradation-worklog.md`
- No source changes expected (tuning latitude below only if a probe goal fails).

**Interfaces:**
- Consumes: the finished DSP from Tasks 1-3; probe sources from the session scratchpad (recreate from the code below if missing).

- [ ] **Step 1: Build the probes against the worktree**

The two probe programs from the investigation live at
`/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/65fdaff5-64f1-4bad-8938-b9afaf649533/scratchpad/quality_probe.cpp` (burst, per-repeat table) and `.../steady_probe.cpp` (continuous, steady-state stats + WAVs). Copy them into the scratchpad if present, else skip to using the test output values. Build against the worktree's test library:

```bash
WT=/Users/gabrielroth/Dev/RobotBoy/.claude/worktrees/retours-quality-degradation
cd $WT && ./tests/retours_delay_dsp/run.sh   # ensures librobotboy_retours_delay_dsp.a is fresh
SCRATCH=/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/65fdaff5-64f1-4bad-8938-b9afaf649533/scratchpad
for p in quality_probe steady_probe; do
  c++ -std=c++17 -O2 $SCRATCH/$p.cpp \
    -I$WT/src/retours_delay/dsp/include -I$WT/src/retours_delay/dsp/src \
    -I$WT/src/particules/dsp/include -I$WT/src/particules/dsp/src \
    $WT/tests/retours_delay_dsp/build/librobotboy_retours_delay_dsp.a -o $SCRATCH/$p-fixed
done
$SCRATCH/quality_probe-fixed
$SCRATCH/steady_probe-fixed
```

- [ ] **Step 2: Check the probe output against these goals**

1. Scorched burst table: HF-LF column drops by >10 dB between repeat 1 and repeat 4 beyond the Bright baseline (which stays flat).
2. Sunny burst table: RMS decay per repeat within 0.5 dB of Bright's over repeats 8-12.
3. Steady-state: Sunny and Scorched clip% == 0.0 (or <0.5%), peaks ≤0.8; Bright clip% still >30%.
4. Level sanity: Scorched steady-state RMS within 12 dB of Bright's (murk may be quieter than a clipped wall — that is fine — but not vanishing).

**Tuning latitude (only if a goal fails):** `kScorchedWriteDrive` ∈ [1.8, 2.5], `kSunnyWriteDrive` ∈ [1.2, 1.6], `kScorchedToneCutoffHz` ∈ [2500, 3500], `kSunnyToneCutoffHz` ∈ [6000, 7500]. If you adjust a constant, update any test threshold whose arithmetic depended on it (the comments in the tests show the derivations), re-run both suites, and document the change + reason in the worklog. One tuning iteration expected at most.

- [ ] **Step 3: VCV plugin compile check**

```bash
make -C vcv -j4 2>&1 | tail -5
```

Expected: clean build (dylib links). If the build needs environment (Rack SDK path), consult the `build-robotboy-plugin` skill. Do not install; compile check only.

- [ ] **Step 4: Write the worklog**

Create `docs/superpowers/plans/2026-07-21-retours-quality-degradation-worklog.md` containing:

- Summary of the three fixes and the design decisions (copy from this plan's "Design decisions" section, noting Particules' feedback-limiter curve change).
- The RED-side measured values recorded in Task 3 Step 2 and the GREEN-side probe tables from Step 1 here (burst per-repeat table + steady-state stats, before/after where available — the "before" numbers are in this plan's Investigation background section).
- Any tuning-latitude changes made, with reasons.
- The user listening checklist below, verbatim (per project convention, GUI/listening verification is user-run):

```markdown
## User listening checklist (VCV Rack, e.g. test patch ~/Desktop/test-patches/13.vcv)

Set Feedback to ~75%, delay time ~250-500 ms, and compare Quality modes:

- [ ] **Bright digital:** repeats stack clean; at high feedback the wall
      brickwalls but stays bright (no darkening).
- [ ] **Sunny tape:** each repeat audibly mellower; decay length now matches
      Bright at the same knob (it used to die ~35% faster); high feedback
      compresses warmly, no digital buzz.
- [ ] **Scorched cassette:** repeats fall into dark murk within 3-4 passes;
      wow/flutter warble in the tail; high feedback is a warm, dense wall,
      noticeably quieter and rounder than Bright's clipped wall.
- [ ] **Cold digital:** unchanged character (12-bit, 10 kHz), still crunches
      digitally at high feedback (intended).
- [ ] Quality switching mid-feedback still fades/clears cleanly (no pops).
- [ ] Freeze + quality interactions unchanged (freeze, switch quality,
      unfreeze — no corruption).
```

- [ ] **Step 5: Final test sweep and commit**

```bash
./tests/particules_dsp/run.sh && ./tests/retours_delay_dsp/run.sh
git add docs/superpowers/plans/2026-07-21-retours-quality-degradation-worklog.md
git commit -m "Retours: quality degradation worklog and listening checklist"
```

---

## Self-review notes

- Spec coverage: per-mode saturation below codec clamp (Tasks 1+3), tape LP retune (Tasks 2+3), Sunny 0.9 trim (Task 1, pinned in Task 3 test C), Bright/Cold unchanged (Task 1 pass-through + test B Bright sanity). ✓
- Type consistency: `SaturateWrite(StereoFrame, QualityMode)` produced in Task 1, consumed in Task 3; `SetTapeToneCutoffs(float, float)` produced in Task 2, consumed in Task 3. ✓
- Known soft spots called out inline: integration-test thresholds are derived, not measured post-fix — the Global Constraints threshold rule plus Task 3's record-values steps handle drift.
