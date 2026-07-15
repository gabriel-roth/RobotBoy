#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include "beadsdelay_dsp/echos_dsp.h"
#include "random/random.h"
#include "util/dsp_utils.h"

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

// Density knob for a target base time (manual mode inverse mapping),
// duplicated per-file per this test lane's convention (see
// test_echo_engine.cpp / test_repeat_envelope.cpp).
float KnobForSeconds(float seconds, float buffer_seconds = 4.f) {
    float d = -std::log2(seconds / buffer_seconds) / 11.0f;  // kManualOctaves
    return 0.5f - 0.5f * d;
}

int FindPeak(const std::vector<StereoFrame>& v, int from, int to = -1) {
    if (to < 0 || to > (int)v.size()) to = (int)v.size();
    int best = from;
    float mag = 0.f;
    for (int i = from; i < to; ++i)
        if (std::fabs(v[i].l) > mag) { mag = std::fabs(v[i].l); best = i; }
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------
// (a) Block-size invariance: identical input + params, processed through
// fresh processors in chunks of 1, 7, 64, 512 frames must produce
// sample-identical output within 1e-4 absolute.
// ---------------------------------------------------------------------------
TEST_CASE("block-size invariance: 1/7/64/512-frame chunking gives identical output") {
    const float sr = 48000.f;
    const size_t total = static_cast<size_t>(2.0f * sr);

    // Deterministic mixed sine + noise input.
    particules_dsp::Random rng;
    rng.Init(0xB16B00B5);
    std::vector<StereoFrame> in(total);
    for (size_t i = 0; i < total; ++i) {
        float sine = 0.3f * std::sin(2.0f * particules_dsp::kPi * 220.f *
                                      static_cast<float>(i) / sr);
        float noise = 0.15f * rng.NextBipolar();
        in[i] = {sine + noise, sine - noise};
    }

    EchosParameters params;
    params.density = KnobForSeconds(0.08f);  // ~80 ms
    params.feedback = 0.4f;
    params.dry_wet = 0.7f;
    params.shape = 0.2f;

    auto run_chunked = [&](size_t chunk) {
        Proc proc(sr);
        proc.p.SetParameters(params);
        std::vector<StereoFrame> out(total);
        size_t offset = 0;
        while (offset < total) {
            size_t n = std::min(chunk, total - offset);
            proc.p.Process(in.data() + offset, out.data() + offset, n);
            offset += n;
        }
        return out;
    };

    auto out1 = run_chunked(1);
    auto out7 = run_chunked(7);
    auto out64 = run_chunked(64);
    auto out512 = run_chunked(512);

    for (size_t i = 0; i < total; ++i) {
        INFO("sample " << i);
        REQUIRE(out1[i].l == Catch::Approx(out64[i].l).margin(1e-4));
        REQUIRE(out1[i].r == Catch::Approx(out64[i].r).margin(1e-4));
        REQUIRE(out7[i].l == Catch::Approx(out64[i].l).margin(1e-4));
        REQUIRE(out7[i].r == Catch::Approx(out64[i].r).margin(1e-4));
        REQUIRE(out512[i].l == Catch::Approx(out64[i].l).margin(1e-4));
        REQUIRE(out512[i].r == Catch::Approx(out64[i].r).margin(1e-4));
    }
}

// ---------------------------------------------------------------------------
// (b) Corner stress: extreme parameter corners, with freeze toggling every
// SetParameters call, pitch alternating +/-24, quality cycling every 0.5 s,
// over 5 s of noise input. No non-finite samples, no crash.
// ---------------------------------------------------------------------------
TEST_CASE("corner stress: extreme params with freeze/quality churn stay finite") {
    const float sr = 48000.f;
    const size_t total = static_cast<size_t>(5.0f * sr);
    const size_t chunk = 64;
    const size_t steps = total / chunk;
    const size_t quality_period_steps =
        std::max<size_t>(1, static_cast<size_t>(0.5f * sr) / chunk);

    QualityMode qualities[4] = {QualityMode::kHiFi, QualityMode::kClouds,
                                 QualityMode::kCleanLoFi, QualityMode::kTape};
    float densities[2] = {0.f, 1.f};
    float times[2] = {0.f, 1.f};

    for (float density : densities) {
        for (float time : times) {
            Proc proc(sr);
            particules_dsp::Random rng;
            rng.Init(0x5EED0000u ^ static_cast<uint32_t>(density * 1000) ^
                     static_cast<uint32_t>(time * 37));

            EchosParameters params;
            params.density = density;
            params.time = time;
            params.shape = 1.f;
            params.feedback = 1.f;

            bool freeze = false;
            bool pitch_high = true;
            bool all_finite = true;
            size_t first_bad_step = SIZE_MAX;

            std::vector<StereoFrame> in(chunk), out(chunk);
            for (size_t step = 0; step < steps; ++step) {
                params.pitch_semitones = pitch_high ? 24.f : -24.f;
                pitch_high = !pitch_high;
                params.freeze = freeze;
                freeze = !freeze;
                params.quality = qualities[(step / quality_period_steps) % 4];
                proc.p.SetParameters(params);

                for (size_t i = 0; i < chunk; ++i) {
                    float n = rng.NextBipolar();
                    in[i] = {n, n};
                }
                proc.p.Process(in.data(), out.data(), chunk);

                for (auto& f : out) {
                    if (!std::isfinite(f.l) || !std::isfinite(f.r)) {
                        all_finite = false;
                        if (first_bad_step == SIZE_MAX) first_bad_step = step;
                    }
                }
            }

            INFO("density=" << density << " time=" << time
                             << " first_bad_step=" << first_bad_step);
            REQUIRE(all_finite);
        }
    }
}

// ---------------------------------------------------------------------------
// (c) ClearBuffer() mid-feedback: a loud feedback tail must die within the
// buffer-clear, not linger through the following silence.
// ---------------------------------------------------------------------------
TEST_CASE("ClearBuffer mid-feedback kills the tail") {
    const float sr = 48000.f;
    Proc proc(sr);
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.95f;
    params.density = KnobForSeconds(0.01f);  // short delay: many repeats build up fast
    proc.p.SetParameters(params);

    // Build up a loud feedback tail: ~50 ms of loud noise-ish input.
    size_t burst_n = static_cast<size_t>(0.05f * sr);
    std::vector<StereoFrame> burst_in(burst_n), burst_out(burst_n);
    particules_dsp::Random rng;
    rng.Init(0xFEEDBACC);
    for (size_t i = 0; i < burst_n; ++i) {
        float v = 0.8f * rng.NextBipolar();
        burst_in[i] = {v, v};
    }
    proc.p.Process(burst_in.data(), burst_out.data(), burst_n);

    proc.p.ClearBuffer();

    // 200 ms of silence.
    size_t silence_n = static_cast<size_t>(0.2f * sr);
    std::vector<StereoFrame> silence_in(silence_n, StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> silence_out(silence_n);
    proc.p.Process(silence_in.data(), silence_out.data(), silence_n);

    // RMS over the last 50 ms.
    size_t tail_n = static_cast<size_t>(0.05f * sr);
    double sum_sq = 0.0;
    for (size_t i = silence_n - tail_n; i < silence_n; ++i) {
        sum_sq += static_cast<double>(silence_out[i].l) * silence_out[i].l;
        sum_sq += static_cast<double>(silence_out[i].r) * silence_out[i].r;
    }
    double rms = std::sqrt(sum_sq / static_cast<double>(2 * tail_n));
    REQUIRE(rms < 0.01);
}

// ---------------------------------------------------------------------------
// (d) Telemetry: BaseTimeSeconds()/DelayTimeSeconds() vs impulse-measured
// delay; IsClocked() false -> true after two ticks -> false after timeout.
// ---------------------------------------------------------------------------
TEST_CASE("telemetry: BaseTimeSeconds/DelayTimeSeconds match impulse-measured delay") {
    const float sr = 48000.f;
    Proc proc(sr);
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.15f);  // ~150 ms, manual mode, no AR
    params.time = 0.f;                       // 1x multiplier
    proc.p.SetParameters(params);

    // Settle the delay-time slew (default tau 0.08 s) before measuring.
    std::vector<StereoFrame> settle(static_cast<size_t>(0.5f * sr), StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> settle_out(settle.size());
    proc.p.Process(settle.data(), settle_out.data(), settle.size());

    float base_seconds = proc.p.BaseTimeSeconds();
    float delay_seconds = proc.p.DelayTimeSeconds();

    std::vector<StereoFrame> in(static_cast<size_t>(1.0f * sr), StereoFrame{0.f, 0.f});
    const int impulse_at = 4800;
    in[impulse_at] = {1.f, 1.f};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    int peak = FindPeak(out, impulse_at + 100);
    float measured_seconds = static_cast<float>(peak - impulse_at) / sr;

    REQUIRE(base_seconds == Catch::Approx(measured_seconds).epsilon(0.05));
    REQUIRE(delay_seconds == Catch::Approx(measured_seconds).epsilon(0.05));
}

TEST_CASE("telemetry: IsClocked false->true after two ticks->false after timeout") {
    const float sr = 48000.f;
    Proc proc(sr);
    EchosParameters params;
    params.clock_connected = true;
    proc.p.SetParameters(params);
    REQUIRE_FALSE(proc.p.IsClocked());

    std::vector<StereoFrame> blk(64, StereoFrame{0.f, 0.f}), blk_out(64);

    // First tick: not enough history to be "clocked" yet.
    params.clock_tick_offset = 0;
    proc.p.SetParameters(params);
    proc.p.Process(blk.data(), blk_out.data(), blk.size());
    REQUIRE_FALSE(proc.p.IsClocked());

    // 0.5 s of quiet (minus the first tick's own block) before the 2nd tick.
    params.clock_tick_offset = -1;
    proc.p.SetParameters(params);
    size_t gap = static_cast<size_t>(0.5f * sr) - blk.size();
    std::vector<StereoFrame> gap_in(gap, StereoFrame{0.f, 0.f}), gap_out(gap);
    proc.p.Process(gap_in.data(), gap_out.data(), gap);

    // Second tick, 0.5 s after the first: now clocked.
    params.clock_tick_offset = 0;
    proc.p.SetParameters(params);
    proc.p.Process(blk.data(), blk_out.data(), blk.size());
    REQUIRE(proc.p.IsClocked());

    // Clock jack pulled, >5 s of silence: falls out of clocked state.
    params.clock_tick_offset = -1;
    params.clock_connected = false;
    proc.p.SetParameters(params);
    size_t long_quiet = static_cast<size_t>(5.5f * sr);
    std::vector<StereoFrame> lq_in(long_quiet, StereoFrame{0.f, 0.f}), lq_out(long_quiet);
    proc.p.Process(lq_in.data(), lq_out.data(), long_quiet);
    REQUIRE_FALSE(proc.p.IsClocked());
}
