#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "WavetableFrame.hpp"
#include "particules_dsp/types.h"

using Catch::Approx;

namespace {
// 2 banks x 2 waves; each waveform is a distinct constant so blends are easy to
// reason about. Arrays are kWavetableSize long (contract), constant-valued.
struct FakeProvider : particules_dsp::WavetableProvider {
    float w[2][2][particules_dsp::kWavetableSize];
    FakeProvider() {
        for (int b = 0; b < 2; ++b)
            for (int v = 0; v < 2; ++v) {
                float val = float(b * 2 + v);  // b0v0=0, b0v1=1, b1v0=2, b1v1=3
                for (int i = 0; i < particules_dsp::kWavetableSize; ++i) w[b][v][i] = val;
            }
    }
    const float* GetWaveform(int bank, int index) const override { return w[bank][index]; }
    int NumBanksAvailable() const override { return 2; }
    int WaveformsPerBank() const override { return 2; }
};
}

TEST_CASE("wavetableFrameSample returns exact stored sample at integer corners") {
    FakeProvider p;
    REQUIRE(robotboy::wavetableFrameSample(p, 0.f, 0.f, 0) == Approx(0.f));
    REQUIRE(robotboy::wavetableFrameSample(p, 0.f, 1.f, 5) == Approx(1.f));
    REQUIRE(robotboy::wavetableFrameSample(p, 1.f, 0.f, 9) == Approx(2.f));
    REQUIRE(robotboy::wavetableFrameSample(p, 1.f, 1.f, 200) == Approx(3.f));
}

TEST_CASE("wavetableFrameSample bilinearly blends between corners") {
    FakeProvider p;
    // wave midpoint at bank 0: halfway between b0v0=0 and b0v1=1 -> 0.5
    REQUIRE(robotboy::wavetableFrameSample(p, 0.f, 0.5f, 0) == Approx(0.5f));
    // bank midpoint at wave 0: halfway between b0v0=0 and b1v0=2 -> 1.0
    REQUIRE(robotboy::wavetableFrameSample(p, 0.5f, 0.f, 0) == Approx(1.0f));
    // center: mean of {0,1,2,3} -> 1.5
    REQUIRE(robotboy::wavetableFrameSample(p, 0.5f, 0.5f, 0) == Approx(1.5f));
}

TEST_CASE("wavetableFrameSample clamps out-of-range normalized inputs") {
    FakeProvider p;
    REQUIRE(robotboy::wavetableFrameSample(p, -1.f, 2.f, 0) == Approx(1.f)); // -> b0v1=1
}
