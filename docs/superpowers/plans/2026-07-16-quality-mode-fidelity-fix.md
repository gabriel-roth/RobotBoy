# Quality-mode fidelity fix — implementation plan

> **For agentic workers:** execute task-by-task; each task ends at a green build/test and a commit.

**Goal:** Fix the inverted decimation ladder for the four quality modes so degradation increases monotonically Bright→Scorched (matching the Beads manual), keep each mode correctly anti-aliased, add gentle wow to Sunny tape, and rename the enum members to the Beads labels — across Particules and Retours.

**Architecture:** Shared `particules_dsp` quality engine + `DecimationFactorForQuality`; `retours_delay_dsp` aliases `QualityMode` and calls the same decimation function. Changes are: (1) enum rename, (2) decimation swap, (3) filter constant split/re-tune, (4) per-mode wow depth.

**Tech Stack:** C++20, Catch2 unit tests (CMake per-module test dirs), VCV Rack SDK build (`make -C vcv`).

## Global Constraints

- Enum **values** stay 0/1/2/3 and in order (serialized `qualityState` must remain valid).
- UI labels and RGB colors do not change.
- Keep the integer doubling ladder (no attempt at Beads' literal 48/32/24/24 kHz).
- Word-boundary-safe rename: `\bkTape\b` etc. must NOT touch constant prefixes like `kTapeLpHz`, `kCloudsInputLpHz`, `kCleanLoFiInputLpHz` (those are renamed explicitly in Task 3).

---

### Task 1: Rename enum members (mechanical, no behavior change)

**Files:** all 21 files matching the old names under `src/` and `tests/` (from `grep -rlE 'kHiFi|kClouds|kCleanLoFi|kTape'`).

**Rename map (whole-word only):** `kHiFi→kBrightDigital`, `kClouds→kColdDigital`, `kCleanLoFi→kSunnyTape`, `kTape→kScorchedCassette`.

- [ ] **Step 1:** Apply word-boundary rename across source + tests:
```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/new-modules
FILES=$(grep -rlE '\bk(HiFi|Clouds|CleanLoFi|Tape)\b' src tests | grep -vE '/build/|\.o$')
perl -pi -e 's/\bkHiFi\b/kBrightDigital/g; s/\bkClouds\b/kColdDigital/g; s/\bkCleanLoFi\b/kSunnyTape/g; s/\bkTape\b/kScorchedCassette/g' $FILES
```
- [ ] **Step 2:** Verify constant prefixes were NOT renamed (must still exist):
```bash
grep -rnE 'kTapeLpHz|kTapeHissLevel|kCloudsInputLpHz|kCleanLoFiInputLpHz|kCleanLoFiLpHz' src | wc -l   # expect >0
grep -rnE '\bkHiFi\b|\bkClouds\b|\bkCleanLoFi\b|\bkTape\b' src tests | grep -vE '/build/'             # expect empty
```
- [ ] **Step 3:** Build both DSP test suites (see Task 6 build commands) — expect compile success (constants named kCleanLoFi* still resolve; enum members renamed consistently).
- [ ] **Step 4:** Commit: `git commit -am "Quality: rename enum members to Beads labels (Bright/Cold/Sunny/Scorched)"`

---

### Task 2: Swap the decimation ladder

**Files:** Modify `src/particules/dsp/include/particules_dsp/types.h` (`DecimationFactorForQuality` + duration comment).

- [ ] **Step 1:** In `DecimationFactorForQuality`, swap the Sunny/Scorched returns:
```cpp
        case QualityMode::kBrightDigital:    return 1;
        case QualityMode::kColdDigital:      return 2;
        case QualityMode::kSunnyTape:        return 4;   // was 8
        case QualityMode::kScorchedCassette: return 8;   // was 4
```
- [ ] **Step 2:** Update the duration comment (was "HiFi 4s, Clouds 8s, Tape 16s, LoFi 32s"):
```cpp
// Decimation extends effective duration: Bright 4s, Cold 8s, Sunny 16s, Scorched 32s.
```
- [ ] **Step 3:** Verify retours test (a) `kColdDigital doubles effective delay` still passes (kColdDigital is still 2×, unaffected) and the whole retours suite builds/passes (Task 6).
- [ ] **Step 4:** Commit: `git commit -am "Quality: fix inverted decimation ladder -> 1/2/4/8 (Scorched now 32s)"`

---

### Task 3: Split and re-tune anti-aliasing filters

**Files:** Modify `src/particules/dsp/src/quality/quality_processor.h` (constants) and `.cpp` (`InputCutoffForMode`, `OutputCutoffForMode`, the kSunnyTape input comment), plus `tests/particules_dsp/test_quality_modes.cpp` assertions.

- [ ] **Step 1:** In `quality_processor.h`, replace the four constants (lines ~65-68) with split, renamed constants:
```cpp
    static constexpr float kColdDigitalInputLpHz = 10000.0f;  // was kCloudsInputLpHz
    static constexpr float kSunnyTapeInputLpHz   = 5000.0f;   // 4x -> Nyquist 6k (was 2500 @ 8x)
    static constexpr float kSunnyTapeOutputLpHz  = 10000.0f;  // was kCleanLoFiLpHz (tone)
    static constexpr float kScorchedInputLpHz    = 2500.0f;   // 8x -> Nyquist 3k anti-alias (was kTapeLpHz 5000)
    static constexpr float kScorchedOutputLpHz   = 5000.0f;   // cassette tone (was kTapeLpHz)
```
- [ ] **Step 2:** Update `InputCutoffForMode` in `.cpp`:
```cpp
static float InputCutoffForMode(QualityMode mode) {
    switch (mode) {
        case QualityMode::kColdDigital:      return QualityProcessor::kColdDigitalInputLpHz;
        case QualityMode::kSunnyTape:        return QualityProcessor::kSunnyTapeInputLpHz;
        case QualityMode::kScorchedCassette: return QualityProcessor::kScorchedInputLpHz;
        default:                             return QualityProcessor::kColdDigitalInputLpHz;
    }
}
```
- [ ] **Step 3:** Update `OutputCutoffForMode` in `.cpp`:
```cpp
static float OutputCutoffForMode(QualityMode mode) {
    switch (mode) {
        case QualityMode::kSunnyTape:        return QualityProcessor::kSunnyTapeOutputLpHz;
        case QualityMode::kScorchedCassette: return QualityProcessor::kScorchedOutputLpHz;
        default:                             return QualityProcessor::kSunnyTapeOutputLpHz;
    }
}
```
- [ ] **Step 4:** Update the `kSunnyTape` input comment in `ProcessInput` (was "Anti-aliasing LP for 8x decimation (effective Nyquist = 3 kHz)"):
```cpp
        case QualityMode::kSunnyTape:
            // Anti-aliasing LP for 4x decimation (effective Nyquist = 6 kHz)
            result = { filtered_l, filtered_r };
            break;
```
- [ ] **Step 5:** Update the two decimation-tagged filter tests in `tests/particules_dsp/test_quality_modes.cpp` to the new cutoffs. The "LoFi input LP attenuates above 2.5kHz" test now targets Sunny tape at **5 kHz**: change the probe tone from 4000 Hz to **9000 Hz** and keep the `< 0.3f` assertion; rename its title to `Sunny tape input LP attenuates above 5kHz`. The "Tape input LP attenuates above 5kHz" test now targets Scorched at **2.5 kHz**: change the probe tone from 15000 Hz to **8000 Hz**, keep `< 0.3f`, rename title to `Scorched input LP attenuates above 2.5kHz`, and fix the comment. Also update the `CleanLoFi output LP` test title/comment to `Sunny tape output LP` (behavior unchanged — output stays 10 kHz).
- [ ] **Step 6:** Build + run particules test suite (Task 6). Expect pass.
- [ ] **Step 7:** Commit: `git commit -am "Quality: split + re-tune anti-alias filters for new decimation rates"`

---

### Task 4: Gentle wow on Sunny tape

**Files:** Modify `src/particules/dsp/src/quality/quality_processor.cpp` (`GetPitchModulation` + a `WowDepthForMode` helper), add tests to both `test_quality_modes.cpp` files.

- [ ] **Step 1:** Add a file-scope helper near the other static helpers in `.cpp`:
```cpp
// Wow/flutter depth by mode: full on Scorched cassette, half on Sunny tape
// (both are tape emulations), none elsewhere.
static float WowDepthForMode(QualityMode mode) {
    switch (mode) {
        case QualityMode::kScorchedCassette: return 1.0f;
        case QualityMode::kSunnyTape:        return 0.5f;
        default:                             return 0.0f;
    }
}
```
- [ ] **Step 2:** Rewrite `GetPitchModulation` to use the depth:
```cpp
float QualityProcessor::GetPitchModulation(QualityMode mode, size_t num_samples) {
    float depth = WowDepthForMode(mode);
    if (depth == 0.0f) {
        return 1.0f;
    }
    float advance = static_cast<float>(num_samples);
    wow_phase_ += wow_increment_ * advance;
    while (wow_phase_ >= 1.0f) wow_phase_ -= 1.0f;
    flutter_phase_ += flutter_increment_ * advance;
    while (flutter_phase_ >= 1.0f) flutter_phase_ -= 1.0f;
    float wow_st     = kWowSemitones     * std::sin(wow_phase_     * kTwoPi);
    float flutter_st = kFlutterSemitones * std::sin(flutter_phase_ * kTwoPi);
    return SemitonesToRatio(depth * (wow_st + flutter_st));
}
```
- [ ] **Step 3:** Add a particules test asserting Sunny tape modulates but at less depth than Scorched. Append to `tests/particules_dsp/test_quality_modes.cpp`:
```cpp
TEST_CASE("QualityModes: Sunny tape wow is present but gentler than Scorched", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);
    auto span = [&](QualityMode m) {
        float lo = 2.0f, hi = 0.0f;
        for (int i = 0; i < 96000; ++i) {
            float r = qp.GetPitchModulation(m, 1);
            lo = std::min(lo, r); hi = std::max(hi, r);
        }
        return hi - lo;
    };
    float sunny = span(QualityMode::kSunnyTape);
    float scorched = span(QualityMode::kScorchedCassette);
    REQUIRE(sunny > 0.0005f);            // present
    REQUIRE(sunny < scorched * 0.75f);   // gentler than full-depth Scorched
}
```
- [ ] **Step 4:** Build + run particules test suite (Task 6). Expect pass.
- [ ] **Step 5:** Commit: `git commit -am "Quality: add gentle (half-depth) wow/flutter to Sunny tape"`

---

### Task 5: CHANGELOG

**Files:** Modify `CHANGELOG.md`.

- [ ] **Step 1:** Add an entry under the unreleased/appropriate section noting: fixed inverted quality-mode decimation ladder (Sunny tape now 16 s, Scorched cassette now 32 s), re-tuned anti-alias filters, added gentle wow to Sunny tape, renamed internal quality enum. Match the file's existing heading style (read it first).
- [ ] **Step 2:** Commit: `git commit -am "docs: CHANGELOG for quality-mode fidelity fix"`

---

### Task 6: Build, test, verify, install

Test build/run commands (both suites use CMake under `tests/<module>/`):
```bash
cd tests/particules_dsp && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j4 && ./build/particules_dsp_tests
cd ../retours_delay_dsp && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j4 && ./build/retours_delay_dsp_tests
```
(If the exact target/binary names differ, read each `tests/<module>/CMakeLists.txt` and adjust.)

- [ ] **Step 1:** Run both suites — all pass.
- [ ] **Step 2:** Headless WAV sanity (per `test-vcv-module-headless` skill): render a bright broadband input through Scorched cassette; confirm no strong tone above ~3 kHz (no aliasing fold) and that Sunny tape passes more highs than the old 8× version. Record findings; this is a sanity check, not a gating unit test.
- [ ] **Step 3:** Build + install the VCV plugin: `make -C vcv -j4` then install per the `robotboy-vcv-install` steps (copy `plugin.dylib`, `plugin.json`, `res/` into the Rack2 plugins dir).
- [ ] **Step 4:** Final commit if any install-related artifacts changed (normally none tracked).

## Self-review notes

- Spec §1–§5 each map to Tasks 2, 3, 4, 1, and "no-op" respectively; CHANGELOG = Task 5; verification = Task 6. Covered.
- Enum values unchanged → patch compatibility preserved (Global Constraints).
- Constant rename (Task 3) is deliberately separate from the whole-word enum rename (Task 1) so `\bkTape\b` never corrupts `kTapeLpHz`.
