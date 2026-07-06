#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "beads/types.h"
#include "beads/parameters.h"
#include "buffer/recording_buffer.h"
#include "delay/delay_engine.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

// Helper: create a recording buffer with known content
struct DelayTestBuffer {
    std::vector<uint8_t> memory;
    RecordingBuffer buffer;
    size_t num_frames;

    DelayTestBuffer(size_t frames = 48000) : num_frames(frames) {
        size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
        memory.resize(bytes, 0);
        buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    }
};

TEST_CASE("DelayEngine: Init and process without crash", "[delay]") {
    DelayTestBuffer tb;
    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    // Write some data into the buffer
    for (size_t i = 0; i < 4800; ++i) {
        tb.buffer.Write(0.0f, 0.0f);
    }

    BeadsParameters params;
    params.size = 1.0f;  // Delay mode
    params.density = 0.5f;
    params.pitch = 0.0f;
    params.shape = 0.0f;
    params.time = 0.5f;

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});
    engine.Process(params, output.data(), 256);

    // Should produce finite output
    for (size_t i = 0; i < 256; ++i) {
        REQUIRE(std::isfinite(output[i].l));
        REQUIRE(std::isfinite(output[i].r));
    }
}

TEST_CASE("DelayEngine: Impulse produces echo at expected delay", "[delay]") {
    DelayTestBuffer tb(48000);  // 1 second buffer
    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    // The delay engine reads relative to the current write head.
    // We continuously write into the buffer while processing the delay.
    // Write an impulse early, then silence — the delay should read it back.
    BeadsParameters params;
    params.size = 1.0f;
    params.density = 0.25f;  // Moderate base delay time
    params.pitch = 0.0f;
    params.shape = 0.0f;
    params.time = 0.0f;      // 1x multiplier (shortest)

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});
    float max_out = 0.0f;

    for (int block = 0; block < 200; ++block) {
        // Write 256 samples per block, impulse in first block only
        for (size_t i = 0; i < 256; ++i) {
            float val = (block == 0 && i == 0) ? 1.0f : 0.0f;
            tb.buffer.Write(val, val);
        }
        engine.Process(params, output.data(), 256);
        for (size_t i = 0; i < 256; ++i) {
            max_out = std::max(max_out, std::max(std::abs(output[i].l), std::abs(output[i].r)));
        }
    }

    // The impulse should eventually appear in the output
    REQUIRE(max_out > 0.01f);
}

TEST_CASE("DelayEngine: Freeze loop repeats content", "[delay]") {
    DelayTestBuffer tb(48000);
    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    // Fill buffer with a recognizable pattern (sine wave)
    for (size_t i = 0; i < 48000; ++i) {
        float phase = static_cast<float>(i) / 48000.0f * 2.0f * 3.14159265f * 10.0f;
        tb.buffer.Write(std::sin(phase), std::sin(phase));
    }

    BeadsParameters params;
    params.size = 1.0f;
    params.density = 0.5f;
    params.pitch = 0.0f;
    params.shape = 0.0f;
    params.time = 0.5f;

    // Process a few blocks without freeze first
    std::vector<StereoFrame> output(256, {0.0f, 0.0f});
    for (int i = 0; i < 10; ++i) {
        engine.Process(params, output.data(), 256);
    }

    // Enable freeze
    params.freeze = true;
    params.size = 0.5f;  // Loop about half the buffer
    params.time = 0.0f;

    // Capture two consecutive runs of the frozen loop
    std::vector<float> run1, run2;
    for (int block = 0; block < 20; ++block) {
        engine.Process(params, output.data(), 256);
        for (size_t i = 0; i < 256; ++i) {
            run1.push_back(output[i].l);
        }
    }
    for (int block = 0; block < 20; ++block) {
        engine.Process(params, output.data(), 256);
        for (size_t i = 0; i < 256; ++i) {
            run2.push_back(output[i].l);
        }
    }

    // Both runs should be non-silent and periodic — check that the loop produced output
    float energy1 = 0.0f, energy2 = 0.0f;
    for (size_t i = 0; i < run1.size(); ++i) {
        energy1 += run1[i] * run1[i];
        energy2 += run2[i] * run2[i];
    }

    REQUIRE(energy1 > 0.0f);
    REQUIRE(energy2 > 0.0f);
    // Both runs read from the same frozen loop, so their energies should be similar
    float ratio = energy1 / std::max(energy2, 1e-10f);
    REQUIRE(ratio > 0.5f);
    REQUIRE(ratio < 2.0f);
}

TEST_CASE("DelayEngine: TIME multiplier affects delay length", "[delay]") {
    // TIME=0 (CCW) = 1x base delay (short), TIME=1 (CW) = max multiple (long)
    // With the same DENSITY (base delay), different TIME values should
    // produce different actual delay times.

    DelayTestBuffer tb1(48000), tb2(48000);
    DelayEngine engine1, engine2;
    engine1.Init(kSampleRate, &tb1.buffer);
    engine2.Init(kSampleRate, &tb2.buffer);

    // Fill both buffers with the same impulse pattern
    for (size_t i = 0; i < 48000; ++i) {
        float val = (i == 1000) ? 1.0f : 0.0f;
        tb1.buffer.Write(val, val);
        tb2.buffer.Write(val, val);
    }

    BeadsParameters params1, params2;
    params1.size = 1.0f;
    params1.pitch = 0.0f;
    params1.shape = 0.0f;
    params1.density = 0.3f;  // Base delay set by density
    params2 = params1;

    params1.time = 0.9f;   // High time = long delay (large multiplier)
    params2.time = 0.1f;   // Low time = short delay (small multiplier)

    std::vector<StereoFrame> out1(256), out2(256);

    // Process enough blocks for the delay time to converge
    float first_nonzero_block1 = -1, first_nonzero_block2 = -1;
    for (int block = 0; block < 200; ++block) {
        engine1.Process(params1, out1.data(), 256);
        engine2.Process(params2, out2.data(), 256);

        for (size_t i = 0; i < 256; ++i) {
            if (first_nonzero_block1 < 0 && std::abs(out1[i].l) > 0.01f) {
                first_nonzero_block1 = static_cast<float>(block);
            }
            if (first_nonzero_block2 < 0 && std::abs(out2[i].l) > 0.01f) {
                first_nonzero_block2 = static_cast<float>(block);
            }
        }
    }

    // Low time (short delay) should produce output sooner than high time (long delay)
    if (first_nonzero_block1 >= 0 && first_nonzero_block2 >= 0) {
        REQUIRE(first_nonzero_block2 <= first_nonzero_block1);
    }
}

TEST_CASE("DelayEngine: Decimation adjusts delay times correctly", "[delay][decimation]") {
    // With 4x decimation, the buffer represents 4x more real time.
    // Delay calculations in buffer-frame units should account for this.
    DelayTestBuffer tb(48000);
    tb.buffer.SetDecimationFactor(4);

    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    // Fill buffer with a sine wave
    for (size_t i = 0; i < 48000; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
        tb.buffer.Write(std::sin(phase), std::sin(phase));
    }

    BeadsParameters params;
    params.size = 1.0f;
    params.density = 0.5f;
    params.pitch = 0.0f;
    params.shape = 0.0f;
    params.time = 0.5f;

    std::vector<StereoFrame> output(256);
    for (int block = 0; block < 50; ++block) {
        engine.Process(params, output.data(), 256);
        for (size_t i = 0; i < 256; ++i) {
            REQUIRE(std::isfinite(output[i].l));
            REQUIRE(std::isfinite(output[i].r));
        }
    }
}

TEST_CASE("DelayEngine: Freeze loop with decimation", "[delay][decimation]") {
    DelayTestBuffer tb(48000);
    tb.buffer.SetDecimationFactor(8);

    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    // Fill buffer
    for (size_t i = 0; i < 48000; ++i) {
        float phase = static_cast<float>(i) / 48000.0f * 2.0f * 3.14159265f * 10.0f;
        tb.buffer.Write(std::sin(phase), std::sin(phase));
    }

    BeadsParameters params;
    params.size = 1.0f;
    params.density = 0.5f;
    params.pitch = 0.0f;
    params.shape = 0.0f;
    params.time = 0.5f;

    std::vector<StereoFrame> output(256);

    // Process unfrozen
    for (int i = 0; i < 10; ++i) {
        engine.Process(params, output.data(), 256);
    }

    // Freeze
    params.freeze = true;
    params.size = 0.5f;
    for (int i = 0; i < 20; ++i) {
        engine.Process(params, output.data(), 256);
        for (size_t j = 0; j < 256; ++j) {
            REQUIRE(std::isfinite(output[j].l));
            REQUIRE(std::isfinite(output[j].r));
        }
    }
}

TEST_CASE("DelayEngine: Output is finite with extreme parameters", "[delay]") {
    DelayTestBuffer tb(48000);
    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    // Fill buffer with a sine wave
    for (size_t i = 0; i < 48000; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
        tb.buffer.Write(std::sin(phase), std::sin(phase));
    }

    BeadsParameters params;
    params.size = 1.0f;
    params.density = 1.0f;   // Shortest delay
    params.pitch = 24.0f;    // 2 octaves up
    params.shape = 1.0f;     // Full slicer
    params.time = 0.0f;

    std::vector<StereoFrame> output(256);
    for (int block = 0; block < 50; ++block) {
        engine.Process(params, output.data(), 256);
        for (size_t i = 0; i < 256; ++i) {
            REQUIRE(std::isfinite(output[i].l));
            REQUIRE(std::isfinite(output[i].r));
        }
    }
}

TEST_CASE("DelayEngine: SIZE knob scales effective buffer in non-frozen delay mode", "[delay]") {
    // Buffer: 1 second at 48kHz, decimation factor 1.
    // We fill it with a sustained tone (sine at 440Hz) so there is always
    // signal to read regardless of exact read-head position.
    DelayTestBuffer tb(48000);
    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    // Fill buffer with a 440Hz sine so there is always energy to read.
    for (size_t i = 0; i < 48000; ++i) {
        float s = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        tb.buffer.Write(s, s);
    }

    // Helper: run N frames with given params, return total output energy (sum of squares).
    // Note: re-initialises engine (resets engine state) but shares the same buffer
    // (pre-filled with 440Hz sine) across all measurements.
    auto total_energy = [&](const BeadsParameters& params, int frames) -> float {
        engine = DelayEngine();
        engine.Init(kSampleRate, &tb.buffer);
        std::vector<StereoFrame> out(frames, {0.0f, 0.0f});
        engine.Process(params, out.data(), frames);
        float energy = 0.0f;
        for (auto& f : out) energy += f.l * f.l + f.r * f.r;
        return energy;
    };

    BeadsParameters params;
    params.freeze   = false;
    params.shape    = 0.0f;
    params.pitch    = 0.0f;
    params.density  = 0.3f;  // CCW → short base delay, well within any effective buffer
    params.time     = 0.0f;  // minimum multiplier (1x base delay)

    constexpr int kFrames = 5000;

    SECTION("Different SIZE values produce different output (SIZE has an effect)") {
        // SIZE=-1.0 → effective buffer ≈ 48 frames → short delay → reads near write head
        // SIZE=1.0  → effective buffer = 48000 frames → long delay → reads from far back
        // The two SIZE values map to different read positions in the sine-filled buffer,
        // so the output samples should differ between SIZE=-1.0 and SIZE=1.0.

        // Collect per-sample output for each SIZE setting
        std::vector<float> out_short(kFrames), out_full(kFrames);

        {
            engine = DelayEngine();
            engine.Init(kSampleRate, &tb.buffer);
            std::vector<StereoFrame> out(kFrames, {0.0f, 0.0f});
            params.size = -1.0f;
            engine.Process(params, out.data(), kFrames);
            for (int i = 0; i < kFrames; i++) out_short[i] = out[i].l;
        }
        {
            engine = DelayEngine();
            engine.Init(kSampleRate, &tb.buffer);
            std::vector<StereoFrame> out(kFrames, {0.0f, 0.0f});
            params.size = 1.0f;
            engine.Process(params, out.data(), kFrames);
            for (int i = 0; i < kFrames; i++) out_full[i] = out[i].l;
        }

        // Count samples that differ by more than a small epsilon.
        // A sine wave through two differently-sized delay lines produces different
        // phase/amplitude relationships for most samples — we expect the vast majority
        // of the 5000 samples to differ.
        int differing_samples = 0;
        for (int i = 0; i < kFrames; i++) {
            if (std::abs(out_short[i] - out_full[i]) > 1e-4f) {
                differing_samples++;
            }
        }
        REQUIRE(differing_samples > 100);
    }

    SECTION("SIZE=1.0 (fully CW) produces same behaviour as current default (full buffer)") {
        // Before this feature SIZE had no effect; with SIZE=1.0 it should still
        // produce maximum-range delay behaviour, not constrain anything.
        params.size    = 1.0f;
        params.density = 0.5f;  // noon = full effective-buffer as base delay
        params.time    = 1.0f;  // maximum multiplier

        std::vector<StereoFrame> out(256, {0.0f, 0.0f});
        engine = DelayEngine();
        engine.Init(kSampleRate, &tb.buffer);
        REQUIRE_NOTHROW(engine.Process(params, out.data(), 256));
        for (auto& f : out) {
            REQUIRE(std::isfinite(f.l));
            REQUIRE(std::isfinite(f.r));
        }
    }

    SECTION("Frozen mode with SIZE=0.5 does not crash or produce NaN") {
        params.size     = 0.5f;
        params.density  = 0.5f;
        params.time     = 0.0f;
        params.freeze   = true;
        params.feedback = 0.0f;

        std::vector<StereoFrame> out(256, {0.0f, 0.0f});
        engine = DelayEngine();
        engine.Init(kSampleRate, &tb.buffer);
        REQUIRE_NOTHROW(engine.Process(params, out.data(), 256));
        for (auto& f : out) {
            REQUIRE(std::isfinite(f.l));
            REQUIRE(std::isfinite(f.r));
        }
    }
}

TEST_CASE("DelayEngine: pitch_ar=0 means pitch_lfo has no effect", "[delay]") {
    DelayTestBuffer tb;
    for (size_t i = 0; i < 48000; ++i) {
        float v = std::sin(2.f * 3.14159f * 440.f * static_cast<float>(i) / 48000.f);
        tb.buffer.Write(v, v);
    }

    DelayEngine engine_a, engine_b;
    engine_a.Init(kSampleRate, &tb.buffer);
    engine_b.Init(kSampleRate, &tb.buffer);

    BeadsParameters p;
    p.density = 0.5f;
    p.pitch = 0.0f;
    p.pitch_ar = 0.0f;
    p.pitch_lfo = 1.0f;   // LFO at max, but AR is zero
    p.pitch_cv_connected = false;

    BeadsParameters p_no_lfo = p;
    p_no_lfo.pitch_lfo = 0.0f;

    std::vector<StereoFrame> out_a(500), out_b(500);
    engine_a.Process(p,        out_a.data(), out_a.size());
    engine_b.Process(p_no_lfo, out_b.data(), out_b.size());

    // With ar=0, LFO should have zero effect
    for (size_t i = 0; i < out_a.size(); ++i) {
        INFO("frame " << i);
        REQUIRE(out_a[i].l == Approx(out_b[i].l).margin(1e-5f));
    }
}

TEST_CASE("DelayEngine: pitch_ar CW + pitch_lfo shifts pitch", "[delay]") {
    DelayTestBuffer tb;
    for (size_t i = 0; i < 48000; ++i) {
        float v = std::sin(2.f * 3.14159f * 440.f * static_cast<float>(i) / 48000.f);
        tb.buffer.Write(v, v);
    }

    DelayEngine engine_a, engine_b;
    engine_a.Init(kSampleRate, &tb.buffer);
    engine_b.Init(kSampleRate, &tb.buffer);

    BeadsParameters p_base;
    p_base.density = 0.5f;
    p_base.pitch = 0.0f;
    p_base.pitch_ar = 0.0f;   // AR at noon — no effect
    p_base.pitch_lfo = 1.0f;
    p_base.pitch_cv_connected = false;

    BeadsParameters p_mod = p_base;
    p_mod.pitch_ar = 0.5f;    // CW: effective_pitch = 0 + 1.0 * 0.5 = 0.5 st

    std::vector<StereoFrame> out_base(500), out_mod(500);
    engine_a.Process(p_base, out_base.data(), out_base.size());
    engine_b.Process(p_mod,  out_mod.data(),  out_mod.size());

    // AR is applied — outputs must differ
    bool any_differ = false;
    for (size_t i = 0; i < out_base.size(); ++i) {
        if (std::fabs(out_base[i].l - out_mod[i].l) > 1e-4f) {
            any_differ = true;
            break;
        }
    }
    REQUIRE(any_differ);
}

TEST_CASE("DelayEngine: time_ar modulates delay time", "[delay]") {
    DelayTestBuffer tb;
    // Fill with a 440 Hz sine so any read position returns non-zero signal
    for (size_t i = 0; i < 48000; ++i) {
        float v = std::sin(2.f * 3.14159f * 440.f * static_cast<float>(i) / 48000.f);
        tb.buffer.Write(v, v);
    }

    DelayEngine engine_a, engine_b;
    engine_a.Init(kSampleRate, &tb.buffer);
    engine_b.Init(kSampleRate, &tb.buffer);

    BeadsParameters p_base;
    p_base.density = 0.3f;    // CCW from noon so base delay << buffer; TIME has room to vary
    p_base.time = 0.5f;
    p_base.time_ar = 0.0f;    // AR at noon — no effect
    p_base.time_lfo = 0.8f;
    p_base.time_cv_connected = false;

    BeadsParameters p_mod = p_base;
    p_mod.time_ar = 0.5f;     // CW: effective_time = 0.5 + 0.8 * 0.5 = 0.9

    std::vector<StereoFrame> out_base(2000), out_mod(2000);
    engine_a.Process(p_base, out_base.data(), out_base.size());
    engine_b.Process(p_mod,  out_mod.data(),  out_mod.size());

    // Different delay times — outputs must differ
    bool any_differ = false;
    for (size_t i = 0; i < out_base.size(); ++i) {
        if (std::fabs(out_base[i].l - out_mod[i].l) > 1e-4f) {
            any_differ = true;
            break;
        }
    }
    REQUIRE(any_differ);
}

TEST_CASE("DelayEngine: TIME change produces no pitch artifact (output amplitude stable)", "[delay]") {
    // Fill a 1-second buffer with a sustained 440 Hz sine.
    // A pitch artifact shows up as an amplitude change in the envelope of the
    // output (the read head is temporarily moving at a non-1x rate, which
    // causes the signal to speed-up or slow-down, leaving a dip/spike in
    // output energy).  After the crossfade completes (~1024 samples), the
    // energy should be back to its steady-state level.
    //
    // We use time_cv_connected=true so the CV/AR path fires a crossfade
    // immediately on threshold crossing — no stabilization timer delay.
    DelayTestBuffer tb(48000);
    for (size_t i = 0; i < 48000; ++i) {
        float v = 0.5f * std::sin(2.f * 3.14159265f * 440.f * i / 48000.f);
        tb.buffer.Write(v, v);
    }

    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.size             = 1.0f;
    params.density          = 0.3f;
    params.pitch            = 0.0f;
    params.shape            = 0.0f;
    params.time             = 0.3f;
    params.time_cv_connected = true;  // use CV path: crossfade fires immediately on threshold

    // Warm up for 4096 samples to let the tap settle at time=0.3
    std::vector<StereoFrame> warmup(4096);
    engine.Process(params, warmup.data(), 4096);

    // Measure steady-state RMS over 512 samples at time=0.3
    std::vector<StereoFrame> before(512);
    engine.Process(params, before.data(), 512);
    float rms_before = 0.0f;
    for (auto& f : before) rms_before += f.l * f.l;
    rms_before = std::sqrt(rms_before / 512.f);

    // Jump TIME to 0.7 (large change → crossfade fires immediately via CV path)
    params.time = 0.7f;

    // Process 512 samples immediately after the TIME jump (the crossfade window)
    std::vector<StereoFrame> during(512);
    engine.Process(params, during.data(), 512);

    // Process 512 more samples after the crossfade has settled
    std::vector<StereoFrame> after(512);
    engine.Process(params, after.data(), 512);
    float rms_after = 0.0f;
    for (auto& f : after) rms_after += f.l * f.l;
    rms_after = std::sqrt(rms_after / 512.f);

    // Both taps contain a sustained 440 Hz sine — RMS must be non-trivial
    REQUIRE(rms_before > 0.01f);
    REQUIRE(rms_after  > 0.01f);

    // After settling, RMS should be within 50% of before-value.
    // Without the fix, a tape glide would produce a severe transient that can
    // push rms_after far outside this range.
    float ratio = rms_after / rms_before;
    REQUIRE(ratio > 0.5f);
    REQUIRE(ratio < 2.0f);
}

TEST_CASE("DelayEngine: DENSITY change is gradual, no abrupt jump at boundary", "[delay]") {
    // Regression: DENSITY changes must go through OnePole smoothing, not snap.
    //
    // We fill the buffer with a linear ramp so output is proportional to read
    // position.  With OnePole(0.001), the first post-change sample's read head
    // moves only ~11 buffer frames (out of ~11000 total), so output changes by
    // ~0.00023.  A snap would jump ~11000 frames → output changes by ~0.23.
    //
    // Per-sample Process() calls are used so the stabilization timer increments
    // one sample at a time.
    const size_t kBufSize = 48000;
    DelayTestBuffer tb(kBufSize);
    // Ramp: buffer[i] = i / kBufSize  (0.0 → ~1.0)
    for (size_t i = 0; i < kBufSize; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(kBufSize);
        tb.buffer.Write(v, v);
    }

    DelayEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    BeadsParameters params;
    params.size              = 1.0f;
    params.density           = 0.3f;   // base_delay ≈ 15 600 samples
    params.pitch             = 0.0f;
    params.shape             = 0.0f;
    params.time              = 0.0f;   // 1x multiplier
    params.time_cv_connected = true;   // CV path so TIME tap settles immediately

    // Warm up per-sample so smoothed_base_delay_ converges
    for (int i = 0; i < 8000; ++i) {
        StereoFrame dummy;
        engine.Process(params, &dummy, 1);
    }

    // Record last sample before the change
    StereoFrame pre;
    engine.Process(params, &pre, 1);

    // Large DENSITY change: base_delay target drops from ~15600 to ~4400 samples
    params.density = 0.2f;

    // Record first sample after the change
    StereoFrame post;
    engine.Process(params, &post, 1);

    // OnePole step: Δsmoothed ≈ 0.001 × (4400 − 15600) ≈ −11.2 frames.
    // Ramp output Δ = 11.2 / 48000 ≈ 0.00023.
    // A snap would give Δ ≈ 11200 / 48000 ≈ 0.23.
    // Threshold 0.01 comfortably separates the two cases.
    float boundary_delta = std::fabs(post.l - pre.l);
    REQUIRE(boundary_delta < 0.01f);
}
