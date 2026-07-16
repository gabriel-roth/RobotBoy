#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "mod/ar_modulator.h"
#include "mod/slow_random_lfo.h"
#include "random/random.h"
#include "retours_delay_dsp/retours_dsp.h"

using namespace retours_delay_dsp;

namespace {
struct Proc {
    void* mem = nullptr;
    RetoursProcessor p;
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

// Run the processor in chunks, sampling DelayTimeSeconds() after each chunk.
std::vector<float> CollectDelayTimes(Proc& proc, const RetoursParameters& params,
                                      size_t total_frames, size_t chunk_frames) {
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(chunk_frames, StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out(chunk_frames);
    std::vector<float> samples;
    size_t done = 0;
    while (done < total_frames) {
        size_t n = std::min(chunk_frames, total_frames - done);
        proc.p.Process(in.data(), out.data(), n);
        samples.push_back(proc.p.DelayTimeSeconds());
        done += n;
    }
    return samples;
}

// crude pitch estimate: count zero crossings of the processor's wet output
// after a continuous sine has settled through the delay + shifter.
float MeasureProcessorRatio(Proc& proc, const RetoursParameters& params, float in_freq,
                             float sample_rate, size_t total_frames, size_t skip_frames) {
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(total_frames), out(total_frames);
    for (size_t i = 0; i < total_frames; ++i) {
        float x = std::sin(2.f * 3.14159265f * in_freq / sample_rate * (float)i);
        in[i] = {x, x};
    }
    proc.p.Process(in.data(), out.data(), total_frames);
    int crossings = 0;
    float prev = 0.f;
    for (size_t i = skip_frames; i < total_frames; ++i) {
        if (prev <= 0.f && out[i].l > 0.f) crossings++;
        prev = out[i].l;
    }
    float measured = crossings / ((float)(total_frames - skip_frames) / sample_rate);
    return measured / in_freq;
}
} // namespace

TEST_CASE("processor: all ARs at noon, no CV -> delay time is stable") {
    Proc proc;
    RetoursParameters params;
    params.density = KnobForSeconds(0.1f);
    params.time = 0.5f;
    // time_ar, pitch_ar, shape_ar all default to 0 (noon); no CV connected.

    // Let the target settle before sampling.
    auto settle = CollectDelayTimes(proc, params, 48000 * 2, 4800);
    (void)settle;

    auto samples = CollectDelayTimes(proc, params, 48000 * 2, 4800);
    float lo = samples.front(), hi = samples.front();
    for (float v : samples) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    REQUIRE((hi - lo) < 1e-6f);
}

TEST_CASE("processor: time_ar fully CCW, no CV -> delay time wanders") {
    Proc proc;
    RetoursParameters params;
    params.density = KnobForSeconds(0.1f);
    params.time = 0.5f;
    params.time_ar = -1.f;

    // Let the target settle first, then sample over 20 s of processing.
    CollectDelayTimes(proc, params, 48000 * 1, 4800);
    auto samples = CollectDelayTimes(proc, params, 48000 * 20, 4800);
    float lo = samples.front(), hi = samples.front();
    for (float v : samples) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    REQUIRE((hi - lo) > 0.001f);
}

TEST_CASE("processor: pitch_ar=1 + pitch_cv=1.0 connected -> ~octave up (1V/oct)") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;       // wet only
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.05f);  // ~50 ms base delay
    params.time = 0.f;                       // 1x multiplier
    params.pitch_semitones = 0.f;
    params.pitch_ar = 1.f;
    params.pitch_cv = 1.0f;
    params.pitch_cv_connected = true;

    float sr = 48000.f;
    float ratio = MeasureProcessorRatio(proc, params, 440.f, sr,
                                         (size_t)(2 * sr), (size_t)(1 * sr));
    REQUIRE(ratio == Catch::Approx(2.f).epsilon(0.08));
}

TEST_CASE("ArModulator::Process: cv-connected positive AR passes CV through exactly") {
    ArModulator ar;
    particules_dsp::Random rng;
    rng.Init();
    ar.lfo.Init(&rng, 1);
    ar.lfo.SetRate(0.15f, 48000.f);
    REQUIRE(ar.Process(0.5f, 1.0f, true, 64) == 0.5f);
}

TEST_CASE("ArModulator::Process: uniform-negative unconnected branch stays bounded and varies") {
    ArModulator ar;
    particules_dsp::Random rng;
    rng.Init();
    ar.lfo.Init(&rng, 1);
    ar.lfo.SetRate(0.15f, 48000.f);

    float lo = 1.f, hi = -1.f;
    bool changed = false;
    float prev = ar.Process(-1.f, 0.f, false, 64);
    lo = std::min(lo, prev);
    hi = std::max(hi, prev);
    for (int i = 0; i < 10000; ++i) {
        float v = ar.Process(-1.f, 0.f, false, 64);
        REQUIRE(v >= -1.f);
        REQUIRE(v <= 1.f);
        if (v != prev) changed = true;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        prev = v;
    }
    REQUIRE(changed);
}

TEST_CASE("SlowRandomLfo: values stay within [-1, 1]") {
    particules_dsp::Random rng;
    rng.Init();
    SlowRandomLfo lfo;
    lfo.Init(&rng, 1);
    lfo.SetRate(0.15f, 48000.f);
    for (int i = 0; i < 5000; ++i) {
        float v = lfo.Next(64);
        REQUIRE(v >= -1.f);
        REQUIRE(v <= 1.f);
    }
}

TEST_CASE("SlowRandomLfo: different salts produce different sequences") {
    particules_dsp::Random rng;
    rng.Init();
    SlowRandomLfo a, b;
    a.Init(&rng, 1);
    b.Init(&rng, 2);
    a.SetRate(0.15f, 48000.f);
    b.SetRate(0.15f, 48000.f);

    bool differ = false;
    for (int i = 0; i < 2000; ++i) {
        float va = a.Next(64);
        float vb = b.Next(64);
        if (va != vb) differ = true;
    }
    REQUIRE(differ);
}

TEST_CASE("SlowRandomLfo: higher rate wanders faster than lower rate") {
    particules_dsp::Random rng_fast, rng_slow;
    rng_fast.Init();
    rng_slow.Init();
    SlowRandomLfo fast, slow;
    fast.Init(&rng_fast, 1);
    slow.Init(&rng_slow, 1);
    fast.SetRate(2.0f, 48000.f);
    slow.SetRate(0.02f, 48000.f);

    const int kBlocks = 200;   // 200 * 64 samples ~= 0.27 s
    float fast_lo = 1.f, fast_hi = -1.f, slow_lo = 1.f, slow_hi = -1.f;
    for (int i = 0; i < kBlocks; ++i) {
        float vf = fast.Next(64);
        float vs = slow.Next(64);
        fast_lo = std::min(fast_lo, vf);
        fast_hi = std::max(fast_hi, vf);
        slow_lo = std::min(slow_lo, vs);
        slow_hi = std::max(slow_hi, vs);
    }
    REQUIRE((fast_hi - fast_lo) > (slow_hi - slow_lo));
}
