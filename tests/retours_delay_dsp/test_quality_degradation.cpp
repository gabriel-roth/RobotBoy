#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include "retours_delay_dsp/retours_dsp.h"
#include "util/dsp_utils.h"

using namespace retours_delay_dsp;

// Integration coverage for the per-quality feedback degradation character
// (2026-07 fix round): tape modes must darken repeats measurably and
// accumulate onto a soft saturation ceiling instead of the storage codec's
// hard +/-1 clamp, and Sunny's decay rate must match Bright's at the same
// feedback knob (the old limiter hid a ~0.9x level trim).
namespace {

constexpr float kSr = 48000.f;
constexpr float kDelayS = 0.25f;

struct Proc {
    void* mem = nullptr;
    RetoursProcessor p;
    explicit Proc(float sr = kSr) {
        auto req = RetoursProcessor::GetMemoryRequirements(sr);
        posix_memalign(&mem, req.alignment, req.total_bytes);
        p.Init(mem, req.total_bytes, sr);
    }
    ~Proc() { std::free(mem); }
};

// Manual-mode density knob for a target delay given the mode's effective
// buffer duration (Bright 4 s, Cold 8 s, Sunny 16 s, Scorched 32 s stereo).
float KnobForSeconds(float seconds, float buffer_seconds) {
    float d = -std::log2(seconds / buffer_seconds) / 11.0f;  // kManualOctaves
    return 0.5f - 0.5f * d;
}

double Goertzel(const std::vector<StereoFrame>& v, size_t from, size_t to,
                double freq_hz) {
    double omega = 2.0 * 3.14159265358979 * freq_hz / kSr;
    double sc = 0.0, ss = 0.0;
    for (size_t i = from; i < to; ++i) {
        double a = omega * static_cast<double>(i);
        sc += static_cast<double>(v[i].l) * std::cos(a);
        ss += static_cast<double>(v[i].l) * std::sin(a);
    }
    size_t n = to - from;
    return n ? 2.0 * std::sqrt(sc * sc + ss * ss) / static_cast<double>(n) : 0.0;
}

double Db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// Render a 100 ms, 220 Hz sawtooth burst (amplitude 0.6 = 3 Vpk) into a
// 250 ms wet-only delay at feedback knob 0.75, after a 2.5 s settle that
// covers the quality transition and delay slew. Repeat k then occupies the
// window starting k*250 ms after the burst.
struct BurstRender {
    std::vector<StereoFrame> out;
    size_t settle;
    size_t burst;
};

BurstRender RenderBurst(QualityMode mode, float buffer_seconds) {
    const size_t settle_n = static_cast<size_t>(2.5f * kSr);
    const size_t burst_n = static_cast<size_t>(0.10f * kSr);
    const int n_windows = 13;
    const size_t total_n =
        settle_n + burst_n +
        static_cast<size_t>((n_windows + 1) * kDelayS * kSr);

    Proc proc;
    RetoursParameters p;
    p.dry_wet = 1.f;
    p.feedback = 0.75f;
    p.time = 0.f;
    p.density = KnobForSeconds(kDelayS, buffer_seconds);
    p.quality = mode;
    proc.p.SetParameters(p);

    std::vector<StereoFrame> in(total_n, StereoFrame{0.f, 0.f});
    const size_t fade = static_cast<size_t>(0.005f * kSr);
    for (size_t i = 0; i < burst_n; ++i) {
        double ph = std::fmod(220.0 * static_cast<double>(i) / kSr, 1.0);
        float s = 0.6f * static_cast<float>(2.0 * ph - 1.0);
        float g = 1.f;
        if (i < fade) g = static_cast<float>(i) / static_cast<float>(fade);
        if (burst_n - i < fade)
            g = static_cast<float>(burst_n - i) / static_cast<float>(fade);
        in[settle_n + i] = {s * g, s * g};
    }
    BurstRender r;
    r.out.resize(total_n);
    r.settle = settle_n;
    r.burst = burst_n;
    proc.p.Process(in.data(), r.out.data(), total_n);
    return r;
}

// Goertzel magnitude of freq inside repeat window k (1-based).
double RepeatMag(const BurstRender& r, int k, double freq) {
    size_t w0 = r.settle +
                static_cast<size_t>(k * kDelayS * kSr) +
                static_cast<size_t>(0.005f * kSr);
    size_t w1 = w0 + r.burst - static_cast<size_t>(0.01f * kSr);
    return Goertzel(r.out, w0, w1, freq);
}

// Continuous 220 Hz saw (amplitude 0.5) for 10 s, wet-only, feedback 0.75;
// stats over the last 2 s where the loop has settled.
struct SteadyStats {
    double clip_frac;
    double peak;
};

SteadyStats RenderSteady(QualityMode mode, float buffer_seconds) {
    const size_t settle_n = static_cast<size_t>(2.5f * kSr);
    const size_t play_n = static_cast<size_t>(10.f * kSr);
    const size_t total_n = settle_n + play_n;

    Proc proc;
    RetoursParameters p;
    p.dry_wet = 1.f;
    p.feedback = 0.75f;
    p.time = 0.f;
    p.density = KnobForSeconds(kDelayS, buffer_seconds);
    p.quality = mode;
    proc.p.SetParameters(p);

    std::vector<StereoFrame> in(total_n, StereoFrame{0.f, 0.f});
    for (size_t i = settle_n; i < total_n; ++i) {
        double t = static_cast<double>(i - settle_n) / kSr;
        double ph = std::fmod(220.0 * t, 1.0);
        float s = 0.5f * static_cast<float>(2.0 * ph - 1.0);
        in[i] = {s, s};
    }
    std::vector<StereoFrame> out(total_n);
    proc.p.Process(in.data(), out.data(), total_n);

    size_t w0 = total_n - static_cast<size_t>(2.f * kSr);
    SteadyStats st{0.0, 0.0};
    size_t clip = 0;
    for (size_t i = w0; i < total_n; ++i) {
        double a = std::fabs(static_cast<double>(out[i].l));
        st.peak = std::max(st.peak, a);
        if (a > 0.985) ++clip;
    }
    st.clip_frac = static_cast<double>(clip) / static_cast<double>(total_n - w0);
    return st;
}

} // namespace

// -----------------------------------------------------------------------
// (A1) Scorched repeats darken fast in the presence band. Differential =
// extra HF (3.3 kHz) loss between repeats 1 and 4 beyond the level decay
// (measured at 440 Hz). With the 2.8 kHz tone LP this is ~-14 dB over the
// three passes; the old 5 kHz voicing gave only ~-2.3 dB.
// -----------------------------------------------------------------------
TEST_CASE("degradation: Scorched repeats darken fast in the presence band") {
    auto r = RenderBurst(QualityMode::kScorchedCassette, 32.f);
    double hf_drop = Db(RepeatMag(r, 1, 3300.0)) - Db(RepeatMag(r, 4, 3300.0));
    double lf_drop = Db(RepeatMag(r, 1, 440.0)) - Db(RepeatMag(r, 4, 440.0));
    REQUIRE(hf_drop - lf_drop > 8.0);
}

// -----------------------------------------------------------------------
// (A2) Sunny's very first repeat is measurably mellower than Bright's:
// spectral tilt (7.92 kHz vs 440 Hz) gap of ~8 dB with the 6.5 kHz
// voicing vs ~4 dB with the old 10 kHz voicing (decimation accounts for
// the baseline gap).
// -----------------------------------------------------------------------
TEST_CASE("degradation: Sunny first repeat is mellower than Bright's") {
    auto sunny = RenderBurst(QualityMode::kSunnyTape, 16.f);
    auto bright = RenderBurst(QualityMode::kBrightDigital, 4.f);
    double tilt_sunny =
        Db(RepeatMag(sunny, 1, 7920.0)) - Db(RepeatMag(sunny, 1, 440.0));
    double tilt_bright =
        Db(RepeatMag(bright, 1, 7920.0)) - Db(RepeatMag(bright, 1, 440.0));
    REQUIRE(tilt_bright - tilt_sunny > 6.0);
}

// -----------------------------------------------------------------------
// (B) Tape modes accumulate onto the soft write-saturation ceiling
// (0.65/0.71 Sunny, 0.45 Scorched), never the codec's hard +/-1 clamp.
// Pre-fix these measured 12.4% (Sunny) and 5.5% (Scorched) of steady-state
// samples pinned above 0.985. Bright must still brickwall by design.
// -----------------------------------------------------------------------
TEST_CASE("degradation: tape modes settle on a soft ceiling, not the codec clamp") {
    auto sunny = RenderSteady(QualityMode::kSunnyTape, 16.f);
    REQUIRE(sunny.clip_frac < 0.005);
    REQUIRE(sunny.peak < 0.9);

    auto scorched = RenderSteady(QualityMode::kScorchedCassette, 32.f);
    REQUIRE(scorched.clip_frac < 0.005);
    REQUIRE(scorched.peak < 0.9);

    auto bright = RenderSteady(QualityMode::kBrightDigital, 4.f);
    REQUIRE(bright.clip_frac > 0.3);
}

// -----------------------------------------------------------------------
// (C) Sunny's decay rate matches Bright's at the same feedback knob.
// Measured over repeats 8..12 (below the saturation knee). Pre-fix the
// hidden 0.9x limiter trim made Sunny ~0.9 dB/repeat faster. This may
// already pass once Task 1's shared curve fix lands — it pins that fix at
// the integration level.
// -----------------------------------------------------------------------
TEST_CASE("degradation: Sunny decay rate matches Bright at the same knob") {
    auto sunny = RenderBurst(QualityMode::kSunnyTape, 16.f);
    auto bright = RenderBurst(QualityMode::kBrightDigital, 4.f);
    auto slope = [](const BurstRender& r) {
        return (Db(RepeatMag(r, 8, 440.0)) - Db(RepeatMag(r, 12, 440.0))) / 4.0;
    };
    REQUIRE(std::fabs(slope(sunny) - slope(bright)) < 0.5);
}

// -----------------------------------------------------------------------
// (D) Cold digital is a Clouds emulation: accumulated feedback lands on
// the cubic write-limiter ceiling (1/1.4 ~= 0.71), never the int12 codec
// clamp. Pre-fix Cold measured 37% of steady-state samples pinned above
// 0.985 (identical overload character to Bright); post-fix it must be
// clamp-free while Bright still brickwalls (asserted in test B above).
// -----------------------------------------------------------------------
TEST_CASE("degradation: Cold settles on the Clouds soft-limit, not the codec clamp") {
    auto cold = RenderSteady(QualityMode::kColdDigital, 8.f);
    REQUIRE(cold.clip_frac < 0.005);
    REQUIRE(cold.peak < 0.9);
}
