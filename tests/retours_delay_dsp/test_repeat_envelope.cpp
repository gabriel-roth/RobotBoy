#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "env/repeat_envelope.h"
#include "retours_delay_dsp/retours_dsp.h"

using namespace retours_delay_dsp;

namespace {
struct Proc {
    void* mem = nullptr; RetoursProcessor p;
    explicit Proc(float sr = 48000.f) {
        auto req = RetoursProcessor::GetMemoryRequirements(sr);
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
std::vector<float> CollectGains(float shape, float period, float sample_rate = 48000.f) {
    RepeatEnvelope env;
    env.Init(sample_rate);
    env.SetPeriodSamples(period);
    env.SetShape(shape);
    std::vector<float> gains(static_cast<size_t>(period));
    for (auto& g : gains) g = env.Next();
    return gains;
}
} // namespace

// (a) shape=0 -> pure passthrough gain, exactly 1.0 for every sample.
TEST_CASE("RepeatEnvelope: shape=0 returns unity gain every sample") {
    auto gains = CollectGains(0.f, 4800.f);
    for (float g : gains) REQUIRE(g == 1.f);
}

// (b) shape=0.25 -> a gate that's wide open early in the period and closed
// (much lower gain) late in the period.
TEST_CASE("RepeatEnvelope: shape=0.25 gates open early, closed late") {
    auto gains = CollectGains(0.25f, 4800.f);

    float max_first_half = 0.f;
    for (size_t i = 0; i < gains.size() / 2; ++i)
        max_first_half = std::max(max_first_half, gains[i]);
    REQUIRE(max_first_half > 0.95f);

    // last 20% of the period: gate must be closed (much lower than the peak).
    size_t last20_start = gains.size() - gains.size() / 5;
    float max_last20 = 0.f;
    for (size_t i = last20_start; i < gains.size(); ++i)
        max_last20 = std::max(max_last20, gains[i]);
    // The segment-1 morph crossfades a flat 1.0 with the gate, so the floor
    // at shape=0.25 is (1 - t) = 0.25, not literally zero -- but it must be
    // far below the open-gate peak.
    REQUIRE(max_last20 < 0.35f);
    REQUIRE(max_last20 < max_first_half * 0.4f);

    // no negative gains, nothing above unity
    for (float g : gains) {
        REQUIRE(g >= 0.f);
        REQUIRE(g <= 1.0001f);
    }
}

// (c) shape=2/3 -> pure Hann window, symmetric around phase 0.5.
TEST_CASE("RepeatEnvelope: shape=2/3 is a symmetric Hann window") {
    auto gains = CollectGains(2.f / 3.f, 4800.f);
    size_t n = gains.size();

    for (size_t i = 0; i < n; i += 97)   // sparse sampling, cheap but thorough
        REQUIRE(gains[i] == Catch::Approx(gains[n - 1 - i]).margin(0.02));

    // peak near phase 0.5 (index n/2 - 1, since phase = (i+1)/n)
    REQUIRE(gains[n / 2 - 1] > 0.95f);

    // endpoints (phase near 0) are near zero
    REQUIRE(gains[0] < 0.05f);
    REQUIRE(gains[n - 1] < 0.05f);
}

// (d1) Processor-level: shape=0 must leave the Task-3 impulse-delay
// behavior unchanged (same peak position, amplitude within 1%).
TEST_CASE("RepeatEnvelope: shape=0 leaves impulse-delay behavior unchanged") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.shape = 0.f;
    params.density = KnobForSeconds(0.1f);   // 100 ms -> 4800 samples
    params.time = 0.f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(48000, StereoFrame{0.f, 0.f});
    in[9600] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    int peak = FindPeak(out, 9700);
    REQUIRE(peak == Catch::Approx(9600 + 4800).margin(48));
    REQUIRE(std::fabs(out[peak].l) == Catch::Approx(1.f).margin(0.01f));
}

// (d2) Processor-level: shape=0.25 with a ~100 ms period gates a constant
// wet-only signal, closing well before the period ends.
TEST_CASE("RepeatEnvelope: shape=0.25 gates a constant wet signal per period") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;      // wet only
    params.feedback = 0.f;
    params.shape = 0.25f;
    params.density = KnobForSeconds(0.1f);   // 100 ms -> 4800 samples
    params.time = 0.f;
    proc.p.SetParameters(params);

    std::vector<StereoFrame> in(48000, StereoFrame{0.5f, 0.5f});
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());

    float period_seconds = proc.p.BaseTimeSeconds();
    REQUIRE(period_seconds == Catch::Approx(0.1f).margin(0.005f));
    int period_samples = static_cast<int>(period_seconds * 48000.f);
    int delay_samples = static_cast<int>(proc.p.DelayTimeSeconds() * 48000.f);

    // Settle: skip a few periods after the wet signal first arrives so the
    // measurement window sits on steady-state (constant-input) output.
    int start = delay_samples + period_samples * 2;
    int first_half_end = start + period_samples / 2;
    int last15_start = start + period_samples - period_samples * 15 / 100;
    int last15_end = start + period_samples;
    REQUIRE(last15_end <= (int)out.size());

    auto Rms = [&](int from, int to) {
        double sum_sq = 0.0;
        for (int i = from; i < to; ++i) sum_sq += double(out[i].l) * out[i].l;
        return std::sqrt(sum_sq / std::max(1, to - from));
    };
    float rms_first_half = Rms(start, first_half_end);
    float rms_last15 = Rms(last15_start, last15_end);

    REQUIRE(rms_first_half > 0.3f);       // gate is open, signal present
    REQUIRE(rms_last15 < rms_first_half * 0.4f);  // gate closed late in period
}

// The pre-vs-post-envelope feedback tap was removed: the feedback tap is now
// permanently post-envelope (matching hardware Beads), so there is no longer a
// pre-feedback variant to test here.
