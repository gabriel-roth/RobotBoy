#include <catch2/catch_amalgamated.hpp>
#include "classic_wet_filter.h"
#include "retours_delay_dsp/retours_dsp.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using retours_delay_dsp::StereoFrame;

static std::vector<StereoFrame> RunProcessor(retours_delay_dsp::WetFilter* f, float feedback) {
    using retours_delay_dsp::RetoursProcessor;
    const float sr = 48000.f;
    auto req = RetoursProcessor::GetMemoryRequirements(sr);
    std::vector<unsigned char> mem(req.total_bytes + req.alignment);
    void* p = mem.data();
    size_t space = mem.size();
    std::align(req.alignment, req.total_bytes, p, space);
    RetoursProcessor proc;
    proc.Init(p, req.total_bytes, sr);
    proc.SetWetFilter(f);
    retours_delay_dsp::RetoursParameters prm{};
    // density > noon: short delay (~0.2 s), so repeats land inside the window
    prm.feedback = feedback; prm.dry_wet = 1.f; prm.density = 0.75f;
    proc.SetParameters(prm);
    std::vector<StereoFrame> in(48000), out(48000);
    for (size_t i = 0; i < in.size(); ++i) {
        float s = std::sin(2.f * 3.14159265f * 440.f * i / sr) * ((i < 4800) ? 0.5f : 0.f);
        in[i] = {s, s};
    }
    for (size_t i = 0; i < in.size(); i += 64) proc.Process(&in[i], &out[i], 64);
    return out;
}

TEST_CASE("null hook and dead-band filter are bit-identical to no filter") {
    auto ref = RunProcessor(nullptr, 0.6f);
    retours_classic::BandwidthFilter f;
    f.Init(48000.f);
    f.SetBandwidth(0.5f);
    REQUIRE_FALSE(f.active());
    auto got = RunProcessor(&f, 0.6f);
    REQUIRE(std::memcmp(ref.data(), got.data(), ref.size() * sizeof(StereoFrame)) == 0);
}

TEST_CASE("an engaged filter changes the output") {
    auto ref = RunProcessor(nullptr, 0.6f);
    retours_classic::BandwidthFilter f;
    f.Init(48000.f);
    f.SetBandwidth(0.f);
    auto got = RunProcessor(&f, 0.6f);
    REQUIRE(std::memcmp(ref.data(), got.data(), ref.size() * sizeof(StereoFrame)) != 0);
}

static float SineGain(float bandwidth, float hz, float sr) {
    retours_classic::BandwidthFilter f;
    f.Init(sr);
    f.SetBandwidth(bandwidth);
    double in_e = 0, out_e = 0;
    for (int i = 0; i < int(sr); ++i) {
        float s = std::sin(2.f * 3.14159265f * hz * i / sr);
        StereoFrame fr{s, s};
        f.Process(fr);
        if (i > sr / 2) { in_e += s * s; out_e += fr.l * fr.l; }
    }
    return float(std::sqrt(out_e / in_e));
}

TEST_CASE("full CCW is low-pass and full CW is high-pass") {
    REQUIRE(SineGain(0.f, 5000.f, 48000.f) < 0.5f * SineGain(0.f, 200.f, 48000.f));
    REQUIRE(SineGain(1.f, 200.f, 48000.f) < 0.5f * SineGain(1.f, 5000.f, 48000.f));
}

TEST_CASE("the response doesn't depend on the host rate") {
    for (float bw : {0.1f, 0.3f, 0.7f, 0.9f})
        for (float hz : {200.f, 1000.f, 5000.f})
            REQUIRE(SineGain(bw, hz, 96000.f) == Catch::Approx(SineGain(bw, hz, 48000.f)).margin(0.05));
}
