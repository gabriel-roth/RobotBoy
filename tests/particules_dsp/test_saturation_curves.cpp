#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include "fx/saturation.h"

using namespace particules_dsp;
using Catch::Approx;

// The old Sunny limiter was AsymmetricSoftClip(input * 0.9f): small-signal
// gain ~0.99 positive / ~0.81 negative, i.e. a hidden ~0.9x level trim that
// made Sunny repeats decay ~0.9 dB/repeat faster than every other mode at
// the same feedback knob. The fix keeps the asymmetric tape character but
// normalizes both branches to unity small-signal slope.
TEST_CASE("Saturation: Sunny feedback limiter has unity small-signal gain") {
    Saturation sat;
    sat.Init();
    for (float x : {0.01f, -0.01f, 0.05f, -0.05f}) {
        float y = sat.LimitFeedback(x, QualityMode::kSunnyTape);
        REQUIRE(y / x == Approx(1.0f).margin(0.03f));
    }
}

TEST_CASE("Saturation: Scorched feedback limiter is soft, not a hard clip") {
    Saturation sat;
    sat.Init();
    // unity small-signal slope
    REQUIRE(sat.LimitFeedback(0.01f, QualityMode::kScorchedCassette) / 0.01f
            == Approx(1.0f).margin(0.03f));
    // A hard clip returns exactly 1.0 at 1.5; tanh returns ~0.93.
    float y = sat.LimitFeedback(1.5f, QualityMode::kScorchedCassette);
    REQUIRE(y < 0.95f);
    REQUIRE(y > 0.7f);
    // still bounded and monotonic
    float prev = -1.1f;
    for (float x = -3.f; x <= 3.f; x += 0.1f) {
        float v = sat.LimitFeedback(x, QualityMode::kScorchedCassette);
        REQUIRE(std::fabs(v) <= 1.0f);
        REQUIRE(v >= prev);
        prev = v;
    }
}

TEST_CASE("Saturation: SaturateWrite ceilings sit below the codec clamp") {
    Saturation sat;
    sat.Init();
    // Digital modes pass through untouched.
    REQUIRE(sat.SaturateWrite(1.7f, QualityMode::kBrightDigital) == 1.7f);
    REQUIRE(sat.SaturateWrite(1.7f, QualityMode::kColdDigital) == 1.7f);
    // Tape modes: unity small-signal slope (quiet repeats decay honestly)...
    for (auto mode : {QualityMode::kSunnyTape, QualityMode::kScorchedCassette}) {
        REQUIRE(sat.SaturateWrite(0.01f, mode) / 0.01f == Approx(1.0f).margin(0.05f));
        REQUIRE(sat.SaturateWrite(-0.01f, mode) / -0.01f == Approx(1.0f).margin(0.05f));
    }
    // ...but hot sums (input + feedback reaches ~2) land well under the
    // storage codec's +/-1 clamp, so the codec never hard-clips tape audio.
    REQUIRE(sat.SaturateWrite(2.0f, QualityMode::kSunnyTape) <= 0.66f);
    REQUIRE(sat.SaturateWrite(-2.0f, QualityMode::kSunnyTape) >= -0.72f);
    REQUIRE(std::fabs(sat.SaturateWrite(2.0f, QualityMode::kScorchedCassette)) <= 0.46f);
    // Sunny is asymmetric: positive peaks saturate earlier (magnetic bias).
    REQUIRE(sat.SaturateWrite(2.0f, QualityMode::kSunnyTape)
            < -sat.SaturateWrite(-2.0f, QualityMode::kSunnyTape));
}
