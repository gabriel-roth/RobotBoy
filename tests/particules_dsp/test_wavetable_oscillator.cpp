#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
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

// Render one block at a fixed pitch for a given bank/position. Each call uses a
// fresh oscillator so the phase trajectory is identical across settings — any
// difference in the output then comes purely from bank/position selection.
static std::vector<float> renderMono(RackWavetableProvider& provider,
                                     float bank, float position) {
    WavetableOscillator osc;
    osc.Init(48000.0f);
    osc.SetProvider(&provider);
    std::vector<StereoFrame> out(512, {0.0f, 0.0f});
    osc.Process(0.0f, bank, position, out.data(), out.size());
    std::vector<float> mono(out.size());
    for (size_t i = 0; i < out.size(); ++i) mono[i] = out[i].l;
    return mono;
}

static double sumAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double d = 0.0;
    for (size_t i = 0; i < a.size(); ++i) d += std::fabs(a[i] - b[i]);
    return d;
}

TEST_CASE("WavetableOscillator: Position selects different waveforms within a bank", "[wavetable]") {
    RackWavetableProvider provider;
    auto a = renderMono(provider, 0.0f, 0.0f);   // bank 0, first waveform
    auto b = renderMono(provider, 0.0f, 1.0f);   // bank 0, last waveform
    REQUIRE(sumAbsDiff(a, b) > 1.0);             // clearly distinct output
}

TEST_CASE("WavetableOscillator: Bank selects different timbral families", "[wavetable]") {
    RackWavetableProvider provider;
    auto a = renderMono(provider, 0.0f, 0.5f);   // first bank
    auto b = renderMono(provider, 1.0f, 0.5f);   // last bank
    REQUIRE(sumAbsDiff(a, b) > 1.0);
}

TEST_CASE("WavetableOscillator: boundary bank/position stay bounded", "[wavetable]") {
    RackWavetableProvider provider;
    for (auto pr : {std::pair<float, float>{0.0f, 0.0f}, {1.0f, 1.0f}}) {
        auto mono = renderMono(provider, pr.first, pr.second);
        for (float s : mono) {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::fabs(s) <= 1.0f);
        }
    }
}
