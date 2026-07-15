#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "pitch/rotary_shifter.h"
#include "beadsdelay_dsp/echos_dsp.h"
using namespace beadsdelay_dsp;

// crude pitch estimate: count zero crossings of a sine after shifting
static float MeasureRatio(float semitones) {
    RotaryShifter s;
    s.Init(48000.f);
    s.SetRatio(std::exp2(semitones / 12.f));
    float freq = 440.f / 48000.f;
    int crossings = 0; float prev = 0.f;
    const int N = 48000;
    for (int i = 0; i < N; ++i) {
        float x = std::sin(2.f * 3.14159265f * freq * i);
        StereoFrame y = s.Process({x, x});
        if (i > 4096) {  // skip warmup
            if (prev <= 0.f && y.l > 0.f) crossings++;
            prev = y.l;
        }
    }
    float measured = crossings / ((N - 4096) / 48000.f);
    return measured / 440.f;
}

TEST_CASE("octave up shifts frequency by ~2x") {
    REQUIRE(MeasureRatio(12.f) == Catch::Approx(2.f).epsilon(0.06));
}
TEST_CASE("octave down shifts frequency by ~0.5x") {
    REQUIRE(MeasureRatio(-12.f) == Catch::Approx(0.5f).epsilon(0.06));
}
TEST_CASE("ratio 1 is exact passthrough after ramp") {
    RotaryShifter s; s.Init(48000.f); s.SetRatio(1.f);
    for (int i = 0; i < 512; ++i) s.Process({0.f, 0.f});
    StereoFrame y = s.Process({0.7f, -0.3f});
    REQUIRE(y.l == 0.7f);
    REQUIRE(y.r == -0.3f);
}

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
} // namespace

TEST_CASE("pitch-shifted wet path still produces output near the delay time") {
    Proc proc;
    EchosParameters params;
    params.dry_wet = 1.f;              // wet only
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.1f);  // 100 ms -> 4800 samples
    params.time = 0.f;                 // 1x multiplier
    params.pitch_semitones = 12.f;     // octave up, well outside bypass window
    proc.p.SetParameters(params);

    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[9600] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());

    int expected = 9600 + 4800;
    int lo = std::max(0, expected - 4096);
    int hi = std::min((int)out.size(), expected + 4096);
    double sum_sq = 0.0;
    int count = 0;
    for (int i = lo; i < hi; ++i) {
        sum_sq += (double)out[i].l * out[i].l;
        ++count;
    }
    float rms = std::sqrt(sum_sq / std::max(1, count));
    REQUIRE(rms > 0.01f);
}
