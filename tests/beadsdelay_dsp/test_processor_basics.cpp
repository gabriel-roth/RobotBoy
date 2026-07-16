#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <vector>
#include "beadsdelay_dsp/echos_dsp.h"

using namespace beadsdelay_dsp;

namespace {
struct Proc {
    void* mem = nullptr;
    EchosProcessor p;
    explicit Proc(float sr = 48000.f) {
        auto req = EchosProcessor::GetMemoryRequirements(sr);
        REQUIRE(req.total_bytes > 0);
        posix_memalign(&mem, req.alignment, req.total_bytes);
        p.Init(mem, req.total_bytes, sr);
    }
    ~Proc() { std::free(mem); }
};
} // namespace

TEST_CASE("silence in, silence out") {
    Proc proc;
    EchosParameters params;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(4800, StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out(4800, StereoFrame{1.f, 1.f});
    proc.p.Process(in.data(), out.data(), in.size());
    for (auto& f : out) {
        REQUIRE(f.l == Catch::Approx(0.f).margin(1e-6));
        REQUIRE(f.r == Catch::Approx(0.f).margin(1e-6));
    }
}

TEST_CASE("null memory Init is safe") {
    EchosProcessor p;
    p.Init(nullptr, 0, 48000.f);
    EchosParameters params;
    p.SetParameters(params);
    StereoFrame in{1.f, 1.f}, out{};
    p.Process(&in, &out, 1);   // must not crash
    REQUIRE(out.l == 0.f);
}

TEST_CASE("dry passthrough at dry_wet 0") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 0.f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(256), out(256);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = {std::sin(0.1f * i), std::cos(0.1f * i)};
    proc.p.Process(in.data(), out.data(), in.size());
    // allow smoothing settle over first 128 frames
    for (size_t i = 128; i < out.size(); ++i)
        REQUIRE(out[i].l == Catch::Approx(in[i].l).margin(0.02));
}

// Fix round (final review): input trim sits at the very front of the signal
// flow per spec, so it must also apply to the dry tap in the final mix, not
// just the path written to the delay buffer (echos_processor.cpp's to_write
// computation already applied it there). -12 dB -> gain 10^(-12/20) ~=
// 0.2512.
TEST_CASE("input trim also applies to the dry tap") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 0.f;         // dry only
    params.input_trim_db = -12.f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(256), out(256);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = {std::sin(0.1f * i), std::cos(0.1f * i)};
    proc.p.Process(in.data(), out.data(), in.size());
    const float expected_gain = 0.25119f;  // 10^(-12/20)
    for (size_t i = 128; i < out.size(); ++i) {
        REQUIRE(out[i].l == Catch::Approx(in[i].l * expected_gain).margin(0.01f));
        REQUIRE(out[i].r == Catch::Approx(in[i].r * expected_gain).margin(0.01f));
    }
}
