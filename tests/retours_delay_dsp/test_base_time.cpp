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

// Establish a tapped tempo of `interval_samples` with NO clock cable
// (clock_connected=false), i.e. via the tap-tempo button path. Leaves the
// control clocked at that interval.
static void EstablishTappedTempo(BaseTimeControl& b, int interval_samples) {
    int blocks = interval_samples / 64;
    b.Update(0.5f, 0.f, 0.f, /*clock_connected=*/false, 0, 64);        // tick 1
    for (int i = 0; i < blocks - 1; ++i)
        b.Update(0.5f, 0.f, 0.f, false, -1, 64);
    b.Update(0.5f, 0.f, 0.f, false, 0, 64);                            // tick 2
}

TEST_CASE("tapped tempo holds indefinitely (no timeout without a cable)") {
    auto b = MakeBtc();
    EstablishTappedTempo(b, 24000);
    REQUIRE(b.IsClocked());
    float base_locked = b.BaseSeconds();
    // Run well past the old 5 s timeout (>7 s) with no further taps, no cable.
    for (int i = 0; i < 7 * 48000 / 64; ++i)
        b.Update(0.5f, 0.f, 0.f, false, -1, 64);
    REQUIRE(b.IsClocked());
    REQUIRE(b.BaseSeconds() == Catch::Approx(base_locked));
}

TEST_CASE("tapped tempo survives a large Interval move") {
    auto b = MakeBtc();
    EstablishTappedTempo(b, 24000);
    REQUIRE(b.IsClocked());
    // Sweep Interval hard from noon to fully CCW: previously this abandoned
    // the tapped tempo; now it just re-selects the subdivision.
    b.Update(0.0f, 0.f, 0.f, false, -1, 64);
    REQUIRE(b.IsClocked());
}

TEST_CASE("ClearClock returns to free-running") {
    auto b = MakeBtc();
    EstablishTappedTempo(b, 24000);
    REQUIRE(b.IsClocked());
    b.ClearClock();
    auto r = b.Update(0.5f, 0.f, 0.f, false, -1, 64);
    REQUIRE_FALSE(r.clocked);
    REQUIRE_FALSE(b.IsClocked());
    // noon, free-running = full buffer again
    REQUIRE(r.base_samples == Catch::Approx(192000.f).epsilon(0.01));
    REQUIRE(b.ClockIntervalSeconds() == 0.f);
}

TEST_CASE("ClockIntervalSeconds reports the measured beat") {
    auto b = MakeBtc();
    EstablishTappedTempo(b, 24000);   // 24000 samples @ 48k = 0.5 s
    REQUIRE(b.ClockIntervalSeconds() == Catch::Approx(0.5f).epsilon(0.02));
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
