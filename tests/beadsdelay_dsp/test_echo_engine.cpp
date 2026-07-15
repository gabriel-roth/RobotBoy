#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <vector>
#include <cmath>
#include "beadsdelay_dsp/echos_dsp.h"
using namespace beadsdelay_dsp;

namespace {
struct Proc {
    void* mem = nullptr; EchosProcessor p;
    explicit Proc(float sr = 48000.f) {
        auto req = EchosProcessor::GetMemoryRequirements(sr);
        posix_memalign(&mem, req.alignment, req.total_bytes);
        p.Init(mem, req.total_bytes, sr);
    }
    ~Proc() { std::free(mem); }
};
// density knob for a target base time (manual mode inverse mapping)
float KnobForSeconds(float seconds, float buffer_seconds = 4.f) {
    float d = -std::log2(seconds / buffer_seconds) / 11.0f; // kManualOctaves
    return 0.5f - 0.5f * d;   // CCW side
}
int FindPeak(const std::vector<StereoFrame>& v, int from, int to = -1) {
    if (to < 0 || to > (int)v.size()) to = (int)v.size();
    int best = from; float mag = 0.f;
    for (int i = from; i < to; ++i)
        if (std::fabs(v[i].l) > mag) { mag = std::fabs(v[i].l); best = i; }
    return best;
}
} // namespace

TEST_CASE("impulse comes back at the set delay time") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f;          // wet only
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.1f);   // 100 ms → 4800 samples
    params.time = 0.f;             // 1× multiplier
    proc.p.SetParameters(params);
    // settle smoothing, then impulse
    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[9600] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    int peak = FindPeak(out, 9700);
    REQUIRE(peak == Catch::Approx(9600 + 4800).margin(48)); // ±1 ms
}

TEST_CASE("feedback produces decaying repeats") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.5f;
    params.density = KnobForSeconds(0.05f);  // 2400 samples
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[4800] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    int p1 = FindPeak(out, 4900);            // first repeat
    float a1 = std::fabs(out[p1].l);
    int p2 = FindPeak(out, p1 + 1200);       // second repeat
    float a2 = std::fabs(out[p2].l);
    REQUIRE(p2 - p1 == Catch::Approx(2400).margin(48));
    REQUIRE(a2 < a1);
    REQUIRE(a2 > a1 * 0.2f);                 // roughly fb-proportional
}

TEST_CASE("multi-tap adds an earlier tap on the CW side") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f; params.feedback = 0.f;
    params.density = 1.f - (1.f - KnobForSeconds(0.1f));  // CW mirror: 0.5+0.5*d
    { float d = -std::log2(0.1f / 4.f) / 11.f; params.density = 0.5f + 0.5f * d; }
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[9600] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    // golden-ratio tap at 0.618*4800 ≈ 2967 before the main tap;
    // window-bounded searches so each tap is located within its own window
    int t2 = FindPeak(out, 9600 + 2400, 9600 + 3600);
    REQUIRE(t2 == Catch::Approx(9600 + 2967).margin(60));
    int t1 = FindPeak(out, 9600 + 4200, 9600 + 5400);
    REQUIRE(t1 == Catch::Approx(9600 + 4800).margin(60));
    // main (full-delay) tap is the louder one; tap2 is the additional tap
    REQUIRE(std::fabs(out[t1].l) > std::fabs(out[t2].l));
}

TEST_CASE("tape mode: delay-time jump glides (no instant jump)") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f; params.feedback = 0.f;
    params.density = KnobForSeconds(0.1f);
    params.slew_seconds = 0.2f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> settle(4800, StereoFrame{0.f,0.f});
    std::vector<StereoFrame> out(settle.size());
    proc.p.Process(settle.data(), out.data(), settle.size());
    float before = proc.p.DelayTimeSeconds();
    params.density = KnobForSeconds(0.2f);
    proc.p.SetParameters(params);
    proc.p.Process(settle.data(), out.data(), 2400); // 50 ms later
    float mid = proc.p.DelayTimeSeconds();
    REQUIRE(mid > before * 1.05f);
    REQUIRE(mid < 0.19f);          // still slewing, not arrived
}

TEST_CASE("high feedback decays instead of self-oscillating (fb HP must not peak)") {
    // Regression: the feedback DC-block HP is a 2nd-order SVF; left at the
    // class's default Q=1 its response peaks at ~1.155x above cutoff, so any
    // feedback >= ~0.87 made the loop gain exceed 1 and a ~30 Hz oscillation
    // grew out of nothing (found by the Task-13 Karplus demo render). With
    // Q=0.707 (Butterworth, as Particules sets) max HP gain is 1.0 and a
    // 0.95-feedback pluck must decay.
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.95f;
    params.density = 0.f;               // audio-rate base (2 ms clamp)
    proc.p.SetParameters(params);
    const size_t total = 96000;         // 2 s
    std::vector<StereoFrame> in(total, StereoFrame{0.f, 0.f});
    // 8 ms noise-ish burst (deterministic)
    for (int i = 0; i < 384; ++i) {
        float w = 0.5f - 0.5f * std::cos(2.f * 3.14159265f * i / 384.f);
        in[i] = {0.2f * w * ((i * 2654435761u % 1000) / 500.f - 1.f),
                 0.2f * w * ((i * 40503u % 1000) / 500.f - 1.f)};
    }
    std::vector<StereoFrame> out(total);
    for (size_t off = 0; off < total; off += 64) {
        proc.p.SetParameters(params);
        proc.p.Process(in.data() + off, out.data() + off, 64);
    }
    auto rms = [&](float t0, float t1) {
        size_t a = (size_t)(t0 * 48000.f), b = (size_t)(t1 * 48000.f);
        double ss = 0;
        for (size_t i = a; i < b; ++i) ss += (double)out[i].l * out[i].l;
        return std::sqrt(ss / (b - a));
    };
    float early = rms(0.2f, 0.4f);
    float late  = rms(1.6f, 1.9f);
    REQUIRE(early > 0.f);
    REQUIRE(late < early * 0.5f);       // decaying, not growing
}

TEST_CASE("NaN input does not poison the buffer") {
    Proc proc;
    EchosParameters params; params.dry_wet = 1.f; params.feedback = 0.9f;
    params.density = KnobForSeconds(0.01f);
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(9600, StereoFrame{0.f, 0.f});
    in[100] = {NAN, INFINITY};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    for (size_t i = 4800; i < out.size(); ++i)
        REQUIRE(std::isfinite(out[i].l));
}
