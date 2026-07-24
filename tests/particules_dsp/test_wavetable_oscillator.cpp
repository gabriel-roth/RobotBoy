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

// Pins the setup-vs-loop split: everything computed once per Process() call
// (frequency, bank/wave region, waveform pointers) must be a pure function of
// its inputs, so calling Process() once per sample (the Ondes.cpp usage
// pattern: num_frames=1 every sample) must produce bit-identical output to
// one call rendering the whole block. True before AND after the rework.
TEST_CASE("WavetableOscillator: Process(N frames) equals N x Process(1 frame)", "[wavetable]") {
    RackWavetableProvider provider;
    constexpr size_t kFrames = 256;
    constexpr float kPitch = 7.3f, kBank = 0.4f, kWave = 0.6f;

    WavetableOscillator oscBlock;
    oscBlock.Init(48000.0f);
    oscBlock.SetProvider(&provider);
    std::vector<StereoFrame> outBlock(kFrames, {0.0f, 0.0f});
    oscBlock.Process(kPitch, kBank, kWave, outBlock.data(), kFrames);

    WavetableOscillator oscPerSample;
    oscPerSample.Init(48000.0f);
    oscPerSample.SetProvider(&provider);
    std::vector<StereoFrame> outPerSample(kFrames, {0.0f, 0.0f});
    for (size_t i = 0; i < kFrames; ++i) {
        oscPerSample.Process(kPitch, kBank, kWave, &outPerSample[i], 1);
    }

    for (size_t i = 0; i < kFrames; ++i) {
        REQUIRE(outBlock[i].l == outPerSample[i].l);
        REQUIRE(outBlock[i].r == outPerSample[i].r);
    }
}

// Guards the fmod-removal edge: with an integer-octave pitch (Exp2Fast/
// SemitonesToRatio agree exactly at whole octaves) and bank=wave=0 (so the
// output is a pure single-table read, no bilinear crossfade blending), the
// output must stay bounded and never jump farther between consecutive
// samples than the phase step could possibly explain from the raw table's
// own sample-to-sample variation. A broken wrap (e.g. an off-by-one that
// resets phase_ to the wrong value) would show up as a much larger jump.
TEST_CASE("WavetableOscillator: static-pitch output is periodic after rework (phase wrap exact)", "[wavetable]") {
    RackWavetableProvider provider;
    WavetableOscillator osc;
    osc.Init(48000.0f);
    osc.SetProvider(&provider);

    constexpr size_t kFrames = 4096;
    std::vector<StereoFrame> out(kFrames, {0.0f, 0.0f});
    osc.Process(12.0f, 0.0f, 0.0f, out.data(), kFrames);

    for (const auto& f : out) {
        REQUIRE(std::isfinite(f.l));
        REQUIRE(std::fabs(f.l) <= 1.5f);
    }

    // Max sample-to-sample delta of the raw table itself (bank 0, wave 0),
    // including the wraparound edge.
    const float* raw = provider.GetWaveform(0, 0);
    REQUIRE(raw != nullptr);
    float maxRawDelta = 0.0f;
    for (int i = 0; i < kWavetableSize; ++i) {
        int next = (i + 1) & (kWavetableSize - 1);
        maxRawDelta = std::max(maxRawDelta, std::fabs(raw[next] - raw[i]));
    }

    // phase_increment_ = kBaseFreq * ratio(12 semitones = 2x) * kWavetableSize / sampleRate.
    constexpr float kBaseFreq = 261.63f;
    float phaseIncrement = kBaseFreq * 2.0f * static_cast<float>(kWavetableSize) / 48000.0f;
    float allowedDelta = maxRawDelta * (std::ceil(phaseIncrement) + 1.0f);

    for (size_t i = 1; i < out.size(); ++i) {
        REQUIRE(std::fabs(out[i].l - out[i - 1].l) <= allowedDelta);
    }
}
