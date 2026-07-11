#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "particules_dsp/parameters.h"

using namespace particules_dsp;
using Catch::Approx;

TEST_CASE("QuantizePitchLock: mode 0 is pass-through", "[pitch_lock]") {
    REQUIRE(QuantizePitchLock(0.0f,   0) == Approx(0.0f));
    REQUIRE(QuantizePitchLock(7.0f,   0) == Approx(7.0f));
    REQUIRE(QuantizePitchLock(-5.0f,  0) == Approx(-5.0f));
    REQUIRE(QuantizePitchLock(24.3f,  0) == Approx(24.3f));
}

TEST_CASE("QuantizePitchLock: mode 1 snaps to nearest octave", "[pitch_lock]") {
    // Exact multiples unchanged
    REQUIRE(QuantizePitchLock(0.0f,   1) == Approx(0.0f));
    REQUIRE(QuantizePitchLock(12.0f,  1) == Approx(12.0f));
    REQUIRE(QuantizePitchLock(-12.0f, 1) == Approx(-12.0f));
    REQUIRE(QuantizePitchLock(24.0f,  1) == Approx(24.0f));

    // Closer to lower octave
    REQUIRE(QuantizePitchLock(5.9f,   1) == Approx(0.0f));
    REQUIRE(QuantizePitchLock(-5.9f,  1) == Approx(0.0f));
    REQUIRE(QuantizePitchLock(11.9f,  1) == Approx(12.0f));

    // Closer to upper octave
    REQUIRE(QuantizePitchLock(6.1f,   1) == Approx(12.0f));
    REQUIRE(QuantizePitchLock(-6.1f,  1) == Approx(-12.0f));

    // Exactly halfway: std::round rounds away from zero
    // round(6/12) = round(0.5) = 1.0 → 12
    REQUIRE(QuantizePitchLock(6.0f,   1) == Approx(12.0f));
    // round(-6/12) = round(-0.5) = -1.0 → -12
    REQUIRE(QuantizePitchLock(-6.0f,  1) == Approx(-12.0f));
}

TEST_CASE("QuantizePitchLock: mode 2 snaps to nearest octave or fifth", "[pitch_lock]") {
    // Exact candidates unchanged
    REQUIRE(QuantizePitchLock(0.0f,   2) == Approx(0.0f));
    REQUIRE(QuantizePitchLock(7.0f,   2) == Approx(7.0f));
    REQUIRE(QuantizePitchLock(12.0f,  2) == Approx(12.0f));
    REQUIRE(QuantizePitchLock(19.0f,  2) == Approx(19.0f));
    REQUIRE(QuantizePitchLock(24.0f,  2) == Approx(24.0f));

    // base=0, candidates={0,7,12,19}
    // 3 semitones: dist 3,4,9,16 → nearest=0
    REQUIRE(QuantizePitchLock(3.0f,   2) == Approx(0.0f));
    // 4 semitones: dist 4,3,8,15 → nearest=7
    REQUIRE(QuantizePitchLock(4.0f,   2) == Approx(7.0f));
    // 10 semitones: dist 10,3,2,9 → nearest=12
    REQUIRE(QuantizePitchLock(10.0f,  2) == Approx(12.0f));

    // base=12, candidates={12,19,24,31}
    // 15 semitones: dist 3,4,9,16 → nearest=12
    REQUIRE(QuantizePitchLock(15.0f,  2) == Approx(12.0f));
    // 16 semitones: dist 4,3,8,15 → nearest=19
    REQUIRE(QuantizePitchLock(16.0f,  2) == Approx(19.0f));

    // Negative: base = floor(-5/12)*12 = floor(-0.417)*12 = -12
    // candidates = {-12,-5,0,7}
    // -5 semitones: dist 7,0,5,12 → nearest=-5
    REQUIRE(QuantizePitchLock(-5.0f,  2) == Approx(-5.0f));
    // -1 semitone: dist 11,4,1,8 → nearest=0
    REQUIRE(QuantizePitchLock(-1.0f,  2) == Approx(0.0f));
    // -12 semitones: exact
    REQUIRE(QuantizePitchLock(-12.0f, 2) == Approx(-12.0f));

    // Tie cases (equidistant): strict < in implementation means lower candidate wins
    // 3.5 st: equidistant between 0 and 7 → lower candidate (0) wins
    REQUIRE(QuantizePitchLock(3.5f,   2) == Approx(0.0f));
    // 9.5 st: equidistant between 7 and 12 → lower candidate (7) wins
    REQUIRE(QuantizePitchLock(9.5f,   2) == Approx(7.0f));
}
