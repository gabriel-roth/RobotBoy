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
