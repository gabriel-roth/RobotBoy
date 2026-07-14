#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "wavetable/wavetable_oscillator.h"
#include "RackWavetableProvider.hpp"

using namespace particules_dsp;

TEST_CASE("WavetableOscillator: silent without a provider", "[wavetable]") {
    WavetableOscillator osc;
    osc.Init(48000.0f);
    std::vector<StereoFrame> out(64, {1.0f, 1.0f});
    osc.Process(0.0f, 0.5f, 0.5f, out.data(), out.size());
    for (const auto& f : out) REQUIRE(f.l == 0.0f);
}

TEST_CASE("WavetableOscillator: produces bounded non-silent output", "[wavetable]") {
    RackWavetableProvider provider;
    WavetableOscillator osc;
    osc.Init(48000.0f);
    osc.SetProvider(&provider);

    // 512 frames at mid pitch / bank / position — one+ full cycle of a mid note.
    std::vector<StereoFrame> out(512, {0.0f, 0.0f});
    osc.Process(0.0f, 0.5f, 0.5f, out.data(), out.size());

    float peak = 0.0f;
    for (const auto& f : out) {
        REQUIRE(std::isfinite(f.l));
        REQUIRE(f.l == f.r);          // mono replicated to both channels
        REQUIRE(std::fabs(f.l) <= 1.0f);
        peak = std::max(peak, std::fabs(f.l));
    }
    REQUIRE(peak > 0.05f);            // not silence
}

TEST_CASE("WavetableOscillator: provider exposes the full table set", "[wavetable]") {
    RackWavetableProvider provider;
    REQUIRE(provider.NumBanksAvailable() == 24);
    REQUIRE(provider.WaveformsPerBank() == 8);
    REQUIRE(provider.GetWaveform(23, 7) != nullptr);
}
