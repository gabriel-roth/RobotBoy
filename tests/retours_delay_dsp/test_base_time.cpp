#include <catch2/catch_amalgamated.hpp>
#include "time/base_time.h"
using namespace retours_delay_dsp;

static BaseTimeControl MakeBtc() {
    BaseTimeControl b;
    b.Init(48000.f, 4.0f);
    return b;
}

TEST_CASE("manual: noon = full buffer") {
    auto b = MakeBtc();
    auto r = b.Update(0.5f, 0.f, 0.f, false, -1, 64);
    REQUIRE(r.base_samples == Catch::Approx(192000.f).epsilon(0.01));
    REQUIRE_FALSE(r.clocked);
    REQUIRE_FALSE(r.multi_tap);
}

TEST_CASE("manual: extremes reach ~2 ms; CCW single tap, CW multi-tap") {
    auto b = MakeBtc();
    auto lo = b.Update(0.0f, 0.f, 0.f, false, -1, 64);
    REQUIRE(lo.base_samples <= 192000.f * std::exp2(-10.9f) * 1.1f);
    REQUIRE(lo.base_samples >= 0.002f * 48000.f * 0.9f);
    REQUIRE_FALSE(lo.multi_tap);
    auto hi = b.Update(1.0f, 0.f, 0.f, false, -1, 64);
    REQUIRE(hi.multi_tap);
}

TEST_CASE("density CV is -1V/oct") {
    auto b = MakeBtc();
    auto r0 = b.Update(0.25f, 0.f, 0.f, false, -1, 64);
    auto r1 = b.Update(0.25f, 1.f, 0.f, false, -1, 64);
    REQUIRE(r1.base_samples == Catch::Approx(r0.base_samples * 0.5f).epsilon(0.01));
}

TEST_CASE("clocked: interval from two ticks, subdivisions, snap multiplier") {
    auto b = MakeBtc();
    b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    // 24000 samples later (375 blocks of 64), second tick:
    for (int i = 0; i < 374; ++i) b.Update(0.5f, 0.f, 0.f, true, -1, 64);
    auto r = b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    REQUIRE(r.clocked);
    REQUIRE(r.base_samples == Catch::Approx(24000.f).epsilon(0.02));
    // fully CCW: 1/16 subdivision
    auto rdiv = b.Update(0.0f, 0.f, 0.f, true, -1, 64);
    REQUIRE(rdiv.base_samples == Catch::Approx(24000.f / 16.f).epsilon(0.02));
    // multiplier snaps: knob 0.5 → 16^0.5 = 4 → snapped 4
    auto rm = b.Update(0.5f, 0.f, 0.5f, true, -1, 64);
    REQUIRE(rm.multiplier == Catch::Approx(4.f));
}

TEST_CASE("freeze slicing math") {
    auto b = MakeBtc();
    // base = buffer/8: deviation d with exp2(-11 d)=1/8 → d=3/11
    float knob = 0.5f - 0.5f * (3.f / 11.f);
    auto r = b.Update(knob, 0.f, 1.0f, false, -1, 64);
    REQUIRE(r.slice_count == 8);
    REQUIRE(r.slice_index == 7);
}

TEST_CASE("subdivision zone hysteresis") {
    auto b = MakeBtc();
    b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    for (int i = 0; i < 374; ++i) b.Update(0.5f, 0.f, 0.f, true, -1, 64);
    b.Update(0.5f, 0.f, 0.f, true, 0, 64);
    // park knob just below a CCW zone edge, wiggle within hysteresis: no change
    auto a1 = b.Update(0.376f, 0.f, 0.f, true, -1, 64);
    auto a2 = b.Update(0.374f, 0.f, 0.f, true, -1, 64);
    REQUIRE(a1.base_samples == Catch::Approx(a2.base_samples));
}
