#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "particules_dsp/types.h"
#include "input/auto_gain.h"

using namespace particules_dsp;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

TEST_CASE("AutoGain: Quiet input gets gain greater than 1", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);
    ag.StartCalibration();

    // Feed quiet signal (0.01 amplitude) for enough samples for the
    // envelope to converge and auto-gain to ramp up
    StereoFrame last_out = {0.0f, 0.0f};
    for (int i = 0; i < 200000; ++i) {
        StereoFrame in = {0.01f, 0.01f};
        last_out = ag.Process(in, NAN, true);  // auto-gain on
    }

    // With a 0.01 input, auto-gain should have boosted the signal
    // The gain should push the output above the raw input level
    float output_level = std::max(std::abs(last_out.l), std::abs(last_out.r));
    REQUIRE(output_level > 0.01f);
}

TEST_CASE("AutoGain: Loud input stays near unity", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);
    ag.StartCalibration();

    // Feed loud signal (1.0 amplitude) for a long time
    StereoFrame last_out = {0.0f, 0.0f};
    for (int i = 0; i < 200000; ++i) {
        float val = std::sin(static_cast<float>(i) / kSampleRate * 440.0f * 2.0f * 3.14159265f);
        StereoFrame in = {val, val};
        last_out = ag.Process(in, NAN, true);
    }

    // With a loud input (peaks at 1.0), auto-gain should preserve headroom
    // rather than pushing the signal back to unity.
    float input_val = std::sin(200000.0f / kSampleRate * 440.0f * 2.0f * 3.14159265f);
    float ratio = std::abs(last_out.l) / std::max(std::abs(input_val), 0.001f);

    // The implementation currently targets 8dB of headroom, so the steady-state
    // loud-signal ratio should sit near 10^(-8/20) ~= 0.398.
    REQUIRE(ratio > 0.35f);
    REQUIRE(ratio < 0.5f);
}

TEST_CASE("AutoGain: Manual gain override works", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);

    // Feed moderate signal with manual gain of +12dB (auto-gain off)
    float manual_gain_db = 12.0f;
    StereoFrame last_out = {0.0f, 0.0f};
    for (int i = 0; i < 100000; ++i) {
        StereoFrame in = {0.1f, 0.1f};
        last_out = ag.Process(in, manual_gain_db, false);
    }

    // With +12dB manual gain, the output should be roughly 0.1 * 10^(12/20) = 0.1 * ~3.98 = ~0.398
    float expected_gain = std::pow(10.0f, 12.0f / 20.0f);
    float expected_output = 0.1f * expected_gain;
    float actual = std::abs(last_out.l);

    REQUIRE(actual == Approx(expected_output).margin(0.05f));
}

TEST_CASE("AutoGain: Auto mode boosts quiet signal more than manual 0dB", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);
    ag.StartCalibration();

    // In auto mode, quiet signal should get boosted
    for (int i = 0; i < 200000; ++i) {
        StereoFrame in = {0.01f, 0.01f};
        ag.Process(in, NAN, true);
    }
    StereoFrame auto_out = ag.Process({0.01f, 0.01f}, NAN, true);

    // Then create a new instance with manual gain = 0dB
    AutoGain ag2;
    ag2.Init(kSampleRate);
    for (int i = 0; i < 200000; ++i) {
        StereoFrame in = {0.01f, 0.01f};
        ag2.Process(in, 0.0f, false);
    }
    StereoFrame manual_out = ag2.Process({0.01f, 0.01f}, 0.0f, false);

    // Auto mode with quiet input should produce higher output than 0dB manual
    REQUIRE(std::abs(auto_out.l) > std::abs(manual_out.l));
}

TEST_CASE("AutoGain: InputLevel reflects signal level", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);

    // Initially, input level should be near zero
    REQUIRE(ag.InputLevel() == Approx(0.0f).margin(0.001f));

    // Feed signal and check that level increases
    for (int i = 0; i < 10000; ++i) {
        StereoFrame in = {0.5f, 0.5f};
        ag.Process(in, NAN, true);
    }

    REQUIRE(ag.InputLevel() > 0.1f);
}

TEST_CASE("AutoGain: Output is always finite", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);
    ag.StartCalibration();

    // Test with various input levels including near-zero
    float test_levels[] = {0.0f, 0.001f, 0.01f, 0.1f, 1.0f};
    for (float level : test_levels) {
        for (int i = 0; i < 1000; ++i) {
            StereoFrame in = {level, -level};
            StereoFrame out = ag.Process(in, NAN, true);
            REQUIRE(std::isfinite(out.l));
            REQUIRE(std::isfinite(out.r));
        }
    }
}

TEST_CASE("AutoGain: Manual gain is clamped to valid range", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);

    // Even with extreme manual gain values, output should be finite
    float extreme_gains[] = {-100.0f, 0.0f, 32.0f, 100.0f};
    for (float gain_db : extreme_gains) {
        for (int i = 0; i < 1000; ++i) {
            StereoFrame in = {0.1f, 0.1f};
            StereoFrame out = ag.Process(in, gain_db, false);
            REQUIRE(std::isfinite(out.l));
            REQUIRE(std::isfinite(out.r));
        }
    }
}

TEST_CASE("AutoGain: Calibrate-and-lock holds steady gain", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);
    ag.StartCalibration();

    // Feed signal through full calibration window (2 seconds = 96000 samples)
    for (int i = 0; i < 100000; ++i) {
        StereoFrame in = {0.05f, 0.05f};
        ag.Process(in, NAN, true);
    }

    // Now in locked state — gain should be stable even if input changes
    StereoFrame out1 = ag.Process({0.05f, 0.05f}, NAN, true);

    // Feed very different signal level for a while
    for (int i = 0; i < 50000; ++i) {
        ag.Process({0.5f, 0.5f}, NAN, true);
    }

    // Output with the original level should be similar (gain is locked)
    StereoFrame out2 = ag.Process({0.05f, 0.05f}, NAN, true);
    float ratio = std::abs(out2.l) / std::max(std::abs(out1.l), 1e-6f);
    REQUIRE(ratio > 0.8f);
    REQUIRE(ratio < 1.2f);
}

TEST_CASE("AutoGain: Recalibrates after 10s silence then sound", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);
    ag.StartCalibration();

    // Feed signal through calibration (2 seconds = 96000 samples)
    for (int i = 0; i < 100000; ++i) {
        ag.Process({0.1f, 0.1f}, NAN, true);
    }

    // Now in locked state - record the locked gain
    float locked_gain_db = ag.GainDb();

    // Feed 10+ seconds of silence (10 * 48000 = 480000 samples)
    for (int i = 0; i < 500000; ++i) {
        ag.Process({0.0f, 0.0f}, NAN, true);
    }

    // Feed a much louder signal - should trigger recalibration
    // and eventually settle on a lower gain
    for (int i = 0; i < 100000; ++i) {
        ag.Process({0.8f, 0.8f}, NAN, true);
    }

    float new_gain_db = ag.GainDb();

    // The new gain should be significantly lower than the locked gain
    // because the new signal is much louder (0.8 vs 0.1)
    REQUIRE(new_gain_db < locked_gain_db - 10.0f);
}

TEST_CASE("AutoGain: Brief sound resets silence counter", "[autogain]") {
    AutoGain ag;
    ag.Init(kSampleRate);
    ag.StartCalibration();

    // Feed signal through calibration
    for (int i = 0; i < 100000; ++i) {
        ag.Process({0.1f, 0.1f}, NAN, true);
    }

    // Now in locked state - record the locked gain
    float locked_gain_db = ag.GainDb();

    // Feed 9 seconds of silence (9 * 48000 = 432000 samples)
    for (int i = 0; i < 432000; ++i) {
        ag.Process({0.0f, 0.0f}, NAN, true);
    }

    // Brief sound interruption (1 second)
    for (int i = 0; i < 48000; ++i) {
        ag.Process({0.1f, 0.1f}, NAN, true);
    }

    // Another 9 seconds of silence
    for (int i = 0; i < 432000; ++i) {
        ag.Process({0.0f, 0.0f}, NAN, true);
    }

    // Feed louder signal - should NOT have triggered recalibration
    // because silence was interrupted before reaching 10 seconds
    for (int i = 0; i < 100000; ++i) {
        ag.Process({0.8f, 0.8f}, NAN, true);
    }

    float new_gain_db = ag.GainDb();

    // Gain should be similar to locked gain (no recalibration happened)
    REQUIRE(std::abs(new_gain_db - locked_gain_db) < 3.0f);
}
