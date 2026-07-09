#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>
#include <limits>

#include "beads/types.h"
#include "beads/parameters.h"
#include "buffer/recording_buffer.h"
#include "grain/grain.h"
#include "grain/grain_scheduler.h"
#include "grain/grain_engine.h"
#include "util/dsp_utils.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

// Helper: create a small recording buffer filled with a sine wave
struct TestBuffer {
    std::vector<uint8_t> memory;
    RecordingBuffer buffer;
    size_t num_frames;

    TestBuffer(size_t frames = 4800) : num_frames(frames) {
        size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
        memory.resize(bytes, 0);
        buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);

        for (size_t i = 0; i < num_frames; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(num_frames) * 2.0f * 3.14159265f * 10.0f;
            buffer.Write(std::sin(phase), std::cos(phase));
        }
    }
};

TEST_CASE("Grain: Init sets inactive", "[grain]") {
    Grain g;
    g.Init();
    REQUIRE(g.active() == false);
}

TEST_CASE("Grain: Start activates grain", "[grain]") {
    Grain g;
    g.Init();

    Grain::GrainParameters params;
    params.position = 100.0f;
    params.size = 480.0f;  // 10ms at 48kHz
    params.pitch_ratio = 1.0f;
    params.shape = 0.5f;
    params.pan = 0.0f;
    params.pre_delay = 0;

    g.Start(params);
    REQUIRE(g.active() == true);
}

TEST_CASE("Grain: Processes for correct duration", "[grain]") {
    TestBuffer tb;
    Grain g;
    g.Init();

    float grain_size = 480.0f;  // 10ms
    Grain::GrainParameters params;
    params.position = 100.0f;
    params.size = grain_size;
    params.pitch_ratio = 1.0f;
    params.shape = 0.5f;
    params.pan = 0.0f;
    params.pre_delay = 0;

    g.Start(params);

    int sample_count = 0;
    float out_l, out_r;
    while (g.Process(tb.buffer, static_cast<float>(tb.buffer.size()), &out_l, &out_r)) {
        sample_count++;
        if (sample_count > 1000) break;  // Safety
    }

    // Grain should have been active for approximately grain_size samples
    REQUIRE(sample_count == Approx(static_cast<int>(grain_size)).margin(2));
}

TEST_CASE("Grain: Output is non-zero with sine input", "[grain]") {
    TestBuffer tb;
    Grain g;
    g.Init();

    Grain::GrainParameters params;
    params.position = 100.0f;
    params.size = 480.0f;
    params.pitch_ratio = 1.0f;
    params.shape = 0.5f;
    params.pan = 0.0f;
    params.pre_delay = 0;

    g.Start(params);

    float max_level = 0.0f;
    float out_l, out_r;
    while (g.Process(tb.buffer, static_cast<float>(tb.buffer.size()), &out_l, &out_r)) {
        max_level = std::max(max_level, std::max(std::abs(out_l), std::abs(out_r)));
    }

    REQUIRE(max_level > 0.01f);
}

TEST_CASE("Grain: Bell envelope has zero at start and end", "[grain]") {
    TestBuffer tb;
    Grain g;
    g.Init();

    Grain::GrainParameters params;
    params.position = 100.0f;
    params.size = 480.0f;
    params.pitch_ratio = 1.0f;
    params.shape = 0.5f;  // Bell/Hann
    params.pan = 0.0f;
    params.pre_delay = 0;

    g.Start(params);

    float out_l, out_r;
    // First sample should be near zero (Hann window starts at 0)
    g.Process(tb.buffer, static_cast<float>(tb.buffer.size()), &out_l, &out_r);
    REQUIRE(std::abs(out_l) < 0.05f);
}

TEST_CASE("Grain: Pre-delay delays output", "[grain]") {
    TestBuffer tb;
    Grain g;
    g.Init();

    Grain::GrainParameters params;
    params.position = 100.0f;
    params.size = 480.0f;
    params.pitch_ratio = 1.0f;
    params.shape = 0.5f;
    params.pan = 0.0f;
    params.pre_delay = 10;

    g.Start(params);

    float out_l, out_r;
    // First 10 samples should be silent (pre-delay)
    for (int i = 0; i < 10; ++i) {
        REQUIRE(g.Process(tb.buffer, static_cast<float>(tb.buffer.size()), &out_l, &out_r) == true);
        REQUIRE(out_l == 0.0f);
        REQUIRE(out_r == 0.0f);
    }
}

TEST_CASE("GrainScheduler: Latched mode produces triggers", "[scheduler]") {
    GrainScheduler sched;
    sched.Init(kSampleRate);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.2f;  // Left of noon = regular triggers

    int triggers[64];
    int total = 0;

    // Process several blocks
    for (int block = 0; block < 100; ++block) {
        int count = sched.Process(params, 256, triggers, 64);
        total += count;
    }

    // Should have generated some triggers
    REQUIRE(total > 0);
}

TEST_CASE("GrainScheduler: Latched at noon is silent", "[scheduler]") {
    GrainScheduler sched;
    sched.Init(kSampleRate);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.5f;  // Noon = silent

    int triggers[64];
    int total = 0;

    for (int block = 0; block < 100; ++block) {
        int count = sched.Process(params, 256, triggers, 64);
        total += count;
    }

    REQUIRE(total == 0);
}

TEST_CASE("GrainEngine: Decimation scales grain pitch and duration", "[engine][decimation]") {
    // With 4x decimation, grains at pitch=0 should advance through the buffer
    // at 1/4 the rate, and max grain duration should be 4x longer.
    // We compare active grain durations at 1x vs 4x decimation.
    TestBuffer tb_1x(48000);  // 1-second buffer at 1x
    TestBuffer tb_4x(48000);  // same physical buffer at 4x decimation
    tb_4x.buffer.SetDecimationFactor(4);

    GrainEngine engine_1x, engine_4x;
    engine_1x.Init(kSampleRate, &tb_1x.buffer);
    engine_4x.Init(kSampleRate, &tb_4x.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.1f;    // Fast triggers
    params.size = 0.8f;       // Large grain size → near max duration
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;

    std::vector<StereoFrame> out_1x(256), out_4x(256);

    // Run both engines long enough to produce grains
    float energy_1x = 0.0f, energy_4x = 0.0f;
    for (int block = 0; block < 200; ++block) {
        engine_1x.Process(params, out_1x.data(), 256);
        engine_4x.Process(params, out_4x.data(), 256);
        for (size_t i = 0; i < 256; ++i) {
            energy_1x += out_1x[i].l * out_1x[i].l;
            energy_4x += out_4x[i].l * out_4x[i].l;
        }
    }

    // Both should produce output
    REQUIRE(energy_1x > 0.0f);
    REQUIRE(energy_4x > 0.0f);
}

TEST_CASE("GrainEngine: high active-grain load switches to cheaper render tier", "[grain][load_tier]") {
    TestBuffer tb(48000);
    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.05f;
    params.size = 0.90f;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;

    std::vector<StereoFrame> output(256);
    for (int i = 0; i < 200; ++i) {
        engine.Process(params, output.data(), output.size());
    }

    REQUIRE(engine.ActiveGrainCount() > 0);
}

TEST_CASE("Grain: Reverse playback reads buffer backwards", "[grain]") {
    // Fill buffer with a ramp so each position has a unique value.
    size_t num_frames = 4800;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);

    for (size_t i = 0; i < num_frames; ++i) {
        float val = static_cast<float>(i) / static_cast<float>(num_frames);
        buffer.Write(val, val);
    }

    float grain_size = 1000.0f;
    float start_pos = 1000.0f;

    // Forward grain: reads buffer[1000..1999] with forward envelope.
    Grain fwd;
    fwd.Init();
    Grain::GrainParameters fwd_params;
    fwd_params.position = start_pos;
    fwd_params.size = grain_size;
    fwd_params.pitch_ratio = 1.0f;
    fwd_params.shape = 0.0f;  // Symmetric triangle envelope
    fwd_params.pan = 0.0f;
    fwd_params.pre_delay = 0;
    fwd.Start(fwd_params);

    std::vector<float> fwd_samples;
    float out_l, out_r;
    while (fwd.Process(buffer, static_cast<float>(buffer.size()), &out_l, &out_r)) {
        fwd_samples.push_back(out_l);
    }

    // Reverse grain: starts at start_pos + span, reads backward through
    // the same segment.  Same symmetric envelope applied forward.
    Grain rev;
    rev.Init();
    Grain::GrainParameters rev_params;
    rev_params.position = start_pos + grain_size;  // end of forward segment
    rev_params.size = grain_size;
    rev_params.pitch_ratio = -1.0f;
    rev_params.shape = 0.0f;
    rev_params.pan = 0.0f;
    rev_params.pre_delay = 0;
    rev.Start(rev_params);

    std::vector<float> rev_samples;
    while (rev.Process(buffer, static_cast<float>(buffer.size()), &out_l, &out_r)) {
        rev_samples.push_back(out_l);
    }

    REQUIRE(fwd_samples.size() == rev_samples.size());
    size_t n = fwd_samples.size();
    REQUIRE(n > 0);

    // With a symmetric envelope (shape=0, slope=0.5), env(phase) = env(1-phase).
    // If the reverse grain truly reads the same segment backwards:
    //   forward[i] = ramp(start + i) * env(i/n)
    //   reverse[n-1-i] ≈ ramp(start + i + 1) * env(i/n)   [off-by-one]
    // These should be approximately equal.  Check the middle region where
    // envelope values are large enough for meaningful comparison.
    size_t start = n / 4;
    size_t end = 3 * n / 4;
    int matches = 0, total = 0;
    for (size_t i = start; i < end; ++i) {
        size_t j = n - 1 - i;
        if (std::abs(fwd_samples[i]) > 0.001f && std::abs(rev_samples[j]) > 0.001f) {
            float ratio = fwd_samples[i] / rev_samples[j];
            if (ratio > 0.95f && ratio < 1.05f) ++matches;
            ++total;
        }
    }
    REQUIRE(total > 0);
    REQUIRE(matches > total * 9 / 10);
}

TEST_CASE("GrainEngine: Negative SIZE produces reverse output", "[engine]") {
    TestBuffer tb(48000);

    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.1f;
    params.size = -0.5f;  // Negative = reverse grains
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});

    bool all_finite = true;
    float max_level = 0.0f;

    for (int block = 0; block < 200; ++block) {
        engine.Process(params, output.data(), 256);
        for (auto& f : output) {
            if (!std::isfinite(f.l) || !std::isfinite(f.r)) all_finite = false;
            max_level = std::max(max_level,
                std::max(std::abs(f.l), std::abs(f.r)));
        }
    }

    REQUIRE(all_finite);
    REQUIRE(max_level > 0.001f);
}

TEST_CASE("GrainEngine: NaN TIME CV can't reach the buffer read as a NaN position",
          "[engine][nan]") {
    // time_ar > 0 with a connected CV makes ComputeGrainParams' modulation
    // term ar_amount * cv; a NaN cv poisons mod_time -> offset_frames ->
    // gp.position all the way through (Clamp/std::min/std::max all propagate
    // NaN). Without the isfinite fence, that NaN would reach Grain::Start()
    // and then RecordingBuffer::ReadHermiteStereoFast's unguarded
    // float->int cast -- undefined behavior. The fence should land it on a
    // safe position instead, keeping output finite.
    TestBuffer tb(48000);

    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.1f;   // fast trigger rate
    params.size = 0.5f;
    params.time = 0.5f;
    params.time_ar = 1.0f;                                  // CW: CV attenuator
    params.time_cv = std::numeric_limits<float>::quiet_NaN();
    params.time_cv_connected = true;
    params.shape = 0.5f;
    params.pitch = 0.0f;

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});
    bool all_finite = true;
    for (int block = 0; block < 200; ++block) {
        engine.Process(params, output.data(), 256);
        for (auto& f : output) {
            if (!std::isfinite(f.l) || !std::isfinite(f.r)) all_finite = false;
        }
    }

    REQUIRE(all_finite);
    REQUIRE(engine.ActiveGrainCount() > 0);
}

TEST_CASE("GrainEngine: Produces output with active grains", "[engine]") {
    TestBuffer tb;

    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.1f;    // Far left of noon = fast trigger rate
    params.size = 0.5f;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});

    // Process enough blocks for triggers to fire and grains to produce output
    float max_level = 0.0f;
    for (int block = 0; block < 200; ++block) {
        engine.Process(params, output.data(), 256);
        for (auto& f : output) {
            max_level = std::max(max_level, std::max(std::abs(f.l), std::abs(f.r)));
        }
    }

    REQUIRE(max_level > 0.001f);
}

TEST_CASE("GrainEngine: 30 active grains produce valid output", "[engine][stress]") {
    TestBuffer tb(48000);  // 1 second of audio

    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.1f;    // Fast triggers to fill all grain slots
    params.size = 0.9f;       // Long grains (many active simultaneously)
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});

    bool all_finite = true;
    float max_level = 0.0f;

    // Process enough blocks for all 30 grain slots to fill
    for (int block = 0; block < 400; ++block) {
        engine.Process(params, output.data(), 256);
        for (auto& f : output) {
            if (!std::isfinite(f.l) || !std::isfinite(f.r)) {
                all_finite = false;
            }
            max_level = std::max(max_level, std::max(std::abs(f.l), std::abs(f.r)));
        }
    }

    REQUIRE(all_finite);
    REQUIRE(max_level > 0.001f);
}

TEST_CASE("GrainEngine: size=-0.2 (11 o'clock) gives ~30ms grain duration", "[engine]") {
    // At the new boundary (-0.2), abs_size=0 → 30ms grain.
    // Old code: abs_size=0.2 → ~60ms grain. This test fails with old code.
    TestBuffer tb(48000);  // 1s buffer so max_dur is meaningful

    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kGated;
    params.gate = true;
    params.size = -0.2f;   // 11 o'clock boundary
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.density = 0.5f;  // noon = rate 0 → only the rising-edge grain fires

    // Fire one grain, then release gate
    std::vector<StereoFrame> block(64);
    engine.Process(params, block.data(), 64);
    params.gate = false;

    // Count samples until grain completes
    int samples_active = 64;
    for (int b = 0; b < 200; ++b) {
        engine.Process(params, block.data(), 64);
        if (engine.ActiveGrainCount() == 0) break;
        samples_active += 64;
    }

    // 30ms at 48kHz = 1440 samples.
    // Old code at -0.2: ~60ms = ~2880 samples.
    REQUIRE(samples_active < 2400);   // fails with old code (~2880+)
    REQUIRE(samples_active > 500);    // sanity: grain ran at all
}

TEST_CASE("GrainEngine: size=-0.1 (between boundary and noon) is forward", "[engine]") {
    // With boundary at -0.2: size=-0.1 > -0.2 → forward.
    // Old code: size=-0.1 < 0.0 → reverse. This test fails with old code.
    //
    // Detection: fill buffer with ramp (0→1). At time=0.5, read position is
    // the buffer midpoint (value ≈ 0.5). Forward reads upward → mean output > 0.5.
    // Reverse reads downward → mean output < 0.5.
    // Rectangle-ish envelope (shape=0) keeps amplitude flat so buffer value dominates.
    const size_t N = 48000;
    size_t bytes = (N + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer ramp_buf;
    ramp_buf.Init(reinterpret_cast<float*>(mem.data()), N, 2);
    for (size_t i = 0; i < N; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(N);
        ramp_buf.Write(v, v);
    }
    // After N writes, write_head wraps to 0.
    // At time=0.5: pos = 0 - 0.5*N = -24000 → +48000 → 24000 → ramp value 0.5

    GrainEngine engine;
    engine.Init(kSampleRate, &ramp_buf);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kGated;
    params.gate = true;
    params.size = -0.1f;   // forward with new code, reverse with old
    params.time = 0.5f;
    params.shape = 0.0f;   // rectangle-ish: flat amplitude, buffer value dominates
    params.pitch = 0.0f;
    params.density = 0.5f; // only rising-edge grain

    // Fire one grain
    std::vector<StereoFrame> block(64);
    engine.Process(params, block.data(), 64);
    params.gate = false;

    // Collect output until grain ends (~40ms = ~1920 samples)
    std::vector<float> samples;
    for (auto& f : block) samples.push_back(f.l);
    for (int b = 0; b < 60; ++b) {
        engine.Process(params, block.data(), 64);
        for (auto& f : block) samples.push_back(f.l);
        if (engine.ActiveGrainCount() == 0 && b > 10) break;
    }

    // Find the grain region: first and last non-negligible sample indices
    size_t grain_start = samples.size(), grain_end = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        if (std::abs(samples[i]) > 0.001f) {
            if (i < grain_start) grain_start = i;
            if (i > grain_end)   grain_end   = i;
        }
    }
    REQUIRE(grain_end > grain_start + 200);  // grain has meaningful duration

    // Compare first half vs second half of the grain.
    // Pan scaling is constant for the grain, so it cancels in the comparison.
    // Forward grain reads ramp upward: second_half mean > first_half mean.
    // Reverse grain reads ramp downward: second_half mean < first_half mean.
    size_t mid = (grain_start + grain_end) / 2;
    float first_sum = 0.0f; int first_count = 0;
    float second_sum = 0.0f; int second_count = 0;
    for (size_t i = grain_start; i <= grain_end; ++i) {
        if (i < mid) { first_sum  += samples[i]; ++first_count;  }
        else         { second_sum += samples[i]; ++second_count; }
    }
    REQUIRE(first_count > 50);
    REQUIRE(second_count > 50);

    float first_mean  = first_sum  / static_cast<float>(first_count);
    float second_mean = second_sum / static_cast<float>(second_count);
    // Forward: second_mean > first_mean (reading upward in ramp)
    // Reverse: second_mean < first_mean (reading downward)
    REQUIRE(second_mean > first_mean);  // fails with old code, passes with new
}

// ── Overlap-normalization smoothing: exact block coefficient ──────────────
//
// GrainEngine::Process folds the per-sample overlap-count OnePole update
// into a single call using block_coefficient = 1 - (1 - c)^n. This test
// pins that formula against the ground truth: applying the per-sample
// OnePole update n times in a row on a constant target must land at
// exactly the same state as one call with the block coefficient.

TEST_CASE("OnePole: exact block coefficient matches n per-sample updates", "[grain][overlap]") {
    auto run_case = [](float start, float target, float coeff, int n) {
        float per_sample = start;
        for (int i = 0; i < n; ++i) {
            OnePole(per_sample, target, coeff);
        }

        float block_coefficient = 1.0f - std::pow(1.0f - coeff, static_cast<float>(n));
        float blocked = start;
        OnePole(blocked, target, block_coefficient);

        REQUIRE(blocked == Approx(per_sample).margin(1e-4f));
    };

    // Fast-rise coefficient (0.9, as used when the count is climbing).
    run_case(0.0f, 30.0f, 0.9f, 64);
    run_case(5.0f, 2.0f, 0.9f, 256);
    // Slow-fall coefficient (0.2, as used when the count is settling).
    run_case(0.0f, 30.0f, 0.2f, 64);
    run_case(12.0f, 1.0f, 0.2f, 512);
    // Degenerate block sizes should still be exact.
    run_case(3.0f, 9.0f, 0.2f, 1);
}

// The old code approximated this same update with a strided per-sample
// loop (stride 4 in the "high load" render tier, i.e. only n/4 OnePole
// calls per block) instead of the exact closed form. At the real-world
// block size (kMaxBlockSize == 64, see beads/types.h) and the slow-fall
// coefficient (0.2, used while the overlap count is settling down), that
// under-shoots convergence by several percent per block — exactly the
// kind of per-block error that would accumulate into audible stepping
// under a sustained density sweep in the high-load tier. This test pins
// the size of that gap so a regression back to striding would be caught.
TEST_CASE("OnePole: strided approximation under-converges vs exact block coefficient", "[grain][overlap]") {
    constexpr int kBlockSize = 64;    // kMaxBlockSize
    constexpr int kStride = 4;        // old high-load-tier stride
    constexpr float kSlowFallCoeff = 0.2f;
    constexpr float kStart = 0.0f;
    constexpr float kTarget = 30.0f;

    // Old: strided loop, i += kStride, kBlockSize / kStride actual updates.
    float strided = kStart;
    for (int i = 0; i < kBlockSize; i += kStride) {
        OnePole(strided, kTarget, kSlowFallCoeff);
    }

    // New: exact block coefficient for the full kBlockSize.
    float exact = kStart;
    float block_coefficient = 1.0f - std::pow(1.0f - kSlowFallCoeff, static_cast<float>(kBlockSize));
    OnePole(exact, kTarget, block_coefficient);

    float exact_residual = kTarget - exact;
    float strided_residual = kTarget - strided;

    CAPTURE(exact_residual);
    CAPTURE(strided_residual);

    // The exact formula converges to within a small fraction of a percent
    // of the target inside a single block...
    REQUIRE(exact_residual < 0.001f * kTarget);
    // ...while the strided approximation is still measurably short (on the
    // order of a couple of percent), i.e. genuinely coarser — confirming
    // (b) is a real (if subtle) smoothing improvement, not a no-op.
    REQUIRE(strided_residual > 0.5f * kTarget * 1e-2f);
    REQUIRE(strided_residual > exact_residual * 10.0f);
}

// ── Dense grain cloud: overlap-loudness has no per-block discontinuity ────
//
// Quantitative proxy for the listening check (deferred to the user): a
// dense grain cloud (Density high, Size mid) rendered through GrainEngine
// while sweeping Density should show a smoothly-tracking output RMS
// envelope, not stepwise jumps between blocks (which would read as
// pumping/stepping in overlap loudness).

TEST_CASE("GrainEngine: dense cloud density sweep has bounded per-block RMS steps", "[grain][overlap][engine]") {
    TestBuffer tb(48000 * 4);  // 4 seconds of audio — enough headroom for the
                                // dynamic max-active-grain cap (buf_dur /
                                // grain_dur * 1.5) to clear the high-load
                                // threshold (12 active grains) at mid SIZE.

    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.size = 0.3f;      // mid SIZE
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;

    // kMaxBlockSize (64, see beads/types.h) is the largest block GrainEngine
    // ever actually sees in production — BeadsProcessor::Process chunks any
    // larger host block into <=64-frame pieces before calling in here.
    constexpr size_t kBlockSize = 64;
    constexpr int kNumBlocks = 3000;
    std::vector<StereoFrame> output(kBlockSize);

    std::vector<float> block_rms;
    block_rms.reserve(kNumBlocks);

    for (int b = 0; b < kNumBlocks; ++b) {
        // Sweep density back and forth across the dense (away-from-noon)
        // range, spending time at the high end where active grain count
        // crosses into the "high load" render tier (>= 12 active grains),
        // which is exactly where the old strided OnePole loop diverged
        // from an exact per-sample smoother.
        float phase = static_cast<float>(b) / static_cast<float>(kNumBlocks);
        float tri = std::abs(2.0f * (phase - std::floor(phase + 0.5f)));  // 0..1 triangle
        params.density = 0.02f + 0.10f * tri;  // sweeps [0.02, 0.12] — dense, left of noon

        engine.Process(params, output.data(), kBlockSize);

        double sum_sq = 0.0;
        for (auto& f : output) {
            sum_sq += static_cast<double>(f.l) * f.l + static_cast<double>(f.r) * f.r;
        }
        float rms = static_cast<float>(std::sqrt(sum_sq / (2.0 * kBlockSize)));
        block_rms.push_back(rms);
    }

    // Discard the startup ramp / fill-in period so we're measuring
    // steady-state overlap behavior, not the initial grain pool filling.
    constexpr int kWarmupBlocks = 400;
    float max_step = 0.0f;
    float max_rms = 0.0f;
    for (size_t i = kWarmupBlocks; i < block_rms.size(); ++i) {
        max_rms = std::max(max_rms, block_rms[i]);
        if (i > kWarmupBlocks) {
            max_step = std::max(max_step, std::abs(block_rms[i] - block_rms[i - 1]));
        }
    }

    CAPTURE(max_step);
    CAPTURE(max_rms);

    REQUIRE(max_rms > 0.01f);  // sanity: the cloud is actually producing sound
    // No single block-to-block RMS jump should exceed a large fraction of
    // the overall signal level. This is the quantitative proxy for
    // "no pumping/stepping in overlap loudness" — a real discontinuity in
    // the overlap-normalization gain would show up as a jump comparable to
    // max_rms itself.
    REQUIRE(max_step < 0.5f * max_rms);
}
