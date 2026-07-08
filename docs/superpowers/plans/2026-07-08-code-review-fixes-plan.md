# Code-Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the five highest-priority defects from `code-review-2026-07-08.md`: MF-20 K35 divergence, beads_dsp Process() OOB, beads_dsp block-rate feedback, beads_dsp freeze crossfade, Loooop jitter/crossfade, and the three Particules wrapper issues.

**Architecture:** Each fix is a self-contained change to one DSP unit plus its tests. No public plugin behavior changes except the bug fixes themselves. Spec: `docs/superpowers/plans/2026-07-08-code-review-fixes-spec.md` (read it before implementing your task).

**Tech Stack:** C++ (C++17/20), VCV Rack plugin + MetaModule cross-build. Three test lanes: `tests/run.sh` (per-module g++ harnesses in tests/mf20, tests/loooop, tests/particules), `tests/beads/run.sh` (Catch2 via CMake/CTest), `python3 -m unittest discover -s tests`.

## Global Constraints

- Work on branch `code-review-fixes` (created before Task 1).
- No heap allocation, locks, or unbounded loops in audio-path code.
- TDD: write the failing test first and RUN it to see it fail before implementing.
- Commit per task; message ≤ 15 words; **no Co-Authored-By / AI attribution lines**.
- Do not fix unrelated findings you notice — they're tracked in the review doc.
- All three test lanes must pass at the end of every task (`tests/run.sh` currently exits 0 in ~20 s; `tests/beads/run.sh` in ~1 min).

---

### Task 1: MF-20 K35 loop saturation

**Files:**
- Modify: `src/mf20/MF20Filter.hpp` (processK35, lines 171-211; doc block lines 33-60)
- Test: `tests/mf20/test_mf20.cpp`

**Interfaces:**
- Consumes: `resTaper()` from `src/mf20/dsp_utils.hpp` (test-side only).
- Produces: no signature changes. `processK35` behavior changes only when `|k·x₁| > 1` (previously divergent).

**Background:** `resTaper()` maps the resonance knob to [0, 1.025]; the filter receives `res` up to 1.025 → `k = res·8/3` up to ≈ 2.733 > 8/3 → negative damping in a purely linear loop → NaN. Fix: clip the `k·x₁` feedback term with the OTA-style piecewise-linear clip at fixed normalized threshold 1.0 / slope 0.25, solved region-wise in closed form. In the linear region the math is algebraically identical to the current code (`(1+g)² + (2/3)g − g·k` = `(1+g)² − g·(k − 2/3)`), so existing K35 tests keep passing.

**Numerical hazard you must handle:** for `k > 8/3`, `D1 = (1+g)² − g·(k − 2/3)` has real roots in `g` — e.g. at `k = 2.733`, `D1 ≤ 0` for `g ∈ [0.773, 1.294]` (cutoff ≈ 11.6–16.6 kHz at 48 kHz). There is then no valid linear-region solution (bistable analog regime) and `rhs/D1` is garbage or ±inf/NaN (division by ~0). Guard with `if (D1 > 0.f)` and fall back to the saturated-region solve with sign taken from `rhs`. `D2 = base − 0.25·g·k` is always positive (roots would need k ≥ 10.7).

- [ ] **Step 1: Fix the test_zero_io accumulator bug**

In `tests/mf20/test_mf20.cpp` (function starts line ~99), replace the `buf[0]`-based accounting:

```cpp
static void test_zero_io() {
    printf("\n1. Zero input → zero output\n");
    MF20Filter f;
    bool allOk = true;
    for (float res : {0.f, 0.5f, 1.f}) {
        for (float fc : {200.f, 1000.f, 8000.f}) {
            f.setSampleRate(44100.f);
            f.reset();
            bool ok = true;
            for (int i = 0; i < 1000; i++) {
                auto [lp, bp, hp] = f.process(0.f, fc, res);
                if (lp != 0.f || bp != 0.f || hp != 0.f) { ok = false; break; }
            }
            allOk = allOk && ok;
        }
    }
    report(allOk, "zero input → zero LP and HP for all (fc, res) combos");
}
```

- [ ] **Step 2: Write the failing stability tests**

Add near the other K35 tests in `tests/mf20/test_mf20.cpp`. Add the include at the top of the file:

```cpp
#include "../../src/mf20/dsp_utils.hpp"   // resTaper — the module applies it before the filter
```

```cpp
// K35 stability through the module's resonance taper.
// MF20FilterModule maps the knob through resTaper() (max 1.025), so the
// filter must stay bounded for res up to 1.025. fc=12000 exercises the
// D1<=0 (bistable) branch at k > 8/3.
static void test_k35_res_taper_stability() {
    printf("\n19. K35 bounded at res = resTaper(knob), knob up to 1.0\n");
    const float fs = 48000.f;
    bool allOk = true;
    char detail[128] = {0};
    for (float knob : {0.85f, 0.90f, 1.00f}) {
        for (float fc : {1000.f, 12000.f}) {
            MF20Filter f;
            f.setSampleRate(fs);
            f.setMode(MF20Filter::Mode::K35);
            f.setDriveCharacter(1.f);
            f.reset();
            const float res = resTaper(knob);
            float peak = 0.f;
            bool finite = true;
            // 0.5 s of 220 Hz sine at 0.2 amplitude, then 2 s of silence.
            for (int i = 0; i < (int)(2.5f * fs); i++) {
                float in = (i < (int)(0.5f * fs))
                    ? 0.2f * std::sin(2.f * 3.14159265f * 220.f * i / fs) : 0.f;
                auto [lp, bp, hp] = f.process(in, fc, res);
                if (!std::isfinite(lp) || !std::isfinite(bp) || !std::isfinite(hp)) {
                    finite = false; break;
                }
                float a = std::fabs(lp);
                if (a > peak) peak = a;
            }
            if (!finite || peak > 2.f) {
                allOk = false;
                snprintf(detail, sizeof(detail), "knob=%.2f fc=%.0f finite=%d peak=%g",
                         knob, fc, (int)finite, peak);
            }
        }
    }
    report(allOk, "K35 finite and |lp| < 2 for knob {0.85,0.9,1.0} × fc {1k,12k}", detail);
}

// K35 self-oscillation must survive (bounded) at the taper's maximum.
static void test_k35_taper_self_oscillation() {
    printf("\n20. K35 sustains bounded self-oscillation at res = resTaper(1.0)\n");
    const float fs = 48000.f;
    MF20Filter f;
    f.setSampleRate(fs);
    f.setMode(MF20Filter::Mode::K35);
    f.setDriveCharacter(1.f);
    f.reset();
    const float res = resTaper(1.f);   // 1.025 → k ≈ 2.733
    f.process(0.5f, 1000.f, res);      // kick
    bool finite = true;
    for (int i = 0; i < 48000 && finite; i++) {          // settle 1 s
        auto [lp, bp, hp] = f.process(0.f, 1000.f, res);
        (void)bp; (void)hp;
        finite = std::isfinite(lp);
    }
    static float buf[24000];
    for (int i = 0; i < 24000 && finite; i++) {          // measure 0.5 s
        auto [lp, bp, hp] = f.process(0.f, 1000.f, res);
        (void)bp; (void)hp;
        if (!std::isfinite(lp)) { finite = false; break; }
        buf[i] = lp;
    }
    float rms = finite ? rmsRun(buf, 24000) : 0.f;
    char detail[64];
    snprintf(detail, sizeof(detail), "finite=%d rms=%g", (int)finite, rms);
    report(finite && rms > 0.02f && rms < 2.f,
           "K35 at resTaper(1.0): finite, 0.02 < RMS < 2", detail);
}
```

Register both in `main()` (line ~739) after `test_k35_self_oscillation();`:

```cpp
    test_k35_res_taper_stability();
    test_k35_taper_self_oscillation();
```

- [ ] **Step 3: Run to verify the new tests FAIL**

Run: `cd tests && ./run.sh` (or compile just mf20: `g++ -std=c++20 -O2 -I../src -o /tmp/t mf20/test_mf20.cpp && /tmp/t` from tests/).
Expected: tests 19 and 20 FAIL (non-finite / peak blowup). All others PASS.

- [ ] **Step 4: Implement the loop clip in processK35**

Replace the solve section of `processK35` in `src/mf20/MF20Filter.hpp` (keep the forward-path `clip_in` block exactly as is):

```cpp
        // Resonance loop with saturating feedback:
        //   ẋ₁ = ωc·(clip_in − (8/3)·x₁ + fbClip(k·x₁) − x₂)
        // The Korg35 loop transistor clips, which bounds the resonance even
        // when the module's resTaper() pushes k past the linear-oscillation
        // threshold 8/3 (max k = 1.025 × 8/3 ≈ 2.733). Fixed normalised
        // threshold/slope: drive shapes the input clip only.
        // Linear region (|k·x₁| ≤ 1) is algebraically identical to the
        // previous formulation: base − g·k = (1+g)² − g·(k − 2/3).
        constexpr float kFbThreshold = 1.f;
        constexpr float kFbSlope     = 0.25f;

        float rhs  = s1 + g * (clip_in - s2);
        float base = (1.f + g) * (1.f + g) + (2.f / 3.f) * g;
        float D1   = base - g * k;

        // For k > 8/3, D1 ≤ 0 over a band of g (fc ≈ 11.6–16.6 kHz at 48 kHz
        // when k = 2.733): the linear region has no valid solution there
        // (bistable analog regime), so solve the saturated region, taking the
        // branch sign from the drive term rhs. When D1 > 0 the region-1 trial
        // classifies exactly (implicit LHS is monotone), as in processOTA.
        bool linear = false;
        float x1_mid = 0.f, fb_val = 0.f;
        if (D1 > 0.f) {
            float x1_try = rhs / D1;
            if (std::fabs(k * x1_try) <= kFbThreshold) {
                x1_mid = x1_try;
                fb_val = k * x1_mid;
                linear = true;
            }
        }
        if (!linear) {
            float D2   = base - kFbSlope * g * k;
            float sign = (rhs > 0.f) ? 1.f : -1.f;
            float knee = (1.f - kFbSlope) * g * kFbThreshold;
            x1_mid = (rhs + sign * knee) / D2;
            fb_val = kFbSlope * k * x1_mid + sign * (1.f - kFbSlope) * kFbThreshold;
        }

        float x2_mid = s2 + g * x1_mid;
        // HP from state equation: clip(in) − (8/3)·x₁ + fbClip(k·x₁) − x₂
        float hp_out = clip_in - (8.f / 3.f) * x1_mid + fb_val - x2_mid;

        s1 = 2.f * x1_mid - s1;
        s2 = 2.f * x2_mid - s2;

        return { x2_mid, x1_mid, hp_out };
```

(Note: when D1 > 0, `sign(rhs) == sign(rhs/D1)`, so using `rhs` for the branch sign matches the old OTA-style `x1_r1 > 0` test.)

Update the K35 section of the header doc comment (lines 33-60): the nonlinearity is now in **both** the forward path (input clip, drive-scaled) and the resonance loop (fixed clip at T=1, slope 0.25); state equation `ẋ₁ = ωc·(clip(in) − (8/3)·x₁ + fbClip(k·x₁) − x₂)`; note the D1 ≤ 0 fallback.

- [ ] **Step 5: Run all mf20 + loooop + particules harness tests**

Run: `cd tests && ./run.sh`
Expected: everything PASSES, including the pre-existing K35 tests (self-oscillation at res=1.0, K35-vs-OTA difference, asymmetry tests) and the new 19/20.

- [ ] **Step 6: Commit**

```bash
git add src/mf20/MF20Filter.hpp tests/mf20/test_mf20.cpp
git commit -m "fix: saturate K35 resonance loop so resTaper overdrive can't diverge"
```

---

### Task 2: beads_dsp Process() chunking

**Files:**
- Modify: `src/vendor/beads_dsp/src/beads_processor.cpp` (Process, lines 144-285)
- Modify: `src/vendor/beads_dsp/include/beads/beads.h` (add private `ProcessBlock` declaration to `class BeadsProcessor`)
- Modify: `src/vendor/beads_dsp/src/beads_processor.h` (fix stale comment at lines 56-58)
- Create: `tests/beads/test_block_size.cpp`

**Interfaces:**
- Produces: `void BeadsProcessor::ProcessBlock(const StereoFrame* input, StereoFrame* output, size_t num_frames)` — private, `num_frames ≤ kMaxBlockSize`, used only by `Process()`. Task 3 modifies this function's body.

- [ ] **Step 1: Write the failing test**

Create `tests/beads/test_block_size.cpp` (the CMake GLOB picks up `test_*.cpp` automatically):

```cpp
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cmath>
#include <cstdint>

#include "beads/beads.h"

using namespace beads;

static constexpr float kSR = 48000.0f;

namespace {
struct Proc {
    std::vector<uint8_t> memory;
    BeadsProcessor p;
    Proc() {
        auto req = BeadsProcessor::GetMemoryRequirements(kSR);
        memory.resize(req.total_bytes, 0);
        p.Init(memory.data(), memory.size(), kSR);
    }
};

BeadsParameters DryParams() {
    BeadsParameters params{};
    params.dry_wet = 0.0f;          // full dry — routes through dry_input_buf
    params.auto_gain = false;
    params.manual_gain_db = 12.0f;  // makes recorded (gained) audio differ from dry
    params.feedback = 0.0f;
    params.reverb = 0.0f;
    return params;
}
}  // namespace

// Process(input, output, 256) must equal four consecutive Process(…, 64)
// calls on an identically-initialized processor fed the same stream.
// Before the chunking fix, the 256-frame call read dry_input_buf (a 64-frame
// array) out of bounds for frames 64-255, so the dry path diverged.
TEST_CASE("BeadsProcessor: Process is block-size invariant (256 vs 4x64)",
          "[processor][blocksize]") {
    constexpr size_t kTotal = 2048;
    std::vector<StereoFrame> input(kTotal), outBig(kTotal), outSmall(kTotal);
    for (size_t i = 0; i < kTotal; ++i) {
        float v = 0.5f * std::sin(2.0 * M_PI * 330.0 * i / kSR)
                + 0.25f * std::sin(2.0 * M_PI * 917.0 * i / kSR);
        input[i] = {v, -v};
    }

    Proc a, b;
    auto params = DryParams();
    a.p.SetParameters(params);
    b.p.SetParameters(params);

    for (size_t off = 0; off < kTotal; off += 256)
        a.p.Process(input.data() + off, outBig.data() + off, 256);
    for (size_t off = 0; off < kTotal; off += 64)
        b.p.Process(input.data() + off, outSmall.data() + off, 64);

    for (size_t i = 0; i < kTotal; ++i) {
        REQUIRE(outBig[i].l == outSmall[i].l);
        REQUIRE(outBig[i].r == outSmall[i].r);
    }
}
```

Note: if `BeadsParameters` field names differ from the above, check `include/beads/parameters.h` and adjust — do not guess.

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests/beads && ./run.sh`
Expected: the new TEST_CASE FAILS with sample mismatches (frames ≥ 64 of each 256-call); everything else passes. If it unexpectedly passes, increase the manual gain or set `params.feedback = 0.5f` so the OOB region (recording buffer contents) differs more from the dry input, and re-verify red.

- [ ] **Step 3: Restructure Process() into chunked ProcessBlock()**

In `include/beads/beads.h`, add to the private section of `class BeadsProcessor`:

```cpp
    // One internal block (num_frames <= kMaxBlockSize). Public Process()
    // chunks arbitrary num_frames into these.
    void ProcessBlock(const StereoFrame* input, StereoFrame* output,
                      size_t num_frames);
```

In `src/vendor/beads_dsp/src/beads_processor.cpp`, replace `Process()` (lines 144-285) with:

```cpp
void BeadsProcessor::Process(const StereoFrame* input, StereoFrame* output,
                              size_t num_frames) {
    if (!impl_) {
        for (size_t i = 0; i < num_frames; ++i) {
            output[i] = {0.0f, 0.0f};
        }
        return;
    }
    // Chunk the full pipeline so every per-block buffer (dry_input_buf,
    // wet_buf) is indexed only by the intra-block offset. This is what makes
    // the types.h promise of arbitrary num_frames true.
    size_t offset = 0;
    while (offset < num_frames) {
        size_t block = std::min(num_frames - offset, kMaxBlockSize);
        ProcessBlock(input + offset, output + offset, block);
        offset += block;
    }
}

void BeadsProcessor::ProcessBlock(const StereoFrame* input, StereoFrame* output,
                                   size_t num_frames) {
    auto& s = *impl_;  // shorthand

    // Drain any deferred buffer clear (post-quality-change) incrementally.
    // Runs once per <=64-frame block on every host, so the clear rate and the
    // quality-transition duck stay aligned at any caller block size.
    static constexpr size_t kClearChunkFloats = (kDefaultBufferFrames / 128) * 2;
    s.recording_buffer.TickClear(kClearChunkFloats);

    // Detect freeze transitions for crossfade
    if (s.params.freeze != s.prev_freeze) {
        s.recording_buffer.StartFreezeCrossfade();
        s.prev_freeze = s.params.freeze;
    }

    // --- Per-sample input processing (steps 1-4) ---
    for (size_t i = 0; i < num_frames; ++i) {
        StereoFrame in = input[i];

        // Save pre-processing frame for dry output path so DRY/WET=0 matches bypass.
        s.dry_input_buf[i] = in;

        // 1. Auto-gain
        in = s.auto_gain.Process(in, s.params.manual_gain_db, s.params.auto_gain);

        // 2. Quality input processing
        in = s.quality_processor.ProcessInput(in, s.params.quality_mode);

        // 3. Feedback mix (smoothed to prevent zipper noise)
        OnePole(s.smoothed_feedback, s.params.feedback, 0.002f);
        StereoFrame fb = {
            s.feedback_hp_l.ProcessHP(s.feedback_sample.l),
            s.feedback_hp_r.ProcessHP(s.feedback_sample.r)
        };
        // Scale source down by (feedback × 0.5) to leave headroom for
        // additive feedback.
        float source_scale = 1.0f - s.smoothed_feedback * 0.5f;
        in.l *= source_scale;
        in.r *= source_scale;
        StereoFrame mixed = in + fb * (s.smoothed_feedback * s.smoothed_feedback);
        in = s.saturation.LimitFeedback(mixed, s.params.quality_mode);

        // 4. Record to buffer (unless frozen)
        if (!s.params.freeze) {
            s.recording_buffer.Write(in);
        }
        if (s.recording_buffer.crossfading()) {
            s.recording_buffer.ProcessFreezeCrossfade();
        }
    }

    // --- Block-based wet signal generation + output processing (steps 5-10) ---
    // wet lives in Impl (DRAM) to keep audio-thread stack usage low.
    StereoFrame* wet = s.wet_buf;

    // Tape mode wow/flutter: compute pitch modulation for this block.
    // The modulation is very slow (0.5Hz wow) so one value per block is fine.
    float pitch_mod = s.quality_processor.GetPitchModulation(s.params.quality_mode, num_frames);
    s.grain_engine.SetPitchModulation(pitch_mod);
    s.grain_engine.Process(s.params, wet, num_frames);

    // Advance dry/wet smoothing and compute equal-power gains once per
    // block.  The OnePole with 0.002 coefficient changes < 0.13% across
    // 64 samples, so per-block cos/sin is inaudible vs per-sample.
    // Closed-form equivalent of running OnePole block times: avoids
    // the O(N) loop and gives the correct end-state in one step.
    {
        float a = 1.0f - std::pow(1.0f - 0.002f, static_cast<float>(num_frames));
        s.smoothed_dry_wet += a * (s.params.dry_wet - s.smoothed_dry_wet);
    }
    float dw_phase = s.smoothed_dry_wet * 0.25f;
    float dry_gain = CosLookup(dw_phase);
    float wet_gain = CosLookup(dw_phase - 0.25f);

    // Per-sample output processing for this block
    for (size_t i = 0; i < num_frames; ++i) {
        StereoFrame wet_frame = wet[i];

        // Quality mode transition: V-shaped duck on wet signal
        if (s.quality_xfade_counter > 0) {
            float phase = 1.0f - static_cast<float>(s.quality_xfade_counter)
                        / static_cast<float>(Impl::kQualityXfadeSamples);
            // phase: 0 at start → 1 at end
            float duck;
            if (phase < 0.5f) {
                duck = 1.0f - phase * 2.0f;   // 1 → 0
            } else {
                duck = (phase - 0.5f) * 2.0f;  // 0 → 1
            }
            wet_frame *= duck;
            s.quality_xfade_counter--;
        }

        // 6. Quality output processing
        wet_frame = s.quality_processor.ProcessOutput(wet_frame, s.params.quality_mode);

        // 7. Capture feedback sample (before reverb)
        // Guard against NaN/inf poisoning the feedback loop permanently
        if (std::isfinite(wet_frame.l) && std::isfinite(wet_frame.r)) {
            s.feedback_sample = wet_frame;
        } else {
            s.feedback_sample = {0.0f, 0.0f};
        }

        // 8. Dry/wet crossfade (equal-power, gains precomputed per block)
        StereoFrame in_frame = s.dry_input_buf[i];
        StereoFrame mixed = {
            in_frame.l * dry_gain + wet_frame.l * wet_gain,
            in_frame.r * dry_gain + wet_frame.r * wet_gain
        };

        // 9. Reverb
        float rev_l, rev_r;
        s.reverb.Process(mixed.l, mixed.r, &rev_l, &rev_r);
        mixed = {rev_l, rev_r};

        // 10. Output
        output[i] = mixed;
    }
}
```

(Behavioral deltas, all intended: `dry_input_buf` never indexed past the block; the dead `output[i] = {0,0}` store is gone; TickClear and the freeze check now run per ≤64-frame block, aligning clear rate with the duck at any caller block size.)

In `src/vendor/beads_dsp/src/beads_processor.h` lines 56-58, update the stale comment:

```cpp
    // Input captured before auto-gain, for dry/wet mix output.
    // Indexed by intra-block offset only; Process() chunks caller frames
    // into blocks of <= kMaxBlockSize.
    StereoFrame dry_input_buf[kMaxBlockSize];
```

- [ ] **Step 4: Run the full beads suite**

Run: `cd tests/beads && ./run.sh`
Expected: all tests PASS, including the new block-size test and the pre-existing 256-frame tests (whose expected values may previously have been computed over UB — if any *existing* assertion now fails, the new behavior is the correct one; inspect and update that test's expectation with a comment, do not weaken the new test).

- [ ] **Step 5: Commit**

```bash
git add src/vendor/beads_dsp/include/beads/beads.h src/vendor/beads_dsp/src/beads_processor.cpp src/vendor/beads_dsp/src/beads_processor.h tests/beads/test_block_size.cpp
git commit -m "fix: chunk BeadsProcessor::Process so num_frames > 64 is safe"
```

---

### Task 3: beads_dsp per-sample feedback path

**Files:**
- Modify: `src/vendor/beads_dsp/src/beads_processor.h` (Impl members)
- Modify: `src/vendor/beads_dsp/src/beads_processor.cpp` (Init + ProcessBlock steps 3 and 7)
- Create: `tests/beads/test_feedback_path.cpp`

**Interfaces:**
- Consumes: `ProcessBlock` from Task 2.
- Produces: Impl members `StereoFrame prev_wet_buf[kMaxBlockSize]` and `size_t prev_wet_len` (Task 4 does not touch them).

**Background:** the input loop reads a single `feedback_sample` that is only updated in the wet loop — so at 64-frame blocks the whole block's feedback derives from one held sample, and the 30 Hz HP (stepped per sample on a constant) drains it toward zero. Fix: keep the previous block's full wet output and mix it per-sample.

- [ ] **Step 1: Write the failing test**

Create `tests/beads/test_feedback_path.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cmath>
#include <cstdint>

#include "beads/beads.h"

using namespace beads;

static constexpr float kSR = 48000.0f;

namespace {
struct Proc {
    std::vector<uint8_t> memory;
    BeadsProcessor p;
    Proc() {
        auto req = BeadsProcessor::GetMemoryRequirements(kSR);
        memory.resize(req.total_bytes, 0);
        p.Init(memory.data(), memory.size(), kSR);
    }
};

double WetEnergyAt64(float feedback) {
    Proc proc;
    BeadsParameters params{};
    params.dry_wet = 1.0f;          // full wet
    params.auto_gain = false;
    params.manual_gain_db = 0.0f;
    params.feedback = feedback;
    params.reverb = 0.0f;
    params.density = 0.8f;          // plenty of grains
    proc.p.SetParameters(params);

    constexpr size_t kFrames = 48000 * 2;   // 2 s
    StereoFrame in[64], out[64];
    double energy = 0.0;
    for (size_t off = 0; off < kFrames; off += 64) {
        for (size_t i = 0; i < 64; ++i) {
            size_t n = off + i;
            float v = (n < 48000)   // 1 s of tone, then 1 s of silence
                ? 0.5f * std::sin(2.0 * M_PI * 220.0 * n / kSR) : 0.0f;
            in[i] = {v, v};
        }
        proc.p.Process(in, out, 64);
        if (off >= 48000) {          // measure the tail second only
            for (size_t i = 0; i < 64; ++i)
                energy += (double)out[i].l * out[i].l + (double)out[i].r * out[i].r;
        }
    }
    return energy;
}
}  // namespace

// At 64-frame cadence (MetaModule), high feedback must add substantial
// regenerated energy vs feedback=0. Before the fix the feedback source was a
// single sample held for the whole block and drained by the 30 Hz HP, so
// feedback was nearly inert at this cadence.
TEST_CASE("BeadsProcessor: feedback regenerates at 64-frame cadence",
          "[processor][feedback]") {
    double e0 = WetEnergyAt64(0.0f);
    double e9 = WetEnergyAt64(0.9f);
    INFO("tail energy fb=0: " << e0 << "  fb=0.9: " << e9);
    REQUIRE(e9 > e0 * 1.5);
}
```

Note: check `include/beads/parameters.h` for exact field names (`density`, `dry_wet`, etc.) and adjust. After running both red and green, record the two measured energies in a comment and, if the pre-fix ratio is close to 1.5, move the threshold to the geometric midpoint between pre-fix and post-fix ratios.

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests/beads && ./run.sh`
Expected: new TEST_CASE FAILS (`e9` barely above `e0`). If it passes pre-fix, the observable is too weak — raise feedback to 1.0 and/or lengthen the tail window, and confirm red before proceeding.

- [ ] **Step 3: Implement the previous-block wet buffer**

In `src/vendor/beads_dsp/src/beads_processor.h`, replace the `feedback_sample` member (lines 36-37):

```cpp
    // Previous block's wet output (post-quality-processing, pre-reverb,
    // NaN-guarded). The next block's input stage mixes prev_wet_buf[i] in
    // per-sample — a one-block feedback delay (1 frame in VCV, 64 on MM),
    // matching the original Beads design instead of a block-rate hold.
    StereoFrame prev_wet_buf[kMaxBlockSize] = {};
    size_t prev_wet_len = 0;
```

In `src/vendor/beads_dsp/src/beads_processor.cpp` `Init()`, replace `impl_->feedback_sample = {0.0f, 0.0f};` with:

```cpp
    impl_->prev_wet_len = 0;
```

In `ProcessBlock()` step 3, replace the `fb` computation:

```cpp
        // 3. Feedback mix (smoothed to prevent zipper noise)
        OnePole(s.smoothed_feedback, s.params.feedback, 0.002f);
        StereoFrame fb_src = {0.0f, 0.0f};
        if (s.prev_wet_len > 0) {
            size_t fb_idx = (i < s.prev_wet_len) ? i : s.prev_wet_len - 1;
            fb_src = s.prev_wet_buf[fb_idx];
        }
        StereoFrame fb = {
            s.feedback_hp_l.ProcessHP(fb_src.l),
            s.feedback_hp_r.ProcessHP(fb_src.r)
        };
```

In `ProcessBlock()` step 7, replace the capture:

```cpp
        // 7. Capture this block's wet frame for the next block's feedback
        // (before reverb). Guard against NaN/inf poisoning the loop.
        if (std::isfinite(wet_frame.l) && std::isfinite(wet_frame.r)) {
            s.prev_wet_buf[i] = wet_frame;
        } else {
            s.prev_wet_buf[i] = {0.0f, 0.0f};
        }
```

After the per-sample output loop (end of `ProcessBlock`), add:

```cpp
    s.prev_wet_len = num_frames;
```

- [ ] **Step 4: Run the full beads suite**

Run: `cd tests/beads && ./run.sh`
Expected: all PASS, including Task 2's block-size invariance test (256 vs 4×64 both decompose into the same 64-frame block sequence, so they stay bit-identical) and the pre-existing feedback-divergence test.

- [ ] **Step 5: Commit**

```bash
git add src/vendor/beads_dsp/src/beads_processor.h src/vendor/beads_dsp/src/beads_processor.cpp tests/beads/test_feedback_path.cpp
git commit -m "fix: per-sample feedback from previous block's wet output"
```

---

### Task 4: beads_dsp freeze crossfade rewrite

**Files:**
- Modify: `src/vendor/beads_dsp/src/buffer/recording_buffer.h` (API + members)
- Modify: `src/vendor/beads_dsp/src/buffer/recording_buffer.cpp` (Write, Init, freeze section lines 210-278)
- Modify: `src/vendor/beads_dsp/src/beads_processor.cpp` (ProcessBlock freeze handling)
- Modify: `tests/beads/test_buffer.cpp` (replace the "Freeze crossfade" TEST_CASE)

**Interfaces:**
- Consumes: `ProcessBlock` from Tasks 2-3.
- Produces: `void RecordingBuffer::NotifyFreeze(bool frozen)`. Removes `StartFreezeCrossfade()`, `ProcessFreezeCrossfade()`, `crossfading()`. Before removing, `grep -rn "crossfading\|FreezeCrossfade" src tests` and update every caller.

- [ ] **Step 1: Write the failing tests**

In `tests/beads/test_buffer.cpp`, replace the `TEST_CASE("RecordingBuffer: Freeze crossfade", ...)` (lines ~90-111) with:

```cpp
TEST_CASE("RecordingBuffer: entering freeze fades both sides of the seam to zero",
          "[buffer][freeze]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    float* data = reinterpret_cast<float*>(mem.data());

    // Fill the whole buffer with a sine (has zero crossings — the old code
    // skipped the fade entirely whenever it found one).
    for (size_t i = 0; i < num_frames; ++i) {
        float v = std::sin(2.0 * M_PI * i / 50.0) * 0.8f + 0.1f;
        buf.Write(v, v);
    }
    // Overwrite the seam region with DC so gains are directly observable.
    // write_head_ has wrapped to 0; write 500 frames of 1.0 → seam at 500.
    for (size_t i = 0; i < 500; ++i) buf.Write(1.0f, 1.0f);
    REQUIRE(buf.write_head() == 500);
    for (size_t i = 400; i < 600; ++i) { data[i * 2] = 1.0f; data[i * 2 + 1] = 1.0f; }

    buf.NotifyFreeze(true);

    // Newest side: frame 499 ≈ 0, ramp up moving away from the seam.
    REQUIRE(std::fabs(data[499 * 2]) < 1e-6f);
    REQUIRE(data[498 * 2] > 0.0f);
    REQUIRE(data[498 * 2] < data[490 * 2]);
    REQUIRE(data[490 * 2] < data[468 * 2]);
    // Oldest side: frame 500 ≈ 0, ramp up moving away.
    REQUIRE(std::fabs(data[500 * 2]) < 1e-6f);
    REQUIRE(data[501 * 2] > 0.0f);
    REQUIRE(data[501 * 2] < data[531 * 2]);
    // Frames >= kCrossfadeSamples away untouched.
    REQUIRE(data[467 * 2] == 1.0f);
    REQUIRE(data[532 * 2] == 1.0f);
}

TEST_CASE("RecordingBuffer: freeze fade near frame 0 keeps the tail mirror in sync",
          "[buffer][freeze]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    float* data = reinterpret_cast<float*>(mem.data());

    // Put the write head just past 0 so the fade wraps across the boundary.
    for (size_t i = 0; i < num_frames; ++i) buf.Write(1.0f, 1.0f);
    for (size_t i = 0; i < 2; ++i) buf.Write(1.0f, 1.0f);
    REQUIRE(buf.write_head() == 2);

    buf.NotifyFreeze(true);

    // Any faded frame < kInterpolationTail must be mirrored into the tail.
    for (int f = 0; f < kInterpolationTail; ++f) {
        REQUIRE(data[(num_frames + f) * 2] == data[f * 2]);
        REQUIRE(data[(num_frames + f) * 2 + 1] == data[f * 2 + 1]);
    }
}

TEST_CASE("RecordingBuffer: leaving freeze blends writes from old content to live input",
          "[buffer][freeze]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    float* data = reinterpret_cast<float*>(mem.data());

    for (size_t i = 0; i < 500; ++i) buf.Write(1.0f, 1.0f);
    size_t seam = buf.write_head();

    buf.NotifyFreeze(true);
    buf.NotifyFreeze(false);
    // Old content at the resume point is ~0 (the freeze fade zeroed it).
    for (size_t i = 0; i < 64; ++i) buf.Write(0.5f, 0.5f);

    // First write ≈ old content (near 0), ramping toward the incoming 0.5.
    REQUIRE(std::fabs(data[seam * 2]) < 0.05f);
    float prev = data[seam * 2];
    for (size_t i = 1; i < 32; ++i) {
        REQUIRE(data[(seam + i) * 2] >= prev - 1e-6f);
        prev = data[(seam + i) * 2];
    }
    // After the ramp, writes are stored verbatim.
    REQUIRE(data[(seam + 40) * 2] == 0.5f);
}
```

- [ ] **Step 2: Run to verify the new cases fail**

Run: `cd tests/beads && ./run.sh`
Expected: the three new cases FAIL to compile (no `NotifyFreeze`) — that's the red state for an API replacement. Everything else unchanged.

- [ ] **Step 3: Implement NotifyFreeze + write ramp**

`src/vendor/beads_dsp/src/buffer/recording_buffer.h`: replace the freeze API block (lines 95-99) with:

```cpp
    // Freeze-transition declicking. Call on every freeze-state change.
    // Entering freeze: one-shot symmetric fade-to-zero at the write seam
    // (2 × kCrossfadeSamples frames) so looped playback crosses silence
    // instead of a discontinuity.
    // Leaving freeze: arms a write crossfade — the next kCrossfadeSamples
    // accepted writes blend from the old buffer content into the incoming
    // audio, so recorded content transitions smoothly back to live input.
    void NotifyFreeze(bool frozen);
```

Remove `bool crossfading() const { return crossfading_; }` (line 120) and replace the private freeze state (lines 142-145) with:

```cpp
    // Unfreeze write-crossfade state (counts accepted writes remaining)
    int write_ramp_remaining_ = 0;
    static constexpr int kCrossfadeSamples = 32;
```

`src/vendor/beads_dsp/src/buffer/recording_buffer.cpp`:

In `Init()`, replace `crossfading_ = false; crossfade_counter_ = 0;` with `write_ramp_remaining_ = 0;`.

In `Write(float, float)`, add the ramp between the decimation gate and the store:

```cpp
    size_t idx = write_head_ * channels_;

    // Unfreeze crossfade: blend from the frozen content into live input.
    if (write_ramp_remaining_ > 0) {
        float g = 1.0f - static_cast<float>(write_ramp_remaining_)
                       / static_cast<float>(kCrossfadeSamples);   // 0 → 1
        left  = buffer_[idx]     + (left  - buffer_[idx])     * g;
        right = buffer_[idx + 1] + (right - buffer_[idx + 1]) * g;
        --write_ramp_remaining_;
    }

    buffer_[idx] = left;
    buffer_[idx + 1] = right;
```

Replace the whole freeze section (lines 210-278) with:

```cpp
// ---------------------------------------------------------------------------
// Freeze declicking
// ---------------------------------------------------------------------------

void RecordingBuffer::NotifyFreeze(bool frozen) {
    if (!buffer_ || size_ == 0 || channels_ < 2) return;

    if (!frozen) {
        // Leaving freeze: blend the next writes from frozen content into
        // live input. No buffer mutation here.
        write_ramp_remaining_ = kCrossfadeSamples;
        return;
    }

    write_ramp_remaining_ = 0;

    // Entering freeze: fade both sides of the seam to zero so grain playback
    // crossing write_head_ passes through silence instead of a hard step
    // from the newest audio into the oldest. One-shot, O(kCrossfadeSamples).
    int size_int = static_cast<int>(size_);
    int fade = kCrossfadeSamples;
    if (fade > size_int / 2) fade = size_int / 2;   // degenerate small buffers

    for (int j = 0; j < fade; ++j) {
        float gain = static_cast<float>(j) / static_cast<float>(fade);
        int newest = static_cast<int>(write_head_) - 1 - j;
        newest = ((newest % size_int) + size_int) % size_int;
        int oldest = (static_cast<int>(write_head_) + j) % size_int;
        const int frames[2] = {newest, oldest};
        for (int f = 0; f < 2; ++f) {
            size_t idx = static_cast<size_t>(frames[f]) * channels_;
            for (int c = 0; c < channels_; ++c) {
                buffer_[idx + c] *= gain;
            }
            // Keep the interpolation-tail mirror in sync.
            if (frames[f] < static_cast<int>(kInterpolationTail)) {
                size_t tail = (size_ + static_cast<size_t>(frames[f])) * channels_;
                for (int c = 0; c < channels_; ++c) {
                    buffer_[tail + c] = buffer_[idx + c];
                }
            }
        }
    }
}
```

(`kInterpolationTail` comes from `types.h`; adjust the cast if it is already `int`.)

`src/vendor/beads_dsp/src/beads_processor.cpp` `ProcessBlock()`: replace the freeze detection with

```cpp
    // Freeze transitions: declick the seam / arm the unfreeze write ramp.
    if (s.params.freeze != s.prev_freeze) {
        s.recording_buffer.NotifyFreeze(s.params.freeze);
        s.prev_freeze = s.params.freeze;
    }
```

and delete the per-sample `if (s.recording_buffer.crossfading()) { s.recording_buffer.ProcessFreezeCrossfade(); }` from the input loop.

Then `grep -rn "crossfading\|FreezeCrossfade" src tests` — fix any remaining caller (e.g. other tests referencing the old API).

- [ ] **Step 4: Run the full beads suite**

Run: `cd tests/beads && ./run.sh`
Expected: all PASS, including the three new freeze cases and the existing click-prevention tests.

- [ ] **Step 5: Commit**

```bash
git add src/vendor/beads_dsp/src/buffer/recording_buffer.h src/vendor/beads_dsp/src/buffer/recording_buffer.cpp src/vendor/beads_dsp/src/beads_processor.cpp tests/beads/test_buffer.cpp
git commit -m "fix: rewrite freeze declick — seam fade on freeze, write ramp on unfreeze"
```

---

### Task 5: Loooop jitter-aware seam crossfade

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (PlayHead, private decls)
- Modify: `src/loooop/dsp/LoopEngine.cpp` (rollJitter/commitJitter, setJitter, restartHead, windowBounds overload, readHead, advanceHead)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Produces: `PlayHead::jitterNext`; `void commitJitter(PlayHead&)`; `void windowBounds(const PlayHead&, float jitterOff, double&, double&) const` overload. Nothing outside LoopEngine changes.

- [ ] **Step 1: Write the failing test**

Append to `tests/loooop/test_loop_engine.cpp` (uses the file's existing `check()` harness; register the function in `main()`):

```cpp
static void test_jitter_crossfade_continuity() {
    LoopEngine e;
    e.reset(48000.f, 2.f);
    soloHead0(e);
    e.toggleRecord();
    const int N = 24000;                       // 0.5 s loop
    for (int i = 0; i < N; ++i)
        e.process((float)std::sin(2.0 * M_PI * 100.0 * i / 48000.0));
    e.toggleRecord();
    e.setJitter(0, 1.f);
    e.setSize(0, 0.25f);                       // 6000-sample window, ~240-sample fade
    e.setCrossfade(true);
    float prev = e.process(0.f);
    float maxDelta = 0.f;
    for (int i = 0; i < 96000; ++i) {          // 2 s → ~16 jittered wraps
        float cur = e.process(0.f);
        float d = std::fabs(cur - prev);
        if (d > maxDelta) maxDelta = d;
        prev = cur;
    }
    // Continuous playback slope of a 100 Hz unit sine at 48 kHz is ~0.013
    // per sample; the smoothstep fade over ~240 samples adds at most ~0.013
    // per sample even between uncorrelated windows. The jitter bug produced
    // full-scale steps (~2.0) at every wrap.
    char msg[64];
    std::snprintf(msg, sizeof(msg), "jitter_crossfade: maxDelta=%.4f < 0.1", maxDelta);
    check(maxDelta < 0.1f, msg);
}
```

(Add `#include <cstdio>` if not present; it is.)

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests && ./run.sh`
Expected: the new check FAILS with maxDelta ≈ 1–2. All existing loop-engine tests still pass.

- [ ] **Step 3: Implement pre-rolled jitter**

`src/loooop/dsp/LoopEngine.hpp`:
- In `struct PlayHead` (line ~73-78), add `float jitterNext = 0.f;` next to `jitterOff`.
- In the private section, add below the existing `windowBounds` declaration:

```cpp
    void windowBounds(const PlayHead& h, float jitterOff,
                      double& winStart, double& winLen) const;
    void commitJitter(PlayHead& h);
```

`src/loooop/dsp/LoopEngine.cpp`:

Replace `rollJitter` (lines 80-85) and add `commitJitter`:

```cpp
// xorshift32: deterministic (seeded in reset), audio-thread safe. Offset up to
// +/- half the loop at jitter 1; jitter 0 always yields exactly 0.
// Rolls the NEXT window's offset; commitJitter() makes it current at the
// wrap. The seam crossfade previews the next window during the fade, so the
// offset must be decided before the fade begins, not at the wrap itself.
void LoopEngine::rollJitter(PlayHead& h) {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    h.jitterNext = h.jitter * ((rng_ >> 8) * (1.f / 16777216.f) - 0.5f);
}

void LoopEngine::commitJitter(PlayHead& h) {
    h.jitterOff = h.jitterNext;
    rollJitter(h);              // pre-roll for the following wrap
}
```

Replace `setJitter` (line 78):

```cpp
void LoopEngine::setJitter(int head, float j01) {
    if (head < 0 || head >= numHeads_) return;
    PlayHead& h = heads_[head];
    float j = clamp01(j01);
    if (j > 0.f && h.jitter == 0.f) {
        h.jitter = j;
        rollJitter(h);          // first wrap after enabling is already random
    } else {
        h.jitter = j;
        if (j == 0.f) h.jitterNext = 0.f;
    }
}
```

Replace `restartHead` (lines 87-94) body's jitter handling:

```cpp
void LoopEngine::restartHead(int head) {
    if (head < 0 || head >= numHeads_ || loopLen_ == 0) return;
    PlayHead& h = heads_[head];
    rollJitter(h);       // fresh window now…
    commitJitter(h);     // …made current, with the next one pre-rolled
    double winStart, winLen;
    windowBounds(h, winStart, winLen);
    h.pos = h.speed < 0.f ? winStart + winLen - 1.0 : winStart;
}
```

Split `windowBounds` (lines 120-132) into the overload pair:

```cpp
void LoopEngine::windowBounds(const PlayHead& h, double& winStart, double& winLen) const {
    windowBounds(h, h.jitterOff, winStart, winLen);
}

void LoopEngine::windowBounds(const PlayHead& h, float jitterOff,
                              double& winStart, double& winLen) const {
    const double L = static_cast<double>(loopLen_);
    const double minWinLen = std::ceil(
        static_cast<double>(sampleRate_) * MINIMUM_LOOP_MILLISECONDS / 1000.0);
    winLen = static_cast<double>(h.size) * L;
    if (winLen < minWinLen) winLen = minWinLen;
    if (winLen > L)   winLen = L;
    double centre = static_cast<double>(clamp01(h.centre + jitterOff)) * L;
    winStart = centre - winLen / 2.0;
    if (winStart < 0.0) winStart = 0.0;
    if (winStart + winLen > L) winStart = L - winLen;
    if (winStart < 0.0) winStart = 0.0;
}
```

In `readHead` (lines 192-195), preview from the **next** window:

```cpp
    const double headAdvance = (static_cast<double>(F) - outToSeam) * sp;
    // Preview from the NEXT window (jitterNext) — that is where advanceHead()
    // will resume at the wrap.
    double ns, nl;
    windowBounds(h, h.jitterNext, ns, nl);
    const double headPos = (h.speed >= 0.f)
        ? ns + headAdvance
        : ns + nl - 1.0 - headAdvance;
```

In `advanceHead`, replace both `rollJitter(h);` calls (lines 218 and 232) with `commitJitter(h);`. The `windowBounds(h, ns, nl)` calls just after now see the committed (previously previewed) offset — no other change needed.

- [ ] **Step 4: Run the loooop tests**

Run: `cd tests && ./run.sh`
Expected: all PASS including the new continuity check (jitter = 0 paths are bit-identical: offsets are exactly 0 throughout).

- [ ] **Step 5: Commit**

```bash
git add src/loooop/dsp/LoopEngine.hpp src/loooop/dsp/LoopEngine.cpp tests/loooop/test_loop_engine.cpp
git commit -m "fix: pre-roll jitter so the seam crossfade previews the real next window"
```

---

### Task 6: Particules — pitch quantize, SEED latch, block-aware conditioners

**Files:**
- Create: `src/particules/particules_cv_conditioning.h`
- Modify: `src/particules/particules_block_runtime.h` (seed-gate latch)
- Modify: `src/particules/Particules.cpp` (ctor conditioner Init calls; per-sample latch call in `process()`; `params_.gate` in `updateSlowParams()`)
- Create: `tests/particules/test_cv_conditioning.cpp`
- Modify: `tests/beads/test_particules_block_runtime.cpp` (latch cases)

**Interfaces:**
- Produces:
  - `particules::CvDecimationForBlock(std::size_t) -> int`; `particules::CvSmoothingForBlock(float, std::size_t) -> float`; constants `kCvSmoothing = 0.5f`, `kPitchCvSmoothing = 0.35f`, `kMenuCvQuantizeStep = 0.01f`, `kPitchCvQuantizeStep = 0.0f`.
  - `ParticulesBlockRuntime::NoteSeedGateSample(bool)` and `ConsumeSeedGateLatch() -> bool`.

- [ ] **Step 1: Write the failing tests**

Create `tests/particules/test_cv_conditioning.cpp` (mirror the harness style of `tests/particules/test_pitch_notch_map.cpp` — read it first; it is a standalone g++ test with its own main/check reporting; `tests/run.sh` builds every `tests/particules/test_*.cpp` automatically):

```cpp
#include "../../src/particules/particules_cv_conditioning.h"
#include "../../src/vendor/beads_dsp/src/util/control_conditioner.h"
#include <cstdio>
#include <cmath>

static int g_failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++g_failures; }
    else       { std::printf("ok:   %s\n", name); }
}

int main() {
    using namespace particules;

    // Decimation: one CV sample per ~8 audio samples at any block size.
    check(CvDecimationForBlock(1) == 8,  "decimation(block=1) == 8 (VCV)");
    check(CvDecimationForBlock(8) == 1,  "decimation(block=8) == 1");
    check(CvDecimationForBlock(64) == 1, "decimation(block=64) == 1 (MetaModule)");

    // Smoothing: per-block coefficient equals per-sample applied block times.
    check(std::fabs(CvSmoothingForBlock(0.5f, 1) - 0.5f) < 1e-6f,
          "smoothing(0.5, block=1) == 0.5 (VCV unchanged)");
    check(CvSmoothingForBlock(0.5f, 64) > 0.99f,
          "smoothing(0.5, block=64) ~ 1 (settles within one block)");
    check(CvSmoothingForBlock(0.35f, 64) > 0.99f,
          "smoothing(0.35, block=64) ~ 1");

    // Pitch CV must never be quantized (1 V/oct).
    check(kPitchCvQuantizeStep == 0.0f, "pitch quantize step is 0");

    // A one-semitone CV step survives a pitch-configured conditioner exactly.
    beads::ControlConditioner c;
    c.Init(1, 1.0f, kPitchCvQuantizeStep, 0.0f);
    float semitone = 1.f / 12.f;
    check(c.Process(semitone) == semitone,
          "1/12 V passes through unquantized (0.05 V step returned 0.10 V)");

    // MetaModule-cadence settling: a step input reaches >99% of target after
    // one conditioned block, vs 35% with the old per-sample coefficient.
    beads::ControlConditioner mm;
    mm.Init(CvDecimationForBlock(64), CvSmoothingForBlock(0.35f, 64),
            kPitchCvQuantizeStep, 0.0f);
    mm.Process(0.0f);
    float after_one_block = mm.Process(1.0f);
    check(after_one_block > 0.99f, "MM pitch conditioner settles in ~1 block");

    std::printf(g_failures ? "\n%d FAILURES\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}
```

Add to `tests/beads/test_particules_block_runtime.cpp` (match its existing Catch2/`TEST_CASE` or harness style — read the file first and conform):

```cpp
TEST_CASE("ParticulesBlockRuntime: seed gate latch captures a mid-block pulse",
          "[block_runtime]") {
    ParticulesBlockRuntime<64> rt;
    for (int i = 0; i < 64; ++i) {
        rt.NoteSeedGateSample(i == 20);   // 1-sample trigger mid-block
        rt.ReadOutputSample();
        rt.PushInputSample({0.f, 0.f});
    }
    REQUIRE(rt.BlockReady());
    REQUIRE(rt.ConsumeSeedGateLatch() == true);
    REQUIRE(rt.ConsumeSeedGateLatch() == false);   // consume clears
}

TEST_CASE("ParticulesBlockRuntime: seed gate latch is per-sample passthrough at block 1",
          "[block_runtime]") {
    ParticulesBlockRuntime<1> rt;
    rt.NoteSeedGateSample(false);
    REQUIRE(rt.ConsumeSeedGateLatch() == false);
    rt.NoteSeedGateSample(true);
    REQUIRE(rt.ConsumeSeedGateLatch() == true);
    rt.NoteSeedGateSample(false);
    REQUIRE(rt.ConsumeSeedGateLatch() == false);
}
```

- [ ] **Step 2: Run to verify both fail**

Run: `cd tests && ./run.sh && cd beads && ./run.sh`
Expected: `test_cv_conditioning.cpp` fails to compile (header doesn't exist); the block-runtime cases fail to compile (no latch methods). Red confirmed.

- [ ] **Step 3: Create the conditioning header**

Create `src/particules/particules_cv_conditioning.h`:

```cpp
#pragma once
#include <cmath>
#include <cstddef>

// CV-conditioner settings for the Particules wrapper.
//
// The wrapper steps each beads::ControlConditioner once per wrapper block —
// every sample in VCV (block 1), every 64 samples on MetaModule. These
// helpers convert per-sample-tuned constants into equivalents for the actual
// block size so both platforms condition CVs on the same timescale.
namespace particules {

// Sample-and-hold decimation in conditioner *steps*: targets one CV sample
// per ~8 audio samples. For block sizes >= 8 every step already spans >= 8
// samples, so no further decimation.
constexpr int CvDecimationForBlock(std::size_t block_size) {
    return block_size >= 8 ? 1 : static_cast<int>(8 / block_size);
}

// Convert a per-sample one-pole coefficient into the per-block equivalent:
// applying the result once per block matches applying per_sample every
// sample of that block.
inline float CvSmoothingForBlock(float per_sample, std::size_t block_size) {
    return 1.0f - std::pow(1.0f - per_sample, static_cast<float>(block_size));
}

constexpr float kCvSmoothing        = 0.5f;   // time/size/shape, per sample
constexpr float kPitchCvSmoothing   = 0.35f;  // pitch, per sample
constexpr float kMenuCvQuantizeStep = 0.01f;  // volts; de-noises time/size/shape CVs

// Volts. MUST stay 0: any nonzero step quantizes the 1 V/oct pitch input to
// a grid coarser than a semitone (the old 0.05 V step put notes up to
// ±0.3 semitones out of tune).
constexpr float kPitchCvQuantizeStep = 0.0f;

}  // namespace particules
```

- [ ] **Step 4: Add the seed-gate latch to the block runtime**

In `src/particules/particules_block_runtime.h`, add to the public section:

```cpp
    // Per-sample SEED gate latch. The engine only sees the gate once per
    // block, so a short trigger landing between block-boundary samples would
    // otherwise be lost (~25% of 1 ms triggers at 48 kHz / 64-sample blocks).
    // Note every sample; consume (and clear) once per processed block.
    void NoteSeedGateSample(bool high) { seed_gate_latch_ = seed_gate_latch_ || high; }
    bool ConsumeSeedGateLatch() {
        bool v = seed_gate_latch_;
        seed_gate_latch_ = false;
        return v;
    }
```

and to the private members: `bool seed_gate_latch_ = false;`

- [ ] **Step 5: Wire both into Particules.cpp**

Add the include near the other particules includes (line ~4):

```cpp
#include "particules_cv_conditioning.h"
```

Replace the four conditioner Init calls (lines 169-172):

```cpp
		const int cv_dec = particules::CvDecimationForBlock(kWrapperBlockSize);
		const float cv_smooth =
			particules::CvSmoothingForBlock(particules::kCvSmoothing, kWrapperBlockSize);
		const float pitch_smooth =
			particules::CvSmoothingForBlock(particules::kPitchCvSmoothing, kWrapperBlockSize);
		time_cv_conditioner_.Init(cv_dec, cv_smooth, particules::kMenuCvQuantizeStep, 0.0f);
		size_cv_conditioner_.Init(cv_dec, cv_smooth, particules::kMenuCvQuantizeStep, 0.0f);
		shape_cv_conditioner_.Init(cv_dec, cv_smooth, particules::kMenuCvQuantizeStep, 0.0f);
		pitch_cv_conditioner_.Init(cv_dec, pitch_smooth, particules::kPitchCvQuantizeStep, 0.0f);
```

(This file uses tabs — match them.)

In `process()` (line ~313), right after `bool frozen = ...`, add:

```cpp
		// Latch the SEED gate every sample so short triggers survive the
		// block boundary on MetaModule (updateSlowParams runs once per block).
		block_runtime_.NoteSeedGateSample(inputs[SEED_INPUT].getVoltage() > 1.f);
```

In `updateSlowParams()` (line 293), replace

```cpp
		params_.gate           = inputs[SEED_INPUT].getVoltage() > 1.f;
```

with

```cpp
		params_.gate           = block_runtime_.ConsumeSeedGateLatch();
```

(VCV, block = 1: latch is set and consumed every sample — behavior identical to today.)

- [ ] **Step 6: Run all lanes**

Run: `cd tests && ./run.sh && cd beads && ./run.sh`
Expected: all PASS including the new conditioning test and latch cases.

- [ ] **Step 7: Commit**

```bash
git add src/particules/particules_cv_conditioning.h src/particules/particules_block_runtime.h src/particules/Particules.cpp tests/particules/test_cv_conditioning.cpp tests/beads/test_particules_block_runtime.cpp
git commit -m "fix: unquantized pitch CV, per-sample SEED latch, block-aware CV conditioning"
```

---

### Task 7: Full verification

**Files:** none created; runs builds and all test lanes.

- [ ] **Step 1: Run every test lane from a clean state**

```bash
cd tests && ./run.sh
cd beads && rm -rf build && ./run.sh
cd .. && python3 -m unittest discover -s . -p 'test_*.py' -v
```

Expected: all PASS.

- [ ] **Step 2: Build the VCV plugin**

Run: `make -C vcv -j` (RACK_DIR per the Makefile default; if the SDK path is missing, use the `build-vcv-plugin` skill instead).
Expected: compiles and links with no new warnings in the touched files.

- [ ] **Step 3: Report**

Summarize test counts, the commits made, and any deviations from this plan. Do not merge to main — report back for the merge decision.

---

## Self-review notes

- Spec coverage: spec §1 → Task 1; §2 → Task 2; §3 → Task 3; §4a → Task 5; §4b → Task 4; §5 → Task 6; global constraints → Task 7. ✓
- Type consistency: `NotifyFreeze(bool)` used in Tasks 4 (definition) and 4 Step 3 processor call; `prev_wet_buf`/`prev_wet_len` defined and consumed in Task 3 only; `ProcessBlock` defined in Task 2, edited in Tasks 3 and 4; `commitJitter`/`jitterNext` defined and consumed in Task 5 only; `CvDecimationForBlock`/`CvSmoothingForBlock`/latch methods defined in Task 6 and consumed there. ✓
- Ordering: Tasks 2 → 3 → 4 all edit `beads_processor.cpp` and MUST run in that order. Tasks 1, 5, 6 are independent of them and of each other.
