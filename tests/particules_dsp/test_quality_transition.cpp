#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "particules_dsp/particules_dsp.h"
#include "particules_dsp/types.h"
#include "particules_dsp/parameters.h"

using namespace particules_dsp;

namespace {

constexpr float kSr = 48000.0f;

struct Proc {
    std::vector<uint8_t> mem;
    ParticulesProcessor p;
    Proc() {
        auto req = ParticulesProcessor::GetMemoryRequirements(kSr);
        mem.resize(req.total_bytes + req.alignment, 0);
        p.Init(mem.data(), mem.size(), kSr);
    }
};

struct RunResult { float peak; bool finite; };
RunResult Run(ParticulesProcessor& p, ParticulesParameters& params,
              int blocks, double& phase) {
    RunResult r{0.0f, true};
    StereoFrame in[64], out[64];
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 64; ++i) {
            float v = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * M_PI * 220.0 / kSr;
            in[i] = {v, v};
        }
        p.SetParameters(params);
        p.Process(in, out, 64);
        for (int i = 0; i < 64; ++i) {
            if (!std::isfinite(out[i].l) || !std::isfinite(out[i].r)) r.finite = false;
            r.peak = std::max(r.peak, std::max(std::fabs(out[i].l), std::fabs(out[i].r)));
        }
    }
    return r;
}

ParticulesParameters WetParams() {
    ParticulesParameters params{};
    params.dry_wet = 1.0f;
    params.density = 0.8f;
    params.size = 0.0f;
    // TIME near the write head: peak checks below want to know "does wet
    // output resume/continue", not "has a (possibly 32 s) buffer refilled
    // to some arbitrary depth" -- the latter needs far more real time than
    // these runs cover and isn't what these tests are probing.
    params.time = 0.0f;
    params.quality_mode = QualityMode::kBrightDigital;
    return params;
}

}  // namespace

TEST_CASE("QualityTransition: Bright->Scorched stays finite and recovers", "[transition]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;

    auto settled = Run(proc.p, params, 800, phase);   // past grain startup ramp
    REQUIRE(settled.finite);
    REQUIRE(settled.peak > 0.01f);

    params.quality_mode = QualityMode::kScorchedCassette;
    auto transition = Run(proc.p, params, 300, phase);   // ~400 ms: full transition
    REQUIRE(transition.finite);

    auto after = Run(proc.p, params, 1500, phase);
    REQUIRE(after.finite);
    REQUIRE(after.peak > 0.01f);
}

TEST_CASE("QualityTransition: Scorched->Bright shrink is safe", "[transition]") {
    Proc proc;
    auto params = WetParams();
    params.quality_mode = QualityMode::kScorchedCassette;
    params.time = 0.9f;   // grains read deep into the long buffer
    double phase = 0.0;

    auto scorched = Run(proc.p, params, 1500, phase);
    REQUIRE(scorched.finite);

    params.quality_mode = QualityMode::kBrightDigital;   // 768k -> 192k frames
    // Deep TIME served its purpose above (stress the shrink while reading
    // far into the long buffer); reset it so the recovery check below
    // reflects "wet resumes", not "the 4 s buffer has refilled to 90% depth".
    params.time = 0.0f;
    auto back = Run(proc.p, params, 1500, phase);
    REQUIRE(back.finite);
    REQUIRE(back.peak > 0.01f);
}

TEST_CASE("QualityTransition: mono/stereo input change reconfigures safely", "[transition][mono]") {
    Proc proc;
    auto params = WetParams();
    params.quality_mode = QualityMode::kScorchedCassette;
    double phase = 0.0;
    Run(proc.p, params, 1200, phase);

    params.mono_input = true;    // "cable unplugged": 768k -> 1.5M mono frames
    auto mono = Run(proc.p, params, 1500, phase);
    REQUIRE(mono.finite);
    REQUIRE(mono.peak > 0.01f);

    params.mono_input = false;   // re-patched
    auto stereo = Run(proc.p, params, 1500, phase);
    REQUIRE(stereo.finite);
    REQUIRE(stereo.peak > 0.01f);
}

TEST_CASE("QualityTransition: rapid mode cycling stays finite", "[transition]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;
    Run(proc.p, params, 200, phase);
    const QualityMode cycle[] = {
        QualityMode::kColdDigital, QualityMode::kScorchedCassette,
        QualityMode::kSunnyTape, QualityMode::kBrightDigital,
        QualityMode::kScorchedCassette, QualityMode::kColdDigital};
    for (QualityMode m : cycle) {
        params.quality_mode = m;
        auto r = Run(proc.p, params, 20, phase);   // re-flip mid-transition
        REQUIRE(r.finite);
    }
    params.quality_mode = QualityMode::kBrightDigital;
    auto r = Run(proc.p, params, 2000, phase);
    REQUIRE(r.finite);
    REQUIRE(r.peak > 0.01f);
}

TEST_CASE("QualityTransition: change while frozen is deferred", "[transition][freeze]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;
    Run(proc.p, params, 800, phase);          // record content in Bright

    params.freeze = true;
    Run(proc.p, params, 100, phase);

    params.quality_mode = QualityMode::kScorchedCassette;
    auto frozen = Run(proc.p, params, 300, phase);
    REQUIRE(frozen.finite);
    REQUIRE(frozen.peak > 0.01f);             // no transition mute while frozen

    params.freeze = false;
    auto after = Run(proc.p, params, 2000, phase);
    REQUIRE(after.finite);
    REQUIRE(after.peak > 0.01f);
}
