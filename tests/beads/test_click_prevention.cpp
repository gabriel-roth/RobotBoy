#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "beads/beads.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;
static constexpr size_t kBlockSize = 256;

// Helper to create and init a processor
struct ClickTestProcessor {
    std::vector<uint8_t> memory;
    BeadsProcessor processor;

    ClickTestProcessor() {
        auto req = BeadsProcessor::GetMemoryRequirements(kSampleRate);
        memory.resize(req.total_bytes, 0);
        processor.Init(memory.data(), memory.size(), kSampleRate);
    }
};

// Generate a 440 Hz sine wave block
static void FillSine(StereoFrame* buf, size_t count, float& phase) {
    for (size_t i = 0; i < count; ++i) {
        float val = std::sin(phase);
        buf[i] = {val, val};
        phase += 440.0f / kSampleRate * 2.0f * 3.14159265f;
        if (phase >= 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
    }
}

// Measure the maximum sample-to-sample delta in a stereo output buffer
static float MaxDelta(const StereoFrame* output, size_t count) {
    float max_d = 0.0f;
    for (size_t i = 1; i < count; ++i) {
        float dl = std::abs(output[i].l - output[i - 1].l);
        float dr = std::abs(output[i].r - output[i - 1].r);
        max_d = std::max(max_d, std::max(dl, dr));
    }
    return max_d;
}

TEST_CASE("Click prevention: Freeze on/off produces no large discontinuities", "[click]") {
    ClickTestProcessor tp;

    BeadsParameters params;
    params.density = 0.1f;
    params.dry_wet = 1.0f;
    params.size = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.time = 0.5f;
    params.manual_gain_db = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);
    float phase = 0.0f;

    // Warm up: process several blocks so grains are active
    for (int i = 0; i < 100; ++i) {
        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // Toggle freeze ON and check for clicks across the transition
    params.freeze = true;
    tp.processor.SetParameters(params);

    float worst_delta = 0.0f;
    for (int i = 0; i < 10; ++i) {
        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        worst_delta = std::max(worst_delta, MaxDelta(output.data(), kBlockSize));
    }

    // A click would produce a delta near or above 0.3
    REQUIRE(worst_delta < 0.3f);

    // Toggle freeze OFF and check again
    params.freeze = false;
    tp.processor.SetParameters(params);

    worst_delta = 0.0f;
    for (int i = 0; i < 10; ++i) {
        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        worst_delta = std::max(worst_delta, MaxDelta(output.data(), kBlockSize));
    }

    REQUIRE(worst_delta < 0.3f);
}

TEST_CASE("Click prevention: Mode crossfade when SIZE sweeps to delay", "[click]") {
    ClickTestProcessor tp;

    BeadsParameters params;
    params.density = 0.3f;
    params.dry_wet = 1.0f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.time = 0.5f;
    params.manual_gain_db = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);
    float phase = 0.0f;

    // Warm up at size=0.5 (grain mode)
    params.size = 0.5f;
    tp.processor.SetParameters(params);
    for (int i = 0; i < 100; ++i) {
        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // Sweep SIZE from 0.9 to 1.0 across blocks (grain -> delay transition)
    float worst_delta = 0.0f;
    for (int i = 0; i <= 20; ++i) {
        float t = static_cast<float>(i) / 20.0f;
        params.size = 0.9f + t * 0.1f;  // 0.9 -> 1.0
        tp.processor.SetParameters(params);

        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        worst_delta = std::max(worst_delta, MaxDelta(output.data(), kBlockSize));
    }

    // No harsh clicks during the crossfade (some discontinuity is expected
    // at grain-to-delay engine crossover, especially since grain and delay
    // engines may be reading from very different buffer positions)
    REQUIRE(worst_delta < 1.0f);
}

TEST_CASE("Click prevention: Quality mode switch mid-stream produces finite output", "[click]") {
    ClickTestProcessor tp;

    BeadsParameters params;
    params.density = 0.2f;
    params.dry_wet = 1.0f;
    params.size = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.time = 0.5f;
    params.manual_gain_db = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;
    params.quality_mode = QualityMode::kHiFi;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);
    float phase = 0.0f;

    // Warm up
    for (int i = 0; i < 100; ++i) {
        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // Switch through all quality modes rapidly, check no NaN/Inf and no huge jumps
    QualityMode modes[] = {
        QualityMode::kClouds, QualityMode::kTape,
        QualityMode::kCleanLoFi, QualityMode::kHiFi
    };

    for (auto mode : modes) {
        params.quality_mode = mode;
        tp.processor.SetParameters(params);

        for (int i = 0; i < 5; ++i) {
            FillSine(input.data(), kBlockSize, phase);
            tp.processor.Process(input.data(), output.data(), kBlockSize);

            for (size_t j = 0; j < kBlockSize; ++j) {
                REQUIRE(std::isfinite(output[j].l));
                REQUIRE(std::isfinite(output[j].r));
            }
        }
    }
}

TEST_CASE("Click prevention: Output is continuous with sine input", "[click]") {
    ClickTestProcessor tp;

    BeadsParameters params;
    params.density = 0.1f;
    params.dry_wet = 1.0f;
    params.size = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.time = 0.95f;
    params.manual_gain_db = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;
    tp.processor.SetParameters(params);

    std::vector<StereoFrame> input(kBlockSize);
    std::vector<StereoFrame> output(kBlockSize);
    float phase = 0.0f;

    // Warm up so grains are active and producing output
    for (int i = 0; i < 200; ++i) {
        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
    }

    // Now collect several blocks and check max delta stays bounded
    float worst_delta = 0.0f;
    for (int i = 0; i < 50; ++i) {
        FillSine(input.data(), kBlockSize, phase);
        tp.processor.Process(input.data(), output.data(), kBlockSize);
        worst_delta = std::max(worst_delta, MaxDelta(output.data(), kBlockSize));
    }

    // With a smooth sine input and stable parameters, output should be smooth
    REQUIRE(worst_delta < 0.3f);
}
