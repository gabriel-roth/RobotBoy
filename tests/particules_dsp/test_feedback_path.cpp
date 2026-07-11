#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cmath>
#include <cstdint>

#include "particules_dsp/particules_dsp.h"

using namespace particules_dsp;

static constexpr float kSR = 48000.0f;

namespace {
struct Proc {
    std::vector<uint8_t> memory;
    ParticulesProcessor p;
    Proc() {
        auto req = ParticulesProcessor::GetMemoryRequirements(kSR);
        memory.resize(req.total_bytes, 0);
        p.Init(memory.data(), memory.size(), kSR);
    }
};

// Skip the read-ahead delay (grain engine's min_offset for the default
// 30ms/unity-pitch grain, ~2880 frames) so the measurement window excludes
// the brief echo of the tone's own tail and reflects only feedback-driven
// content recorded during silence.
static constexpr size_t kTailMargin = 4800;  // 100ms, safely > min_offset

// Runs 1s of a 220Hz tone followed by 1s of silence through the processor at
// MetaModule's 64-frame Process() cadence, with a continuous (non-random,
// always-on) grain stream so the feedback loop has something to recirculate.
// Returns { tail energy, tail "roughness" } measured only on the silent tail,
// after the read-ahead delay has drained the tone's own echo.
//
// Roughness = sum of squared sample-to-sample deltas. It is normalized by
// energy so it is a *scale-invariant* signature of waveform smoothness: a
// signal made of per-sample-varying audio has low roughness/energy, while a
// signal built by holding one value constant for a 64-sample block and then
// jumping to a new held value has a sharp discontinuity every block boundary,
// which injects broadband energy and drives roughness/energy way up
// regardless of the signal's overall loudness.
struct TailStats { double energy; double roughness; };

TailStats FeedbackTailStats(float feedback) {
    Proc proc;
    ParticulesParameters params{};
    params.dry_wet = 1.0f;          // full wet
    params.auto_gain = false;
    params.manual_gain_db = 0.0f;
    params.feedback = feedback;
    params.reverb = 0.0f;
    params.density = 0.0f;          // max regular (non-random) grain rate —
                                     // continuous coverage, deterministic timing
    params.time = 0.0f;             // read as close to write head as possible
    proc.p.SetParameters(params);

    constexpr size_t kFrames = 48000 * 2;   // 2 s
    StereoFrame in[64], out[64];
    double energy = 0.0, roughness = 0.0;
    float prev = 0.0f;
    bool have_prev = false;
    for (size_t off = 0; off < kFrames; off += 64) {
        for (size_t i = 0; i < 64; ++i) {
            size_t n = off + i;
            float v = (n < 48000)   // 1 s of tone, then 1 s of silence
                ? 0.5f * std::sin(2.0 * M_PI * 220.0 * n / kSR) : 0.0f;
            in[i] = {v, v};
        }
        proc.p.Process(in, out, 64);
        if (off >= 48000 + kTailMargin) {
            for (size_t i = 0; i < 64; ++i) {
                float x = out[i].l;
                energy += (double)x * x;
                if (have_prev) {
                    double d = x - prev;
                    roughness += d * d;
                }
                prev = x;
                have_prev = true;
            }
        }
    }
    return {energy, roughness};
}
}  // namespace

// At 64-frame cadence (MetaModule), the feedback path must recirculate the
// previous block's wet output per-sample. Before the fix, the input loop
// read a single feedback_sample held constant for the whole 64-frame block
// (only updated in the wet loop), so the recirculated signal was really a
// staircase: one value repeated 64 times, then an abrupt jump to a new held
// value at the next block boundary. That jump injects broadband energy at
// every block boundary, which shows up as a large, waveform-shape-dependent
// "roughness" (mean squared sample-to-sample delta) relative to the signal's
// own energy — independent of whether the held-sample scheme happens to be
// louder or quieter overall (it isn't reliably either, since which sample
// gets "held" is essentially phase noise relative to the true waveform).
//
// Measured on this exact test (tail energy/roughness on the silent second,
// feedback=0.9, after the read-ahead delay has drained the tone's echo):
//   pre-fix:  energy=13.0146  roughness=0.396686  ratio=0.0304800
//   post-fix: energy=13.3524  roughness=0.0104284 ratio=0.0007810  (~39x lower)
// This ratio held stable (~0.03 vs ~0.0008-0.0009) across feedback in
// [0.5, 0.7, 0.9] and density in [0.0, 0.02, 0.05] in exploratory testing,
// confirming it is a robust signature of the block-held-sample bug rather
// than an artifact of one parameter choice. The threshold below is the
// geometric midpoint of the two measured ratios:
// sqrt(0.0304800 * 0.0007810) ~= 0.00488.
//
// Note: an earlier version of this test compared aggregate tail *energy*
// for feedback=0 vs feedback=0.9 (per the original task brief), on the
// theory that a "nearly inert" feedback path would add little energy.
// That metric did not hold up: because the previous-block-held sample can
// coincidentally sit near a waveform peak, repeating it for 64 samples can
// carry *more* average power than the true oscillating signal, so the
// pre-fix aggregate energy was frequently equal to or higher than post-fix,
// not lower. The roughness/energy ratio above is scale-invariant and
// directly targets the discontinuity the bug actually introduces, so it
// reliably distinguishes the two implementations regardless of loudness.
TEST_CASE("ParticulesProcessor: feedback recirculates per-sample at 64-frame cadence",
          "[processor][feedback]") {
    TailStats s = FeedbackTailStats(0.9f);
    REQUIRE(s.energy > 0.0);  // sanity: the feedback loop actually produced tail audio
    double ratio = s.roughness / s.energy;
    INFO("tail energy: " << s.energy << "  roughness: " << s.roughness
                          << "  roughness/energy: " << ratio);
    REQUIRE(ratio < 0.00488);
}

// Control case: feedback=0 baselines what the 0.00488 threshold means.
// `FeedbackTailStats` measures the tail *after* `kTailMargin` has drained
// the read-ahead delay's echo of the tone, so with recirculation fully
// disengaged there is no mechanism left to keep the buffer's tail region
// non-silent: nothing was written there but silence, and nothing feeds it
// back. Measured here: energy == 0.0 exactly (bit-for-bit), confirming the
// window really does isolate feedback-driven content — the energy and
// roughness measured in the feedback=0.9 case above are attributable to
// the recirculation path itself, not to some pre-existing grain-engine
// artifact the ratio metric would pick up regardless of feedback. It also
// guards against exactly the class of bug this suite cares about: any
// future change that makes feedback "leak" when the knob is fully off
// (e.g. a mixed-up smoothing target or an always-on feedback tap) would
// turn this exact-zero baseline into nonzero tail energy.
TEST_CASE("ParticulesProcessor: feedback=0 control produces silent tail",
          "[processor][feedback]") {
    TailStats s = FeedbackTailStats(0.0f);
    INFO("tail energy: " << s.energy << "  roughness: " << s.roughness);
    REQUIRE(s.energy == 0.0);
    REQUIRE(s.roughness == 0.0);
}
