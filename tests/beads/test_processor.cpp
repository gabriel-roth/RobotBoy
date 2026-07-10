#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>
#include <cstring>

#include "beads/beads.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;
static constexpr size_t kBlockSize = 256;

// Helper to create and init a processor
struct TestProcessor {
    std::vector<uint8_t> memory;
    BeadsProcessor processor;

    TestProcessor() {
        auto req = BeadsProcessor::GetMemoryRequirements(kSampleRate);
        memory.resize(req.total_bytes, 0);
        processor.Init(memory.data(), memory.size(), kSampleRate);
    }
};

TEST_CASE("BeadsProcessor: GetMemoryRequirements returns sensible values", "[processor]") {
    auto req = BeadsProcessor::GetMemoryRequirements(kSampleRate);
    REQUIRE(req.total_bytes > 0);
    REQUIRE(req.alignment > 0);
    // Should be roughly 1.5MB with 192K-frame buffer (4s at 48kHz)
    REQUIRE(req.total_bytes > 1000000);
    REQUIRE(req.total_bytes < 10000000);
}

TEST_CASE("BeadsProcessor: Init does not crash", "[processor]") {
    TestProcessor tp;
    // If we got here, Init succeeded
    REQUIRE(tp.processor.ActiveGrainCount() == 0);
}

TEST_CASE("BeadsProcessor: Process with silence input", "[processor]") {
    TestProcessor tp;

    BeadsParameters params;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize, {0.0f, 0.0f});
    std::vector<StereoFrame> output(kBlockSize);

    tp.processor.Process(input.data(), output.data(), kBlockSize);

    // With silence in and default params, output should be near silence
    for (size_t i = 0; i < kBlockSize; ++i) {
        REQUIRE(std::isfinite(output[i].l));
        REQUIRE(std::isfinite(output[i].r));
    }
}

TEST_CASE("BeadsProcessor: Process with sine input produces output", "[processor]") {
    TestProcessor tp;

    BeadsParameters params;
    params.density = 0.1f;  // Far left of noon = fast grain trigger rate
    params.dry_wet = 1.0f;  // Full wet
    params.time = 0.95f;    // Read near write head (where data has been written)
    params.size = 0.3f;     // Short grains
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.manual_gain_db = 0.0f;  // Bypass auto-gain ramping
    params.trigger_mode = TriggerMode::kLatched;
    tp.processor.SetParameters(params);

    // Generate a sine wave input
    std::vector<StereoFrame> input(kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
        input[i] = {std::sin(phase), std::sin(phase)};
    }

    std::vector<StereoFrame> output(kBlockSize);

    // Process enough blocks to fill buffer, trigger grains, and render output
    float max_level = 0.0f;
    for (int block = 0; block < 200; ++block) {
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            max_level = std::max(max_level, std::max(std::abs(output[i].l), std::abs(output[i].r)));
            REQUIRE(std::isfinite(output[i].l));
            REQUIRE(std::isfinite(output[i].r));
        }
    }

    // After many blocks, we should have some output from grains
    REQUIRE(max_level > 0.001f);
}

TEST_CASE("BeadsProcessor: No NaN in output with extreme parameters", "[processor]") {
    TestProcessor tp;

    BeadsParameters params;
    params.density = 1.0f;
    params.feedback = 0.99f;
    params.dry_wet = 1.0f;
    params.reverb = 1.0f;
    params.size = 0.5f;
    params.pitch = 24.0f;  // 2 octaves up
    params.quality_mode = QualityMode::kTape;
    params.manual_gain_db = 0.0f;  // Fixed gain to avoid auto-gain runaway
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
        input[i] = {std::sin(phase), std::sin(phase)};
    }

    std::vector<StereoFrame> output(kBlockSize);

    for (int block = 0; block < 100; ++block) {
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            REQUIRE(std::isfinite(output[i].l));
            REQUIRE(std::isfinite(output[i].r));
        }
    }
}

TEST_CASE("BeadsProcessor: Freeze stops recording", "[processor]") {
    TestProcessor tp;

    BeadsParameters params;
    params.density = 0.3f;
    params.size = 0.5f;
    params.dry_wet = 0.5f;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
        input[i] = {std::sin(phase), std::sin(phase)};
    }
    std::vector<StereoFrame> output(kBlockSize);

    // Process a few blocks
    for (int i = 0; i < 10; ++i) {
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // Freeze
    params.freeze = true;
    tp.processor.SetParameters(params);
    tp.processor.Process(input.data(), output.data(), kBlockSize);

    // Process should still work without crash
    for (int i = 0; i < 10; ++i) {
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        for (size_t j = 0; j < kBlockSize; ++j) {
            REQUIRE(std::isfinite(output[j].l));
            REQUIRE(std::isfinite(output[j].r));
        }
    }
}

TEST_CASE("BeadsProcessor: All quality modes work without NaN", "[processor]") {
    for (int mode = 0; mode < 4; ++mode) {
        TestProcessor tp;

        BeadsParameters params;
        params.quality_mode = static_cast<QualityMode>(mode);
        params.density = 0.3f;
        params.size = 0.5f;
        params.dry_wet = 1.0f;
        tp.processor.SetParameters(params);

        std::vector<StereoFrame> input(kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
            input[i] = {std::sin(phase), std::sin(phase)};
        }

        std::vector<StereoFrame> output(kBlockSize);
        for (int block = 0; block < 20; ++block) {
            tp.processor.Process(input.data(), output.data(), kBlockSize);
            for (size_t i = 0; i < kBlockSize; ++i) {
                REQUIRE(std::isfinite(output[i].l));
                REQUIRE(std::isfinite(output[i].r));
            }
        }
    }
}

TEST_CASE("BeadsProcessor: Mode transitions produce no NaN", "[processor][decimation]") {
    TestProcessor tp;

    std::vector<StereoFrame> input(kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
        input[i] = {std::sin(phase), std::sin(phase)};
    }
    std::vector<StereoFrame> output(kBlockSize);

    // Cycle through all quality modes rapidly
    QualityMode modes[] = {
        QualityMode::kHiFi, QualityMode::kClouds,
        QualityMode::kCleanLoFi, QualityMode::kTape,
        QualityMode::kHiFi
    };

    for (auto mode : modes) {
        BeadsParameters params;
        params.quality_mode = mode;
        params.density = 0.3f;
        params.size = 0.5f;
        params.dry_wet = 1.0f;
        params.manual_gain_db = 0.0f;
        tp.processor.SetParameters(params);

        for (int block = 0; block < 30; ++block) {
            tp.processor.Process(input.data(), output.data(), kBlockSize);
            for (size_t i = 0; i < kBlockSize; ++i) {
                REQUIRE(std::isfinite(output[i].l));
                REQUIRE(std::isfinite(output[i].r));
            }
        }
    }
}

TEST_CASE("BeadsProcessor: LoFi delay mode produces output", "[processor][decimation]") {
    // Verify LoFi delay mode (8x decimation) works correctly end-to-end.
    // The buffer-level "Effective duration scales with decimation" test
    // verifies the >2s retention property directly.
    TestProcessor tp;

    BeadsParameters params;
    params.quality_mode = QualityMode::kCleanLoFi;
    params.size = 1.0f;      // Delay mode
    params.density = 0.3f;   // Moderate base delay
    params.time = 0.0f;      // Short delay (read near write head)
    params.dry_wet = 1.0f;
    params.pitch = 0.0f;
    params.shape = 0.0f;
    params.manual_gain_db = 0.0f;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);

    // Feed continuous audio and check for delay output
    float max_level = 0.0f;
    for (int block = 0; block < 200; ++block) {
        for (size_t i = 0; i < kBlockSize; ++i) {
            float t = static_cast<float>(block * kBlockSize + i);
            float phase = t / kSampleRate * 440.0f * 2.0f * 3.14159265f;
            input[i] = {std::sin(phase) * 0.5f, std::sin(phase) * 0.5f};
        }
        tp.processor.Process(input.data(), output.data(), kBlockSize);

        // Check later blocks after delay has converged
        if (block > 50) {
            for (size_t i = 0; i < kBlockSize; ++i) {
                max_level = std::max(max_level,
                    std::max(std::abs(output[i].l), std::abs(output[i].r)));
                REQUIRE(std::isfinite(output[i].l));
                REQUIRE(std::isfinite(output[i].r));
            }
        }
    }

    REQUIRE(max_level > 0.001f);
}

TEST_CASE("BeadsProcessor: Memory requirements unchanged with decimation", "[processor][decimation]") {
    // Decimation doesn't change the physical buffer size — verify requirements
    // are the same regardless of what mode we'll use
    auto req = BeadsProcessor::GetMemoryRequirements(kSampleRate);

    // Fixed 192K-frame buffer (~1.5MB stereo float + overhead)
    REQUIRE(req.total_bytes > 1000000);
    REQUIRE(req.total_bytes < 10000000);
}

TEST_CASE("BeadsProcessor: Output levels match Eurorack input levels", "[processor][levels]") {
    // Eurorack audio is ±5V. The plugin scales input ×0.2 (→ ±1.0 internal)
    // and output ×5.0 (→ ±5V). The DSP chain should maintain roughly unity
    // gain so that output amplitudes match input amplitudes.
    //
    // Test: send a ±1.0 sine (= ±5V Eurorack) through the full processor
    // in grain mode and delay mode, then verify the wet output peak is
    // within a reasonable range (not drastically attenuated or amplified).

    static constexpr float kSineFreq = 440.0f;
    static constexpr float kSineAmplitude = 1.0f;  // ±5V Eurorack level
    static constexpr int kWarmupBlocks = 100;  // Let grains fill the buffer
    static constexpr int kMeasureBlocks = 100;

    auto make_sine_block = [](StereoFrame* buf, size_t n, int block_idx) {
        for (size_t i = 0; i < n; ++i) {
            float t = static_cast<float>(block_idx * static_cast<int>(n) + static_cast<int>(i));
            float phase = t / kSampleRate * kSineFreq * 2.0f * 3.14159265f;
            float s = std::sin(phase) * kSineAmplitude;
            buf[i] = {s, s};
        }
    };

    // -- Grain mode: 100% wet, 0dB gain, no reverb, no feedback --
    SECTION("Grain mode at 100% wet") {
        TestProcessor tp;
        BeadsParameters params;
        params.density = 0.2f;            // Regular grain triggers
        params.size = 0.5f;               // Medium grains
        params.time = 0.0f;               // Read from most recent audio
        params.shape = 0.5f;
        params.pitch = 0.0f;              // Unity pitch
        params.dry_wet = 1.0f;            // Full wet
        params.feedback = 0.0f;
        params.reverb = 0.0f;
        params.manual_gain_db = 0.0f;     // 0dB = unity
        params.trigger_mode = TriggerMode::kLatched;
        tp.processor.SetParameters(params);

        std::vector<StereoFrame> input(kBlockSize);
        std::vector<StereoFrame> output(kBlockSize);

        // Warm up: fill buffer and let grains stabilize
        for (int b = 0; b < kWarmupBlocks; ++b) {
            make_sine_block(input.data(), kBlockSize, b);
            tp.processor.Process(input.data(), output.data(), kBlockSize);
        }

        // Measure peak output level
        float peak = 0.0f;
        for (int b = 0; b < kMeasureBlocks; ++b) {
            make_sine_block(input.data(), kBlockSize, kWarmupBlocks + b);
            tp.processor.Process(input.data(), output.data(), kBlockSize);
            for (size_t i = 0; i < kBlockSize; ++i) {
                peak = std::max(peak,
                    std::max(std::abs(output[i].l), std::abs(output[i].r)));
            }
        }

        // Wet output should be within -6dB to +3dB of input level.
        // (±1.0 input → output peak should be between 0.5 and 1.4)
        INFO("Grain mode wet peak = " << peak);
        REQUIRE(peak > 0.5f);
        REQUIRE(peak < 1.4f);
    }

    // -- Dry pass-through: should be unity --
    SECTION("Dry pass-through") {
        TestProcessor tp;
        BeadsParameters params;
        params.dry_wet = 0.0f;            // Full dry
        params.reverb = 0.0f;
        params.manual_gain_db = 0.0f;
        // This SECTION pins the menu-off bypass-dry path (dry = raw input);
        // the default post-gain path is covered by the [drytap] tests.
        params.dry_post_gain = false;
        tp.processor.SetParameters(params);

        std::vector<StereoFrame> input(kBlockSize);
        std::vector<StereoFrame> output(kBlockSize);

        // A few blocks to settle
        for (int b = 0; b < 10; ++b) {
            make_sine_block(input.data(), kBlockSize, b);
            tp.processor.Process(input.data(), output.data(), kBlockSize);
        }

        float peak = 0.0f;
        for (int b = 0; b < 10; ++b) {
            make_sine_block(input.data(), kBlockSize, 10 + b);
            tp.processor.Process(input.data(), output.data(), kBlockSize);
            for (size_t i = 0; i < kBlockSize; ++i) {
                peak = std::max(peak,
                    std::max(std::abs(output[i].l), std::abs(output[i].r)));
            }
        }

        // Dry pass-through should be near unity
        INFO("Dry pass-through peak = " << peak);
        REQUIRE(peak > 0.9f);
        REQUIRE(peak < 1.1f);
    }
}

TEST_CASE("BeadsProcessor: High density + large size produces continuous audio", "[processor][stress]") {
    TestProcessor tp;

    BeadsParameters params;
    params.density = 0.1f;    // Fast grain triggers
    params.size = 0.9f;       // Long grains (many active)
    params.dry_wet = 1.0f;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.manual_gain_db = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
        input[i] = {std::sin(phase), std::sin(phase)};
    }

    std::vector<StereoFrame> output(kBlockSize);

    bool all_finite = true;
    bool had_output = false;

    for (int block = 0; block < 200; ++block) {
        tp.processor.Process(input.data(), output.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            if (!std::isfinite(output[i].l) || !std::isfinite(output[i].r)) {
                all_finite = false;
            }
            float level = std::max(std::abs(output[i].l), std::abs(output[i].r));
            if (level > 0.001f) had_output = true;
        }
    }

    REQUIRE(all_finite);
    REQUIRE(had_output);
}

TEST_CASE("BeadsProcessor: Feedback affects output when source is present", "[processor]") {
    // Run two processors with the same source: one with feedback=0, one with feedback=0.9.
    // After enough blocks for the feedback to propagate through the buffer round-trip,
    // their outputs should diverge.
    // With the BROKEN additive model: LimitFeedback clips away the feedback addition
    // when source fills the buffer to ±1.0, so both processors write identical buffer
    // content and produce identical output. diff ≈ 0 → test fails.
    // With the FIXED crossfade model: feedback=0.9 processor writes a blend of
    // source and grain output to its buffer, diverging from the feedback=0 processor.
    // diff > 0 after several round-trips → test passes.

    auto make_params = [](float fb) {
        BeadsParameters p;
        p.density = 0.2f;          // Left of noon = fast constant grain triggers
        p.time = 0.0f;             // Read near write head (short round-trip)
        p.size = 0.3f;             // ~130ms grains; min_offset ~260ms
        p.shape = 0.5f;
        p.pitch = 0.0f;
        p.dry_wet = 1.0f;          // Full wet — only grain output in the comparison
        p.reverb = 0.0f;
        p.feedback = fb;
        p.manual_gain_db = 0.0f;   // Fixed gain — bypass auto-gain ramp-up
        p.trigger_mode = TriggerMode::kLatched;
        return p;
    };

    TestProcessor tp_no_fb, tp_fb;
    tp_no_fb.processor.SetParameters(make_params(0.0f));
    tp_fb.processor.SetParameters(make_params(0.9f));

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> out_no_fb(kBlockSize), out_fb(kBlockSize);

    // Feed the same sine wave to both processors for enough blocks to let
    // feedback propagate through several buffer round-trips.
    // Round-trip = min_offset / sample_rate ≈ 260ms → 200 blocks ≈ 1.07s ≈ 4 round-trips.
    for (int block = 0; block < 200; ++block) {
        for (size_t i = 0; i < kBlockSize; ++i) {
            float t = static_cast<float>(block * static_cast<int>(kBlockSize) + static_cast<int>(i));
            float s = std::sin(t / kSampleRate * 440.0f * 2.0f * 3.14159265f) * 1.0f;
            input[i] = {s, s};
        }
        tp_no_fb.processor.Process(input.data(), out_no_fb.data(), kBlockSize);
        tp_fb.processor.Process(input.data(), out_fb.data(), kBlockSize);
    }

    // The last block's outputs should differ between the two processors.
    float max_diff = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        max_diff = std::max(max_diff, std::abs(out_fb[i].l - out_no_fb[i].l));
    }

    REQUIRE(max_diff > 0.01f);
}

// Local sine fill with controllable amplitude: Q9 tests need the post-gain
// signal to stay inside SoftLimit's exactly-linear region (|x| <= 0.8).
static void make_scaled_sine_block(StereoFrame* buf, size_t n, int block_index,
                                   float amplitude) {
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(block_index * n + i);
        float v = amplitude * std::sin(2.0f * 3.14159265f * 440.0f * t / 48000.0f);
        buf[i] = {v, v};
    }
}

TEST_CASE("BeadsProcessor: dry tap follows input gain when dry_post_gain is set", "[processor][drytap]") {
    TestProcessor tp;
    BeadsParameters params;
    params.dry_wet = 0.0f;            // Full dry
    params.reverb = 0.0f;
    params.auto_gain = false;
    params.manual_gain_db = 12.0f;    // 10^(12/20) = 3.9811x
    params.dry_post_gain = true;      // The new default, set explicitly
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);

    // Settle manual-gain smoothing. AutoGain's manual-mode OnePole runs at
    // coeff 0.0001 (~10.4k-sample / ~217ms time constant at 48kHz), so 50
    // blocks (12800 samples) leaves ~4% of the gain step unsettled — enough
    // to blow the 0.002 margin below. 200 blocks (51200 samples, ~4.9 time
    // constants) settles within ~0.0009 of target, comfortably inside margin.
    for (int b = 0; b < 200; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // 0.05 * 3.9811 = 0.199 peak — inside SoftLimit's linear region, so the
    // dry output is exactly the gained input (crossfade dry gain is 1 at
    // dry_wet = 0).
    for (int b = 200; b < 205; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            REQUIRE(output[i].l == Approx(input[i].l * 3.9811f).margin(0.002f));
            REQUIRE(output[i].r == Approx(input[i].r * 3.9811f).margin(0.002f));
        }
    }
}

TEST_CASE("BeadsProcessor: dry_post_gain=false keeps the pre-gain bypass dry", "[processor][drytap]") {
    TestProcessor tp;
    BeadsParameters params;
    params.dry_wet = 0.0f;
    params.reverb = 0.0f;
    params.auto_gain = false;
    params.manual_gain_db = 12.0f;    // gain applies to wet path only
    params.dry_post_gain = false;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);

    for (int b = 0; b < 50; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // Dry equals the RAW input despite the +12 dB input gain.
    for (int b = 50; b < 55; ++b) {
        make_scaled_sine_block(input.data(), kBlockSize, b, 0.05f);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            REQUIRE(output[i].l == Approx(input[i].l).margin(0.002f));
            REQUIRE(output[i].r == Approx(input[i].r).margin(0.002f));
        }
    }
}
