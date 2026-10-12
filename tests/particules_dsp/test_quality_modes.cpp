#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "particules_dsp/types.h"
#include "quality/quality_processor.h"
#include "fx/saturation.h"

using namespace particules_dsp;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

TEST_CASE("QualityModes: Sunny tape feedback limiter is bounded", "[quality][saturation]") {
    Saturation saturation;
    saturation.Init();
    const float inputs[] = {-20.0f, -10.0f, -3.0f, 0.0f, 3.0f, 10.0f, 20.0f};
    float previous = -2.0f;
    for (float input : inputs) {
        const float output = saturation.LimitFeedback(input, QualityMode::kSunnyTape);
        REQUIRE(std::isfinite(output));
        REQUIRE(output >= -1.0f);
        REQUIRE(output <= 1.0f);
        REQUIRE(output >= previous);
        if (input != 0.0f)
            REQUIRE((output > 0.0f) == (input > 0.0f));
        previous = output;
    }
}

TEST_CASE("QualityModes: Scorched preserves stereo", "[quality]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float diff_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float l = 0.5f * std::sin(2.0f * kPi * 500.0f * i / 48000.0f);
        float r = 0.5f * std::sin(2.0f * kPi * 700.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({l, r}, QualityMode::kScorchedCassette);
        if (i > 480) diff_peak = std::max(diff_peak, std::fabs(out.l - out.r));
    }
    REQUIRE(diff_peak > 0.1f);   // channels stay distinct (old code mono-summed)
}

TEST_CASE("QualityModes: Scorched input LP passes 4kHz", "[quality][decimation]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float v = 0.5f * std::sin(2.0f * kPi * 4000.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({v, v}, QualityMode::kScorchedCassette);
        if (i > 480) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak > in_peak * 0.7f);
}

TEST_CASE("QualityModes: Scorched input LP attenuates 20kHz", "[quality][decimation]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float v = 0.5f * std::sin(2.0f * kPi * 20000.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({v, v}, QualityMode::kScorchedCassette);
        if (i > 480) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak < in_peak * 0.4f);   // one octave above 10k, 2nd-order LP
}

TEST_CASE("QualityModes: Sunny tape input LP passes 4kHz", "[quality][decimation]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float v = 0.5f * std::sin(2.0f * kPi * 4000.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({v, v}, QualityMode::kSunnyTape);
        if (i > 480) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak > in_peak * 0.7f);
}

TEST_CASE("QualityModes: Sunny tape input LP attenuates 20kHz", "[quality][decimation]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float v = 0.5f * std::sin(2.0f * kPi * 20000.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({v, v}, QualityMode::kSunnyTape);
        if (i > 480) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak < in_peak * 0.4f);   // one octave above 10k, 2nd-order LP
}

TEST_CASE("QualityModes: HiFi ProcessInput is near-passthrough", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // HiFi input should pass through unchanged
    float max_error = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float val = std::sin(static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * kPi);
        StereoFrame in = {val, -val};
        StereoFrame out = qp.ProcessInput(in, QualityMode::kBrightDigital);
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
        StereoFrame out = qp.ProcessOutput(in, QualityMode::kBrightDigital);
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

TEST_CASE("QualityModes: Sunny tape output LP attenuates high frequencies", "[quality]") {
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

        StereoFrame hifi_out = qp_hifi.ProcessOutput(in, QualityMode::kBrightDigital);
        StereoFrame lofi_out = qp_lofi.ProcessOutput(in, QualityMode::kSunnyTape);

        hifi_energy += hifi_out.l * hifi_out.l;
        lofi_energy += lofi_out.l * lofi_out.l;
    }

    // Sunny tape output LP at 10kHz should attenuate 18kHz
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
        StereoFrame processed = qp.ProcessInput(in, QualityMode::kScorchedCassette);
        StereoFrame out = qp.ProcessOutput(processed, QualityMode::kScorchedCassette);
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
        float ratio = qp.GetPitchModulation(QualityMode::kScorchedCassette, 1);
        if (ratio < min_ratio) min_ratio = ratio;
        if (ratio > max_ratio) max_ratio = ratio;
    }

    // ±0.023 semitones combined ≈ ±0.13% ratio deviation
    REQUIRE(min_ratio >= 0.998f);
    REQUIRE(max_ratio <= 1.002f);
}

TEST_CASE("QualityModes: Scorched input/output pair is level-neutral", "[quality]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 9600; ++i) {
        float v = 0.4f * std::sin(2.0f * kPi * 1000.0f * i / 48000.0f);
        StereoFrame mid = qp.ProcessInput({v, v}, QualityMode::kScorchedCassette);
        StereoFrame out = qp.ProcessOutput(mid, QualityMode::kScorchedCassette);
        if (i > 960) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak == Approx(in_peak).margin(0.15f));
}

TEST_CASE("QualityModes: Mode crossfade produces smooth transition", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // Process several samples in HiFi mode
    for (int i = 0; i < 100; ++i) {
        StereoFrame in = {0.5f, 0.5f};
        qp.ProcessInput(in, QualityMode::kBrightDigital);
        qp.ProcessOutput(in, QualityMode::kBrightDigital);
    }

    // Switch to Clouds and check the transition frames
    float prev_l = 0.5f;
    float max_delta = 0.0f;
    for (int i = 0; i < 100; ++i) {
        StereoFrame in = {0.5f, 0.5f};
        StereoFrame out = qp.ProcessOutput(in, QualityMode::kColdDigital);
        float delta = std::abs(out.l - prev_l);
        max_delta = std::max(max_delta, delta);
        prev_l = out.l;
    }

    // The crossfade should prevent a large jump at the mode switch
    REQUIRE(max_delta < 0.1f);
}

TEST_CASE("QualityModes: Tape LimitFeedback keeps the feedback path bounded", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    Saturation sat;
    sat.Init();

    // Simulate the Scorched feedback path:
    // ProcessInput -> LimitFeedback -> buffer -> ProcessOutput
    // The loop must stay bounded (no companding-specific gain claim now
    // that Scorched no longer compresses/expands via mu-law).
    float test_values[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f, -0.3f, -0.7f};
    for (float val : test_values) {
        StereoFrame in = {val, val};
        StereoFrame processed = qp.ProcessInput(in, QualityMode::kScorchedCassette);
        StereoFrame limited = sat.LimitFeedback(processed, QualityMode::kScorchedCassette);
        StereoFrame out = qp.ProcessOutput(limited, QualityMode::kScorchedCassette);

        REQUIRE(std::isfinite(out.l));
        REQUIRE(std::isfinite(out.r));
        REQUIRE(std::abs(out.l) <= 1.0f);
        REQUIRE(std::abs(out.r) <= 1.0f);
    }
}

TEST_CASE("QualityModes: Tape feedback loop converges with low feedback", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    Saturation sat;
    sat.Init();

    // Simulate the actual feedback loop sample-by-sample:
    // input + feedback → ProcessInput → LimitFeedback → [buffer] → ProcessOutput → capture feedback
    float feedback_gain = 0.1f * 0.1f;  // feedback param 0.1 squared (matches particules_processor)
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

        // ProcessInput
        StereoFrame processed = qp.ProcessInput(in, QualityMode::kScorchedCassette);
        // LimitFeedback
        StereoFrame limited = sat.LimitFeedback(processed, QualityMode::kScorchedCassette);
        // ProcessOutput — simulates buffer readback
        StereoFrame out = qp.ProcessOutput(limited, QualityMode::kScorchedCassette);
        // Capture feedback
        feedback_sample = out;

        if (i >= settle_samples) {
            max_output = std::max(max_output, std::abs(out.l));
        }
    }

    // With 0.5 amplitude input and 1% feedback gain, output should stay
    // bounded and not grow beyond a safe margin over the input amplitude
    // (no feedback-induced runaway gain).
    REQUIRE(max_output < 0.8f);
}

TEST_CASE("QualityModes: Sunny tape wow is present but gentler than Scorched", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);
    auto span = [&](QualityMode m) {
        float lo = 2.0f, hi = 0.0f;
        for (int i = 0; i < 96000; ++i) {
            float r = qp.GetPitchModulation(m, 1);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        return hi - lo;
    };
    float sunny = span(QualityMode::kSunnyTape);
    float scorched = span(QualityMode::kScorchedCassette);
    REQUIRE(sunny > 0.0005f);            // modulation is present
    REQUIRE(sunny < scorched * 0.75f);   // gentler than full-depth Scorched
}
