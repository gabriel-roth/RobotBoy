#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "beads/types.h"
#include "quality/quality_processor.h"
#include "fx/saturation.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

TEST_CASE("QualityModes: Clouds output quantization is detectable", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // Feed a ramp of small values and check that the Clouds output shows
    // quantization steps (round to 12-bit = 1/2048 steps)
    int step_changes = 0;
    float prev_out = 0.0f;
    bool first = true;

    for (int i = 0; i < 1000; ++i) {
        float val = static_cast<float>(i) / 1000.0f * 0.1f;  // 0 to 0.1 ramp
        StereoFrame in = {val, val};
        StereoFrame out = qp.ProcessOutput(in, QualityMode::kClouds);

        if (!first && out.l != prev_out) {
            step_changes++;
        }
        prev_out = out.l;
        first = false;
    }

    // A ramp from 0 to 0.1 at 12-bit resolution spans ~205 steps (0.1 * 2048)
    // The quantized output should have fewer unique transitions than the input
    REQUIRE(step_changes > 0);
    REQUIRE(step_changes < 999);  // Fewer than every sample changing (quantized)
}

TEST_CASE("QualityModes: Tape mode reduces high-frequency content", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // Generate a high-frequency signal (20kHz) well above tape LP at 8kHz
    float hifi_energy = 0.0f;
    float tape_energy = 0.0f;

    QualityProcessor qp_hifi;
    qp_hifi.Init(kSampleRate);

    for (int i = 0; i < 10000; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 20000.0f * 2.0f * kPi;
        float val = std::sin(phase);
        StereoFrame in = {val, val};

        StereoFrame hifi_out = qp_hifi.ProcessInput(in, QualityMode::kHiFi);
        StereoFrame tape_out = qp.ProcessInput(in, QualityMode::kTape);

        hifi_energy += hifi_out.l * hifi_out.l;
        tape_energy += tape_out.l * tape_out.l;
    }

    // Tape LP filter at 8kHz should heavily attenuate 20kHz
    REQUIRE(tape_energy < hifi_energy * 0.5f);
}

TEST_CASE("QualityModes: HiFi ProcessInput is near-passthrough", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // HiFi input should pass through unchanged
    float max_error = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float val = std::sin(static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * kPi);
        StereoFrame in = {val, -val};
        StereoFrame out = qp.ProcessInput(in, QualityMode::kHiFi);
        max_error = std::max(max_error, std::abs(out.l - val));
        max_error = std::max(max_error, std::abs(out.r - (-val)));
    }

    REQUIRE(max_error < 0.001f);
}

TEST_CASE("QualityModes: HiFi ProcessOutput is near-passthrough", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    float max_error = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float val = std::sin(static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * kPi);
        StereoFrame in = {val, -val};
        StereoFrame out = qp.ProcessOutput(in, QualityMode::kHiFi);
        max_error = std::max(max_error, std::abs(out.l - val));
        max_error = std::max(max_error, std::abs(out.r - (-val)));
    }

    REQUIRE(max_error < 0.001f);
}

TEST_CASE("QualityModes: Each mode produces finite output", "[quality]") {
    for (int mode = 0; mode < 4; ++mode) {
        QualityProcessor qp;
        qp.Init(kSampleRate);

        QualityMode qm = static_cast<QualityMode>(mode);

        for (int i = 0; i < 5000; ++i) {
            float val = std::sin(static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * kPi);
            StereoFrame in = {val, val};

            StereoFrame out_in = qp.ProcessInput(in, qm);
            StereoFrame out_out = qp.ProcessOutput(in, qm);

            REQUIRE(std::isfinite(out_in.l));
            REQUIRE(std::isfinite(out_in.r));
            REQUIRE(std::isfinite(out_out.l));
            REQUIRE(std::isfinite(out_out.r));
        }
    }
}

TEST_CASE("QualityModes: CleanLoFi output LP attenuates high frequencies", "[quality]") {
    QualityProcessor qp_lofi, qp_hifi;
    qp_lofi.Init(kSampleRate);
    qp_hifi.Init(kSampleRate);

    // Generate a high-frequency signal (18kHz) and pass through output processing
    float hifi_energy = 0.0f;
    float lofi_energy = 0.0f;

    for (int i = 0; i < 10000; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 18000.0f * 2.0f * kPi;
        float val = std::sin(phase);
        StereoFrame in = {val, val};

        StereoFrame hifi_out = qp_hifi.ProcessOutput(in, QualityMode::kHiFi);
        StereoFrame lofi_out = qp_lofi.ProcessOutput(in, QualityMode::kCleanLoFi);

        hifi_energy += hifi_out.l * hifi_out.l;
        lofi_energy += lofi_out.l * lofi_out.l;
    }

    // CleanLoFi LP at 10kHz should attenuate 18kHz
    REQUIRE(lofi_energy < hifi_energy * 0.5f);
}

TEST_CASE("QualityModes: Tape wow/flutter does not pump amplitude", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // Run for 4+ seconds (multiple full wow LFO cycles at 0.5Hz)
    const int total_samples = static_cast<int>(kSampleRate * 4.5f);
    const int settle_samples = static_cast<int>(kSampleRate * 0.5f);
    const int window_samples = static_cast<int>(kSampleRate * 0.1f);  // 100ms windows

    // Feed constant-amplitude 440Hz sine through ProcessInput -> ProcessOutput
    std::vector<float> output_samples;
    output_samples.reserve(total_samples);

    for (int i = 0; i < total_samples; ++i) {
        float val = 0.5f * std::sin(static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * kPi);
        StereoFrame in = {val, val};
        StereoFrame processed = qp.ProcessInput(in, QualityMode::kTape);
        StereoFrame out = qp.ProcessOutput(processed, QualityMode::kTape);
        output_samples.push_back(out.l);
    }

    // Measure RMS in consecutive 100ms windows after settling
    float min_rms = 1e10f;
    float max_rms = 0.0f;

    for (int start = settle_samples; start + window_samples <= total_samples; start += window_samples) {
        float sum_sq = 0.0f;
        for (int j = start; j < start + window_samples; ++j) {
            sum_sq += output_samples[j] * output_samples[j];
        }
        float rms = std::sqrt(sum_sq / static_cast<float>(window_samples));
        if (rms < min_rms) min_rms = rms;
        if (rms > max_rms) max_rms = rms;
    }

    // Less than 5% amplitude variation across all windows
    REQUIRE(min_rms > 0.0f);
    REQUIRE(max_rms / min_rms < 1.05f);
}

TEST_CASE("QualityModes: Tape pitch modulation ratio stays within tight range", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // Run for 2 seconds (one full wow cycle at 0.5Hz)
    const int total_samples = 96000;
    float min_ratio = 2.0f;
    float max_ratio = 0.0f;

    for (int i = 0; i < total_samples; ++i) {
        float ratio = qp.GetPitchModulation(QualityMode::kTape, 1);
        if (ratio < min_ratio) min_ratio = ratio;
        if (ratio > max_ratio) max_ratio = ratio;
    }

    // ±0.023 semitones combined ≈ ±0.13% ratio deviation
    REQUIRE(min_ratio >= 0.998f);
    REQUIRE(max_ratio <= 1.002f);
}

TEST_CASE("QualityModes: Tape ProcessInput/ProcessOutput roundtrip preserves amplitude", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    const int total_samples = 5000;
    const int settle_samples = 1000;

    for (int i = 0; i < total_samples; ++i) {
        StereoFrame in = {0.5f, 0.5f};
        StereoFrame processed = qp.ProcessInput(in, QualityMode::kTape);
        StereoFrame out = qp.ProcessOutput(processed, QualityMode::kTape);

        if (i >= settle_samples) {
            REQUIRE(out.l >= 0.3f);
            REQUIRE(out.l <= 0.7f);
            REQUIRE(out.r >= 0.3f);
            REQUIRE(out.r <= 0.7f);
        }
    }
}

TEST_CASE("QualityModes: Mode crossfade produces smooth transition", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // Process several samples in HiFi mode
    for (int i = 0; i < 100; ++i) {
        StereoFrame in = {0.5f, 0.5f};
        qp.ProcessInput(in, QualityMode::kHiFi);
        qp.ProcessOutput(in, QualityMode::kHiFi);
    }

    // Switch to Clouds and check the transition frames
    float prev_l = 0.5f;
    float max_delta = 0.0f;
    for (int i = 0; i < 100; ++i) {
        StereoFrame in = {0.5f, 0.5f};
        StereoFrame out = qp.ProcessOutput(in, QualityMode::kClouds);
        float delta = std::abs(out.l - prev_l);
        max_delta = std::max(max_delta, delta);
        prev_l = out.l;
    }

    // The crossfade should prevent a large jump at the mode switch
    REQUIRE(max_delta < 0.1f);
}

TEST_CASE("QualityModes: Tape LimitFeedback does not create gain in compress/expand roundtrip", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    Saturation sat;
    sat.Init();

    // Simulate the Tape feedback path:
    // ProcessInput(compress 64) → LimitFeedback → buffer → ProcessOutput(expand 64)
    // This roundtrip must not add gain.
    float test_values[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f, -0.3f, -0.7f};
    for (float val : test_values) {
        StereoFrame in = {val, val};
        StereoFrame compressed = qp.ProcessInput(in, QualityMode::kTape);
        StereoFrame limited = sat.LimitFeedback(compressed, QualityMode::kTape);
        StereoFrame expanded = qp.ProcessOutput(limited, QualityMode::kTape);

        // Output amplitude should not exceed input amplitude (no gain)
        float input_amp = std::abs(val);
        float output_amp = std::abs(expanded.l);
        REQUIRE(output_amp <= input_amp * 1.1f);  // Allow 10% for filter transients
    }
}

TEST_CASE("QualityModes: LoFi input LP attenuates above 2.5kHz", "[quality][decimation]") {
    QualityProcessor qp_lofi, qp_hifi;
    qp_lofi.Init(kSampleRate);
    qp_hifi.Init(kSampleRate);

    // Generate a 4kHz signal (above LoFi's 2.5kHz anti-aliasing cutoff)
    float hifi_energy = 0.0f;
    float lofi_energy = 0.0f;

    for (int i = 0; i < 10000; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 4000.0f * 2.0f * kPi;
        float val = std::sin(phase);
        StereoFrame in = {val, val};

        StereoFrame hifi_out = qp_hifi.ProcessInput(in, QualityMode::kHiFi);
        StereoFrame lofi_out = qp_lofi.ProcessInput(in, QualityMode::kCleanLoFi);

        hifi_energy += hifi_out.l * hifi_out.l;
        lofi_energy += lofi_out.l * lofi_out.l;
    }

    // LoFi LP at 2.5kHz should significantly attenuate 4kHz
    REQUIRE(lofi_energy < hifi_energy * 0.3f);
}

TEST_CASE("QualityModes: Tape input LP attenuates above 5kHz", "[quality][decimation]") {
    QualityProcessor qp_tape, qp_hifi;
    qp_tape.Init(kSampleRate);
    qp_hifi.Init(kSampleRate);

    // Generate a 15kHz signal (well above Tape's 5kHz cutoff).
    // Use a high frequency because tape mode's mu-law compression
    // re-expands attenuated signals, so we need strong LP rejection.
    float hifi_energy = 0.0f;
    float tape_energy = 0.0f;

    for (int i = 0; i < 10000; ++i) {
        float phase = static_cast<float>(i) / kSampleRate * 15000.0f * 2.0f * kPi;
        float val = std::sin(phase);
        StereoFrame in = {val, val};

        StereoFrame hifi_out = qp_hifi.ProcessInput(in, QualityMode::kHiFi);
        StereoFrame tape_out = qp_tape.ProcessInput(in, QualityMode::kTape);

        hifi_energy += hifi_out.l * hifi_out.l;
        tape_energy += tape_out.l * tape_out.l;
    }

    // Tape LP at 5kHz should heavily attenuate 15kHz
    REQUIRE(tape_energy < hifi_energy * 0.3f);
}

TEST_CASE("QualityModes: Tape feedback loop converges with low feedback", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    Saturation sat;
    sat.Init();

    // Simulate the actual feedback loop sample-by-sample:
    // input + feedback → ProcessInput → LimitFeedback → [buffer] → ProcessOutput → capture feedback
    float feedback_gain = 0.1f * 0.1f;  // feedback param 0.1 squared (matches beads_processor)
    StereoFrame feedback_sample = {0.0f, 0.0f};

    // Run for 2 seconds with constant input
    const int total_samples = 96000;
    const int settle_samples = 24000;  // 0.5s settle for LP filters + mode crossfade
    float max_output = 0.0f;

    for (int i = 0; i < total_samples; ++i) {
        float val = 0.5f * std::sin(static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * kPi);
        StereoFrame in = {val, val};

        // Mix in feedback
        in.l += feedback_sample.l * feedback_gain;
        in.r += feedback_sample.r * feedback_gain;

        // ProcessInput (compress)
        StereoFrame compressed = qp.ProcessInput(in, QualityMode::kTape);
        // LimitFeedback
        StereoFrame limited = sat.LimitFeedback(compressed, QualityMode::kTape);
        // ProcessOutput (expand) — simulates buffer readback
        StereoFrame expanded = qp.ProcessOutput(limited, QualityMode::kTape);
        // Capture feedback
        feedback_sample = expanded;

        if (i >= settle_samples) {
            max_output = std::max(max_output, std::abs(expanded.l));
        }
    }

    // With 0.5 amplitude input and 1% feedback gain, output should stay bounded
    // and not grow beyond the input amplitude (no feedback-induced gain)
    REQUIRE(max_output < 0.8f);  // Well below 1.0, reasonable for 0.5 input through mu-law
}
