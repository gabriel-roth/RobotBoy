#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>
#include "beads/beads.h"

using namespace beads;

static constexpr float kSampleRate = 48000.0f;
static constexpr size_t kBlockSize = 256;

struct TestProcessor {
    std::vector<uint8_t> memory;
    BeadsProcessor processor;
    TestProcessor() {
        auto req = BeadsProcessor::GetMemoryRequirements(kSampleRate);
        memory.resize(req.total_bytes, 0);
        processor.Init(memory.data(), memory.size(), kSampleRate);
    }
};

// Test each quality mode in grain mode to find which one is silent
TEST_CASE("Silence hunt: grain mode output level per quality mode", "[silence]") {
    QualityMode modes[] = {
        QualityMode::kHiFi, QualityMode::kClouds,
        QualityMode::kCleanLoFi, QualityMode::kTape
    };
    const char* names[] = {"HiFi", "Clouds", "CleanLoFi", "Tape"};

    for (int m = 0; m < 4; ++m) {
        TestProcessor tp;

        BeadsParameters params;
        params.quality_mode = modes[m];
        params.density = 0.1f;
        params.dry_wet = 1.0f;  // Full wet to isolate wet path
        params.time = 0.0f;     // Read near write head
        params.size = 0.3f;
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
        float max_level = 0.0f;

        for (int block = 0; block < 400; ++block) {
            tp.processor.Process(input.data(), output.data(), kBlockSize);
            // Skip quality duck period
            if (block > 20) {
                for (size_t i = 0; i < kBlockSize; ++i) {
                    max_level = std::max(max_level,
                        std::max(std::abs(output[i].l), std::abs(output[i].r)));
                }
            }
        }

        INFO("Quality mode: " << names[m] << " max_level=" << max_level);
        REQUIRE(max_level > 0.01f);
    }
}

// Test each quality mode in delay mode
TEST_CASE("Silence hunt: delay mode output level per quality mode", "[silence]") {
    QualityMode modes[] = {
        QualityMode::kHiFi, QualityMode::kClouds,
        QualityMode::kCleanLoFi, QualityMode::kTape
    };
    const char* names[] = {"HiFi", "Clouds", "CleanLoFi", "Tape"};

    for (int m = 0; m < 4; ++m) {
        TestProcessor tp;

        BeadsParameters params;
        params.quality_mode = modes[m];
        params.density = 0.3f;
        params.dry_wet = 1.0f;
        params.time = 0.0f;     // Short delay
        params.size = 1.0f;     // Delay mode
        params.shape = 0.0f;
        params.pitch = 0.0f;
        params.manual_gain_db = 0.0f;
        tp.processor.SetParameters(params);

        std::vector<StereoFrame> input(kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
            input[i] = {std::sin(phase), std::sin(phase)};
        }

        std::vector<StereoFrame> output(kBlockSize);
        float max_level = 0.0f;

        for (int block = 0; block < 400; ++block) {
            tp.processor.Process(input.data(), output.data(), kBlockSize);
            if (block > 20) {
                for (size_t i = 0; i < kBlockSize; ++i) {
                    max_level = std::max(max_level,
                        std::max(std::abs(output[i].l), std::abs(output[i].r)));
                }
            }
        }

        INFO("Quality mode: " << names[m] << " max_level=" << max_level);
        REQUIRE(max_level > 0.01f);
    }
}

// Test dry signal passthrough (dry_wet = 0.5)
TEST_CASE("Silence hunt: dry signal passthrough", "[silence]") {
    QualityMode modes[] = {
        QualityMode::kHiFi, QualityMode::kClouds,
        QualityMode::kCleanLoFi, QualityMode::kTape
    };
    const char* names[] = {"HiFi", "Clouds", "CleanLoFi", "Tape"};

    for (int m = 0; m < 4; ++m) {
        TestProcessor tp;

        BeadsParameters params;
        params.quality_mode = modes[m];
        params.density = 0.5f;  // No triggers
        params.dry_wet = 0.5f;  // 50/50 dry/wet
        params.size = 0.5f;
        params.manual_gain_db = 0.0f;
        tp.processor.SetParameters(params);

        std::vector<StereoFrame> input(kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            float phase = static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f;
            input[i] = {std::sin(phase), std::sin(phase)};
        }

        std::vector<StereoFrame> output(kBlockSize);
        float max_level = 0.0f;

        for (int block = 0; block < 50; ++block) {
            tp.processor.Process(input.data(), output.data(), kBlockSize);
            if (block > 5) {
                for (size_t i = 0; i < kBlockSize; ++i) {
                    max_level = std::max(max_level,
                        std::max(std::abs(output[i].l), std::abs(output[i].r)));
                }
            }
        }

        INFO("Quality mode: " << names[m] << " max_level=" << max_level);
        REQUIRE(max_level > 0.1f);  // Dry signal should be strong
    }
}
