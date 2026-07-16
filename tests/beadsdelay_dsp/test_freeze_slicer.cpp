#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <random>
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

// density knob for a target base time (manual mode inverse mapping),
// matching test_echo_engine.cpp's helper.
float KnobForSeconds(float seconds, float buffer_seconds = 4.f) {
    float d = -std::log2(seconds / buffer_seconds) / 11.0f; // kManualOctaves
    return 0.5f - 0.5f * d;   // CCW side
}

float RmsRange(const std::vector<StereoFrame>& v, size_t from, size_t to) {
    double sum_sq = 0.0;
    size_t n = to - from;
    for (size_t i = from; i < to; ++i) sum_sq += static_cast<double>(v[i].l) * v[i].l;
    return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(n)));
}

float MeanAbsRange(const std::vector<StereoFrame>& v, size_t from, size_t to) {
    double sum = 0.0;
    size_t n = to - from;
    for (size_t i = from; i < to; ++i) sum += std::fabs(v[i].l);
    return static_cast<float>(sum / static_cast<double>(n));
}

float MeanRange(const std::vector<StereoFrame>& v, size_t from, size_t to) {
    double sum = 0.0;
    size_t n = to - from;
    for (size_t i = from; i < to; ++i) sum += v[i].l;
    return static_cast<float>(sum / static_cast<double>(n));
}

float StdDevRange(const std::vector<StereoFrame>& v, size_t from, size_t to) {
    float mean = MeanRange(v, from, to);
    double sum_sq = 0.0;
    size_t n = to - from;
    for (size_t i = from; i < to; ++i) {
        double d = v[i].l - mean;
        sum_sq += d * d;
    }
    return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(n)));
}

// Normalized autocorrelation of v[from,to) at the given lag.
float NormalizedAutocorrelation(const std::vector<StereoFrame>& v, size_t from, size_t to, int lag) {
    double num = 0.0, e1 = 0.0, e2 = 0.0;
    for (size_t i = from; i + static_cast<size_t>(lag) < to; ++i) {
        double a = v[i].l;
        double b = v[i + static_cast<size_t>(lag)].l;
        num += a * b;
        e1 += a * a;
        e2 += b * b;
    }
    double denom = std::sqrt(e1 * e2);
    if (denom <= 0.0) return 0.f;
    return static_cast<float>(num / denom);
}

void FillSine(std::vector<StereoFrame>& v, float freq_hz, float amplitude, float sr) {
    for (size_t i = 0; i < v.size(); ++i) {
        float s = amplitude * std::sin(2.f * static_cast<float>(M_PI) * freq_hz *
                                        static_cast<float>(i) / sr);
        v[i] = {s, s};
    }
}
} // namespace

// (a) Frozen audio persists: a recorded 10 Hz sine keeps looping through 4 s
// of silent input once frozen.
TEST_CASE("freeze: frozen audio persists through silence") {
    Proc proc;
    const float sr = 48000.f;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.25f);  // base ~= 250 ms
    params.time = 0.f;
    proc.p.SetParameters(params);

    // Record 2 s of a 10 Hz sine (amplitude 0.8).
    std::vector<StereoFrame> record(static_cast<size_t>(2.0 * sr));
    FillSine(record, 10.f, 0.8f, sr);
    std::vector<StereoFrame> discard(record.size());
    proc.p.Process(record.data(), discard.data(), record.size());

    // Freeze, then feed 4 s of silence.
    params.freeze = true;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> silence(static_cast<size_t>(4.0 * sr), StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out(silence.size());
    proc.p.Process(silence.data(), out.data(), silence.size());

    // RMS of the last 2 s must show the loop still producing audio.
    size_t from = out.size() - static_cast<size_t>(2.0 * sr);
    float rms = RmsRange(out, from, out.size());
    REQUIRE(rms > 0.05f);
}

// (b) Frozen slice loops with period == base time: autocorrelate 2 s of
// frozen output (recorded from noise, so it's not self-similar except at
// the loop period) at lag == base samples.
TEST_CASE("freeze: frozen slice loops with period == base delay time") {
    Proc proc;
    const float sr = 48000.f;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.25f);  // base ~= 250 ms
    params.time = 0.f;
    proc.p.SetParameters(params);

    // Record 2 s of white noise (deterministic seed).
    std::vector<StereoFrame> record(static_cast<size_t>(2.0 * sr));
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-0.9f, 0.9f);
    for (auto& f : record) { float s = dist(rng); f = {s, s}; }
    std::vector<StereoFrame> discard(record.size());
    proc.p.Process(record.data(), discard.data(), record.size());

    // Freeze and read back 2 s (silence input; wet=1 so output is the loop).
    params.freeze = true;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> silence(static_cast<size_t>(2.0 * sr), StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out(silence.size());
    proc.p.Process(silence.data(), out.data(), silence.size());

    // Actual resolved base time, in host samples (decimation is 1x here).
    int lag = static_cast<int>(std::lround(proc.p.BaseTimeSeconds() * sr));
    REQUIRE(lag > 100);  // sanity: a real, sizeable period

    // Skip the very start (shifter bypass-ramp settle) for a clean window.
    float corr = NormalizedAutocorrelation(out, 1000, out.size(), lag);
    REQUIRE(corr > 0.8f);
}

// (c) TIME selects a different slice while frozen: record a ramp (sample
// value proportional to buffer position), freeze at time=0, then retarget
// to time=1.0 live (still frozen) -- the two windows must read distinct
// regions of the ramp.
TEST_CASE("freeze: TIME selects a different slice while frozen") {
    Proc proc;
    const float sr = 48000.f;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(1.0f);  // base = 1 s = 48000 samples
    params.time = 0.f;
    proc.p.SetParameters(params);

    // Fill exactly one full buffer loop (kBufferFrames) with a ramp so the
    // write head wraps back to exactly 0 when recording finishes, and every
    // buffer frame p holds value p / kBufferFrames.
    std::vector<StereoFrame> ramp(kBufferFrames);
    for (size_t i = 0; i < ramp.size(); ++i) {
        float v = static_cast<float>(i) / static_cast<float>(ramp.size());
        ramp[i] = {v, v};
    }
    std::vector<StereoFrame> discard(ramp.size());
    proc.p.Process(ramp.data(), discard.data(), ramp.size());

    // Freeze at time=0 -> slice_index 0 (a slice near the ramp's high end).
    params.freeze = true;
    params.time = 0.f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> silence(static_cast<size_t>(0.5 * sr), StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out0(silence.size());
    proc.p.Process(silence.data(), out0.data(), silence.size());
    float mean0 = MeanAbsRange(out0, 0, out0.size());

    // Retarget TIME to 1.0 while still frozen -> last slice (ramp's low end).
    params.time = 1.f;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> out1(silence.size());
    proc.p.Process(silence.data(), out1.data(), silence.size());
    float mean1 = MeanAbsRange(out1, 0, out1.size());

    REQUIRE(std::fabs(mean0 - mean1) > 0.1f);
}

// (d) Unfreeze resumes writes: after a freeze period, live input should
// reappear in the wet output within a few hundred ms (well inside the
// [+0.5s, +1s] post-unfreeze window used here). The frozen material was an
// oscillating tone; the new material is a DC offset, so a std-dev collapse
// (tone -> flat) is a much more robust signature than comparing RMS values
// (which, for these two signals' particular amplitudes, can land close
// enough numerically to make a bare RMS-diff threshold fragile). This is a
// pragmatic adjustment from the brief's literal "RMS differs by >20%"
// wording; the underlying intent -- "new input's signature reappears" --
// still holds and is checked more robustly this way.
TEST_CASE("freeze: unfreeze resumes writes, new input reappears") {
    Proc proc;
    const float sr = 48000.f;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.1f);  // base ~= 100 ms
    params.time = 0.f;
    proc.p.SetParameters(params);

    // Record a 10 Hz sine for 1 s.
    std::vector<StereoFrame> record(static_cast<size_t>(1.0 * sr));
    FillSine(record, 10.f, 0.8f, sr);
    std::vector<StereoFrame> discard(record.size());
    proc.p.Process(record.data(), discard.data(), record.size());

    // Freeze, feed 0.5 s of silence so the loop settles.
    params.freeze = true;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> silence(static_cast<size_t>(0.5 * sr), StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> frozen_out(silence.size());
    proc.p.Process(silence.data(), frozen_out.data(), silence.size());
    size_t frozen_from = frozen_out.size() - static_cast<size_t>(0.25 * sr);
    float frozen_std = StdDevRange(frozen_out, frozen_from, frozen_out.size());
    REQUIRE(frozen_std > 0.05f);  // sanity: the frozen loop is really oscillating

    // Unfreeze, feed 1 s of a 0.5-amplitude DC offset.
    params.freeze = false;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> dc_in(static_cast<size_t>(1.0 * sr), StereoFrame{0.5f, 0.5f});
    std::vector<StereoFrame> post_out(dc_in.size());
    proc.p.Process(dc_in.data(), post_out.data(), dc_in.size());

    // Window [+0.5s, +1s] post-unfreeze.
    size_t from = static_cast<size_t>(0.5 * sr);
    size_t to = static_cast<size_t>(1.0 * sr);
    float post_std = StdDevRange(post_out, from, to);
    float post_mean = MeanRange(post_out, from, to);

    REQUIRE(post_std < frozen_std * 0.3f);   // tone's oscillation is gone
    REQUIRE(post_mean > 0.15f);              // and the new DC content is present
}

// (e) Fix round: unfreezing from a slice that wraps past the write head must
// slew, not snap. With a high slice index (TIME=1.0 -> slice_index ==
// slice_count-1), NotifyFreeze's rising edge anchors slice_start_ at
// anchor - slice_count*slice_len (mod buffer size); since slice_count*base
// is engineered to sit just under the buffer duration, slice_start_ lands
// just AHEAD of (i.e. numerically past) the anchor once wrapped, not behind
// it. slice_phase_ then grows the frozen read position further ahead still.
// So at the falling edge, `equiv_delay = read_subsample_ - frozen_read_pos`
// (echo_engine.cpp) is negative -- unwrapped, that negative delay_frames_
// then satisfies SetTargets()'s `delay_frames_ < 0.f` "first-ever target"
// sentinel on the very next block, snapping delay_frames_ straight to that
// block's target instead of continuing the slew from where the frozen
// playback actually left off. Fixed by wrapping equiv_delay into [0, size)
// before assigning it (both tape and crossfade branches), so it's always a
// valid non-negative "current delay" a normal slew can continue from.
TEST_CASE("freeze: unfreeze from a wrapped high-slice-index position slews, doesn't snap") {
    Proc proc;
    const float sr = 48000.f;
    EchosParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.5f);  // base ~= 0.5 s
    params.time = 1.f;                      // slice_index == slice_count-1 (high)
    proc.p.SetParameters(params);

    // Record 2 s of noise so the buffer/write head are in a realistic state
    // (the wrap behavior itself is a property of slice_count*base vs. the
    // buffer size, not of how much content has actually been recorded).
    size_t rec_n = static_cast<size_t>(2.0f * sr);
    std::vector<StereoFrame> record(rec_n);
    std::mt19937 rng(0xF12EE2E);
    std::uniform_real_distribution<float> dist(-0.9f, 0.9f);
    for (auto& f : record) { float s = dist(rng); f = {s, s}; }
    std::vector<StereoFrame> discard(rec_n);
    proc.p.Process(record.data(), discard.data(), rec_n);

    // Freeze at the high slice index, let it settle for a bit (less than one
    // slice period) so slice_phase_ has advanced but not wrapped.
    params.freeze = true;
    proc.p.SetParameters(params);
    size_t settle_n = static_cast<size_t>(0.2f * sr);
    std::vector<StereoFrame> in_settle(settle_n, StereoFrame{0.f, 0.f}),
        out_settle(settle_n);
    proc.p.Process(in_settle.data(), out_settle.data(), settle_n);

    // Unfreeze and retarget TIME back to 0 (multiplier 1x -> final target
    // ~= base, 0.5 s) so the eventual settled delay is clearly far from the
    // frozen-equivalent position this test is probing (which, once wrapped,
    // sits close to a full buffer length away -- see comment above).
    params.freeze = false;
    params.time = 0.f;
    proc.p.SetParameters(params);

    // The falling-edge block itself (the very first internal kMaxBlockSize
    // chunk after unfreeze).
    std::vector<StereoFrame> in1(kMaxBlockSize, StereoFrame{0.f, 0.f}), out1(kMaxBlockSize);
    proc.p.Process(in1.data(), out1.data(), kMaxBlockSize);
    float delay_s1 = proc.p.DelayTimeSeconds();

    // Two more blocks (3 total post-unfreeze) -- still deep in the slew,
    // nowhere near the final 0.5 s target given the default ~0.08 s slew
    // time constant (3 blocks == 192 samples == 4 ms, a small fraction of
    // one time constant).
    std::vector<StereoFrame> in2(2 * kMaxBlockSize, StereoFrame{0.f, 0.f}),
        out2(2 * kMaxBlockSize);
    proc.p.Process(in2.data(), out2.data(), in2.size());
    float delay_s3 = proc.p.DelayTimeSeconds();

    INFO("delay_s1=" << delay_s1 << " delay_s3=" << delay_s3);

    // Discriminator: a genuine slew from the wrapped frozen-equivalent delay
    // (which sits close to a full buffer length -- several seconds) toward a
    // 0.5 s target moves only a small fraction of that distance in 3 blocks
    // (192 samples), so delay_s3 must still be well above the target. The
    // broken code instead snaps delay_frames_ straight to the target on the
    // block immediately after the falling edge (via the mis-triggered
    // "first-ever target" sentinel), landing at ~0.5 s already by this point.
    REQUIRE(delay_s3 > 1.0f);

    // Sanity: it does eventually converge given enough time, proving this
    // is genuinely a slew (not just permanently stuck).
    std::vector<StereoFrame> in3(static_cast<size_t>(2.0f * sr), StereoFrame{0.f, 0.f}),
        out3(static_cast<size_t>(2.0f * sr));
    proc.p.Process(in3.data(), out3.data(), in3.size());
    float delay_converged = proc.p.DelayTimeSeconds();
    REQUIRE(delay_converged == Catch::Approx(0.5f).margin(0.05f));
}
