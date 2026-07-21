#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>

#include "buffer/sample_codec.h"

using namespace particules_dsp;
using Catch::Approx;

TEST_CASE("Int12: zero, clamping, NaN", "[codec]") {
    REQUIRE(Int12Encode(0.0f) == 0);
    REQUIRE(Int12Decode(0) == 0.0f);
    REQUIRE(Int12Encode(1.0f) == 2047);
    REQUIRE(Int12Encode(-1.0f) == -2047);
    REQUIRE(Int12Encode(5.0f) == 2047);
    REQUIRE(Int12Encode(-5.0f) == -2047);
    REQUIRE(Int12Encode(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("Int12: roundtrip error within half a step", "[codec]") {
    for (float x = -1.0f; x <= 1.0f; x += 0.001f) {
        float y = Int12Decode(Int12Encode(x));
        REQUIRE(std::fabs(y - x) <= 0.5f / 2047.0f + 1e-6f);
    }
}

TEST_CASE("MuLaw8: zero codes decode to exact silence", "[codec]") {
    REQUIRE(MuLaw8Encode(0.0f) == 0);
    REQUIRE(MuLaw8Encode(-0.0f) == 0);
    REQUIRE(MuLaw8Decode(0x00) == 0.0f);
    REQUIRE(MuLaw8Decode(0x80) == 0.0f);     // negative zero code
    REQUIRE(MuLaw8Encode(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("MuLaw8: full scale and clamping", "[codec]") {
    REQUIRE(MuLaw8Decode(MuLaw8Encode(1.0f)) == Approx(1.0f).margin(0.02f));
    REQUIRE(MuLaw8Decode(MuLaw8Encode(-1.0f)) == Approx(-1.0f).margin(0.02f));
    REQUIRE(MuLaw8Encode(3.0f) == MuLaw8Encode(1.0f));
}

TEST_CASE("MuLaw8: sign symmetry and bounds over all codes", "[codec]") {
    for (int c = 0; c < 128; ++c) {
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c | 0x80)) ==
                Approx(-MuLaw8Decode(static_cast<uint8_t>(c))).margin(1e-9f));
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c)) >= 0.0f);
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c)) <= 1.0f);
    }
}

TEST_CASE("MuLaw8: decode is monotonic in magnitude", "[codec]") {
    for (int c = 0; c < 127; ++c) {
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c + 1)) >
                MuLaw8Decode(static_cast<uint8_t>(c)));
    }
}

TEST_CASE("MuLaw8: encode(decode(c)) is the identity on codes", "[codec]") {
    // Idempotent re-quantization: re-recording an already-quantized signal
    // (freeze crossfade, feedback loop) must not drift.
    for (int c = 0; c < 256; ++c) {
        uint8_t code = static_cast<uint8_t>(c);
        uint8_t rt = MuLaw8Encode(MuLaw8Decode(code));
        if (code == 0x80) { REQUIRE(rt == 0x00); }   // negative zero
        else              { REQUIRE(rt == code); }
    }
}

TEST_CASE("MuLaw8: roundtrip relative error within segment bound", "[codec]") {
    // Segment mu-law: max relative error ~1/32 inside segments, worse only
    // in the near-zero linear region.
    for (float x = 0.02f; x <= 1.0f; x += 0.005f) {
        float y = MuLaw8Decode(MuLaw8Encode(x));
        REQUIRE(std::fabs(y - x) / x < 0.07f);
    }
}
