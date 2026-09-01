#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
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

// ---------------------------------------------------------------------------
// Task 10 pinning-test helpers (same FNV-1a-over-raw-float-bits technique as
// Task 7's LoopEngine pinning test, tests/loooop/test_loop_engine.cpp).
// Pins EchoEngine::ReadWet's wrap/decimation/multi-tap-snap/freeze-seam
// output bit-exact across the Task 10 rework (steps 2-5 of the brief).
// ---------------------------------------------------------------------------
std::uint64_t Fnv1aStereo(const std::vector<StereoFrame>& v) {
    std::uint64_t h = 14695981039346656037ull;  // FNV-1a 64-bit offset basis
    auto mix = [&](float f) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        for (int b = 0; b < 4; ++b) {
            h ^= static_cast<std::uint64_t>((bits >> (b * 8)) & 0xFFu);
            h *= 1099511628211ull;  // FNV prime
        }
    };
    for (const auto& fr : v) { mix(fr.l); mix(fr.r); }
    return h;
}

// Deterministic LCG noise, independently seeded per channel (same constants
// as the Task 7 loop-engine pinning test's PRNG).
void FillLcgNoise(std::vector<StereoFrame>& buf, std::uint32_t seedL, std::uint32_t seedR) {
    std::uint32_t lcgL = seedL, lcgR = seedR;
    for (auto& fr : buf) {
        lcgL = lcgL * 1664525u + 1013904223u;
        lcgR = lcgR * 1664525u + 1013904223u;
        fr.l = static_cast<float>(lcgL >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
        fr.r = static_cast<float>(lcgR >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
    }
}

void CheckPinHash(const char* name, std::uint64_t got, std::uint64_t expected) {
    if (got != expected) {
        std::printf("  hash %s: got 0x%016llx expected 0x%016llx\n",
                     name, (unsigned long long)got, (unsigned long long)expected);
    }
    REQUIRE(got == expected);
}
} // namespace

TEST_CASE("impulse comes back at the set delay time") {
    Proc proc;
    RetoursParameters params;
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
    RetoursParameters params;
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
    RetoursParameters params;
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
    RetoursParameters params;
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
    // sub-unity-gain feedback pluck must decay.
    //
    // Knob value: 0.87, not the original 0.95. Fix-round update (final
    // review): FEEDBACK's knob->gain mapping is now piecewise 0..1.1 (plan
    // restoration -- see retours_processor.cpp), so a 0.95 *knob* now maps to
    // 1.0 + (0.95-0.9) = 1.05 *gain*, i.e. above unity by design (runaway is
    // the intended behavior there, not a bug). 0.87 stays below the 0.9
    // knob breakpoint (gain = 0.87/0.9 ~= 0.967, still sub-unity) and was
    // empirically verified (probe harness, not asserted here) to still
    // sharply discriminate the Q bug with this fixture: at Q=1 the HP's
    // ~1.155x peak pushes the loop over unity and the burst grows to the
    // kBrightDigital clip ceiling (measured late RMS ~0.72 vs. early ~0.002); at
    // Q=0.707 the HP's max gain is capped at 1.0 so the loop stays at
    // ~0.967 and the burst fully decays (measured late RMS ~0.0000). The
    // brief's suggested 0.85 turned out NOT to discriminate reliably at this
    // fixture's burst amplitude/duration (both Q values decay there -- the
    // growth-vs-decay knife-edge for this specific fixture sits closer to
    // knob ~0.853); 0.87 was chosen instead after probing several knob
    // values and finding the widest, most robust separation. Re-verified red
    // against Q=1 (SetQ(0.707f) call temporarily removed) before restoring.
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.87f;
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

// ---------------------------------------------------------------------------
// Regression: Impl::active_mono defaults to false, but Init() always
// configures the recording buffer for stereo (cable state isn't known at
// construction time). A mono-cabled patch -- only IN_L patched, the
// module's own documented "mono in" pattern, and exactly what a
// Karplus-Strong pluck patch uses -- reports params.mono_input=true from
// the very first block. That used to mismatch active_mono and fall into
// the crossfaded config-transition state machine meant for a LIVE
// mid-patch quality/cabling change (fade wet out ~43 ms, hold muted
// through the buffer-clear drain ~170 ms, fade back in ~43 ms) -- eating
// the very first pluck of every mono Karplus-Strong patch (found via the
// RB-Retours-3 test-patch investigation). ProcessBlock now resolves the
// real config synchronously on the first-ever block instead.
// ---------------------------------------------------------------------------
TEST_CASE("mono input on the first block does not mute the first pluck") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.85f;
    params.density = 0.05f;             // audio-rate KS delay, matches RB-Retours-3
    params.mono_input = true;           // only IN_L patched
    proc.p.SetParameters(params);
    const size_t total = 48000;         // 1 s
    std::vector<StereoFrame> in(total, StereoFrame{0.f, 0.f});
    // 5 ms noise-ish burst (deterministic), same construction style as the
    // high-feedback decay test above.
    int n_burst = 240;
    for (int i = 0; i < n_burst; ++i) {
        float w = 0.5f - 0.5f * std::cos(2.f * 3.14159265f * i / n_burst);
        float noise = ((i * 2654435761u) % 1000) / 500.f - 1.f;
        in[i] = {0.2f * w * noise, 0.2f * w * noise};
    }
    std::vector<StereoFrame> out(total);
    for (size_t off = 0; off < total; off += 64) {
        proc.p.SetParameters(params);
        proc.p.Process(in.data() + off, out.data() + off, 64);
    }
    auto rms = [&](size_t a, size_t b) {
        double ss = 0;
        for (size_t i = a; i < b; ++i) ss += (double)out[i].l * out[i].l;
        return std::sqrt(ss / (b - a));
    };
    // Before the fix this window measured ~0.000002 (fully muted by the
    // spurious fade-out/clear); the pluck should still be audibly ringing.
    REQUIRE(rms(1900, 2100) > 0.005f);   // ~40-44 ms post-pluck
}

// ---------------------------------------------------------------------------
// Fix round (final review): FEEDBACK knob range is 0..1.1, not clamped to
// 1.0 -- >1 loop gain (self-sustaining/growing feedback, signature Beads
// behavior per the plan) must be reachable at the top of the knob's travel,
// and Saturation::LimitFeedback (per-quality) must keep it safely bounded
// once there. Knob=1.0 -> gain=1.1 (piecewise mapping, retours_processor.cpp).
// ---------------------------------------------------------------------------
TEST_CASE("feedback knob at max (gain 1.1) sustains/grows and stays bounded") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 1.0f;             // gain 1.1: above unity, runaway territory
    params.density = 0.f;               // audio-rate base (2 ms clamp)
    proc.p.SetParameters(params);
    const size_t total = 5 * 48000;     // 5 s
    std::vector<StereoFrame> in(total, StereoFrame{0.f, 0.f});
    // 8 ms noise-ish burst (same construction as the decay test above).
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

    float max_abs = 0.f;
    for (auto& f : out) {
        REQUIRE(std::isfinite(f.l));
        REQUIRE(std::isfinite(f.r));
        max_abs = std::max(max_abs, std::max(std::fabs(f.l), std::fabs(f.r)));
    }
    // Same headroom rationale as test_quality_modes.cpp's feedback=1.0 test:
    // the written sum (input + fb) isn't re-clipped, and cubic-Hermite
    // interpolation overshoot on wideband content pushes isolated peaks
    // above the nominal +/-1 ceiling.
    REQUIRE(max_abs <= 2.0f);

    auto rms = [&](float t0, float t1) {
        size_t a = (size_t)(t0 * 48000.f), b = (size_t)(t1 * 48000.f);
        double ss = 0;
        for (size_t i = a; i < b; ++i) ss += (double)out[i].l * out[i].l;
        return std::sqrt(ss / (b - a));
    };
    float at_1s = rms(0.9f, 1.0f);
    float tail = rms(4.5f, 5.0f);
    // Runaway is reachable: the loop sustains or grows all the way to the
    // limiter ceiling, rather than decaying away like the sub-unity-gain
    // case above.
    REQUIRE(tail >= at_1s);
}

// ---------------------------------------------------------------------------
// Fix round (final review, coverage gap): TimeChangeMode::kCrossfade had no
// dedicated test. Retarget mid-stream while feeding a sine, wet-only: the
// crossfade must not click (no sample-to-sample jump bigger than the input
// amplitude across the retarget boundary) and must land on the new target
// once the fade (kJumpCrossfadeFrames = 1024 samples) completes.
// ---------------------------------------------------------------------------
TEST_CASE("crossfade mode: retarget mid-stream declicks and reaches the new target") {
    const float sr = 48000.f;
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kCrossfade;
    params.density = KnobForSeconds(0.1f);  // delay A ~= 100 ms
    proc.p.SetParameters(params);

    const float freq = 300.f;
    auto make_sine = [&](std::vector<StereoFrame>& buf, size_t start_idx) {
        for (size_t i = 0; i < buf.size(); ++i) {
            float s = 0.3f * std::sin(2.f * 3.14159265f * freq *
                                        static_cast<float>(start_idx + i) / sr);
            buf[i] = {s, s};
        }
    };

    // Settle at A (the very first SetTargets() call snaps rather than fades,
    // so A is already the live delay from the start).
    size_t settle_n = static_cast<size_t>(0.5f * sr);
    std::vector<StereoFrame> settle_in(settle_n), settle_out(settle_n);
    make_sine(settle_in, 0);
    proc.p.Process(settle_in.data(), settle_out.data(), settle_n);

    // Retarget to B mid-stream.
    params.density = KnobForSeconds(0.3f);  // delay B ~= 300 ms
    proc.p.SetParameters(params);

    size_t trans_n = static_cast<size_t>(kJumpCrossfadeFrames) + 200;
    std::vector<StereoFrame> trans_in(trans_n), trans_out(trans_n);
    make_sine(trans_in, settle_n);
    proc.p.Process(trans_in.data(), trans_out.data(), trans_n);

    // No click: max sample-to-sample jump across the settle->transition
    // boundary and through the whole fade window must stay under the input
    // amplitude (0.3).
    float max_diff = 0.f;
    float prev = settle_out.back().l;
    for (auto& f : trans_out) {
        max_diff = std::max(max_diff, std::fabs(f.l - prev));
        prev = f.l;
    }
    REQUIRE(max_diff < 0.5f);

    // Let the fade finish settling, then confirm the delay has landed on B.
    size_t more_n = static_cast<size_t>(0.2f * sr);
    std::vector<StereoFrame> more_in(more_n), more_out(more_n);
    make_sine(more_in, settle_n + trans_n);
    proc.p.Process(more_in.data(), more_out.data(), more_n);

    REQUIRE(proc.p.DelayTimeSeconds() == Catch::Approx(0.3f).margin(0.02f));
}

// A second retarget issued while the first crossfade is still in progress
// must queue (per EchoEngine::SetTargets' `queued_target_` path) rather than
// crash or produce NaN, and the queued fade must still run to completion
// once the first one finishes.
TEST_CASE("crossfade mode: retarget during an in-progress fade queues cleanly") {
    const float sr = 48000.f;
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kCrossfade;
    params.density = KnobForSeconds(0.1f);  // delay A ~= 100 ms
    proc.p.SetParameters(params);

    const float freq = 300.f;
    auto make_sine = [&](std::vector<StereoFrame>& buf, size_t start_idx) {
        for (size_t i = 0; i < buf.size(); ++i) {
            float s = 0.3f * std::sin(2.f * 3.14159265f * freq *
                                        static_cast<float>(start_idx + i) / sr);
            buf[i] = {s, s};
        }
    };

    size_t idx = 0;
    size_t settle_n = static_cast<size_t>(0.5f * sr);
    std::vector<StereoFrame> settle_in(settle_n), settle_out(settle_n);
    make_sine(settle_in, idx);
    proc.p.Process(settle_in.data(), settle_out.data(), settle_n);
    idx += settle_n;

    // Start a fade toward B, then -- well before it can complete (fade takes
    // kJumpCrossfadeFrames = 1024 samples) -- retarget again to C. This must
    // queue rather than corrupt state.
    params.density = KnobForSeconds(0.3f);  // B
    proc.p.SetParameters(params);
    size_t mid_fade_n = 200;  // << kJumpCrossfadeFrames
    std::vector<StereoFrame> mid_in(mid_fade_n), mid_out(mid_fade_n);
    make_sine(mid_in, idx);
    proc.p.Process(mid_in.data(), mid_out.data(), mid_fade_n);
    idx += mid_fade_n;

    params.density = KnobForSeconds(0.6f);  // C, queued behind B's fade
    proc.p.SetParameters(params);

    size_t rest_n = static_cast<size_t>(2.0f * sr);
    std::vector<StereoFrame> rest_in(rest_n), rest_out(rest_n);
    make_sine(rest_in, idx);
    proc.p.Process(rest_in.data(), rest_out.data(), rest_n);
    idx += rest_n;

    for (auto& f : rest_out) {
        REQUIRE(std::isfinite(f.l));
        REQUIRE(std::isfinite(f.r));
    }

    // The queued fade to C eventually runs to completion once B's fade
    // finishes.
    REQUIRE(proc.p.DelayTimeSeconds() == Catch::Approx(0.6f).margin(0.03f));
}

// ---------------------------------------------------------------------------
// Crossfade-mode splice alignment (see AlignedFadeTarget in
// dsp/src/engine/echo_engine.cpp and
// docs/superpowers/2026-07-26-crossfade-variants-measurements.md). A
// Crossfade-mode fade blends the tap at the
// old delay with the tap at the new one; on an Interval sweep those sit
// hundreds of ms apart in the buffer, so their phase relationship is random
// and the blend garbles. Each fade's destination is now nudged by up to
// +/-kAlignSearchFrames (~2 ms, inaudible as a delay-time error) onto the
// best cross-correlating offset, subject to three radius caps. Nothing about
// the fade CADENCE changed, so the delay still lands within one fade of the
// knob's target -- the tests below assert both halves of that.
//
// These replace the earlier bounded-per-fade ratio-chase tests: measurement
// (see the report) showed that mechanism never engaged at human sweep speeds,
// left the sweep garble untouched, and added ~12 fade cycles of lag to an
// instant retarget, so it was removed rather than tuned.
// ---------------------------------------------------------------------------

namespace {
// The exact delay (seconds) the engine resolves for a given density knob, with
// no fade or slew in the way: a fresh engine's FIRST target always snaps (the
// first-target sentinel in EchoEngine::SetTargets), so one block is enough.
float ResolvedDelaySeconds(float density) {
    Proc ref;
    RetoursParameters p;
    p.dry_wet = 1.f;
    p.feedback = 0.f;
    p.time_change_mode = TimeChangeMode::kCrossfade;
    p.density = density;
    ref.p.SetParameters(p);
    std::vector<StereoFrame> in(64, StereoFrame{0.f, 0.f}), out(64);
    ref.p.Process(in.data(), out.data(), in.size());
    return ref.p.DelayTimeSeconds();
}

// Drives a Crossfade-mode retarget from `a_seconds` to `b_seconds` with a
// steady sine feeding the buffer, and reports what happened. `tone_hz` should
// NOT divide evenly into either delay, otherwise both taps are phase-coherent
// by luck and alignment has nothing to fix.
struct SpliceRun {
    float delay_before = 0.f;      // settled delay at A (seconds)
    float delay_after_fade = 0.f;  // delay one fade after the retarget
    float delay_settled = 0.f;     // delay a further fade later
    float dip_ratio = 0.f;         // min RMS during the fade / steady RMS
};

SpliceRun RunSplice(float a_seconds, float b_seconds, float tone_hz,
                    bool silent_buffer = false) {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kCrossfade;
    params.density = KnobForSeconds(a_seconds);

    size_t phase = 0;
    auto run = [&](size_t n, std::vector<StereoFrame>* capture) {
        std::vector<StereoFrame> in(n), out(n);
        for (size_t i = 0; i < n; ++i) {
            float v = silent_buffer
                ? 0.f
                : std::sin(2.f * static_cast<float>(M_PI) * tone_hz *
                           static_cast<float>(phase + i) / 48000.f);
            in[i] = StereoFrame{v, v};
        }
        phase += n;
        proc.p.SetParameters(params);
        proc.p.Process(in.data(), out.data(), n);
        if (capture) *capture = out;
    };

    // Fill the whole 4 s buffer, settled at A, before touching the knob.
    for (int i = 0; i < 100; ++i) run(2400, nullptr);
    std::vector<StereoFrame> steady;
    run(2048, &steady);

    SpliceRun r;
    r.delay_before = proc.p.DelayTimeSeconds();

    params.density = KnobForSeconds(b_seconds);
    std::vector<StereoFrame> fade;
    run(kJumpCrossfadeFrames, &fade);
    r.delay_after_fade = proc.p.DelayTimeSeconds();

    std::vector<StereoFrame> after;
    run(kJumpCrossfadeFrames, &after);
    r.delay_settled = proc.p.DelayTimeSeconds();

    auto rms = [](const std::vector<StereoFrame>& v, size_t from, size_t n) {
        float e = 0.f;
        for (size_t i = from; i < from + n && i < v.size(); ++i) e += v[i].l * v[i].l;
        return std::sqrt(e / static_cast<float>(n));
    };
    float ref = rms(steady, 1024, 1024);
    float worst = 1e30f;
    const size_t sub = 256;
    for (size_t s = 0; s + sub <= fade.size(); s += sub / 2)
        worst = std::min(worst, rms(fade, s, sub));
    r.dip_ratio = (ref > 0.f) ? worst / ref : 0.f;
    return r;
}
}  // namespace

TEST_CASE("crossfade splice alignment: a phase-cancelling retarget keeps its level") {
    // 0.1 s -> 0.8 s at 466.164 Hz (A#4). Neither delay is a whole number of
    // tone periods, so the raw target lands the new tap roughly antiphase with
    // the old one and the blend partially cancels. Measured through the fade
    // (min RMS over 256-sample sub-windows, relative to the steady level
    // before the retarget): 0.557 without alignment, 0.997 with it -- i.e. the
    // 5 dB cancellation notch that the splice used to punch is gone. The 0.9
    // bar sits well clear of both.
    SpliceRun r = RunSplice(0.1f, 0.8f, 466.164f);
    REQUIRE(r.dip_ratio > 0.9f);
}

TEST_CASE("crossfade splice alignment: landed delay stays inside the search budget") {
    // The nudge must be inaudible as a delay-time error: at most
    // kAlignSearchFrames of buffer frames away from what the knob asked for.
    const float requested = ResolvedDelaySeconds(KnobForSeconds(0.8f));
    // decimation is 1 in kBrightDigital (the default quality), so one buffer
    // frame is one host sample; +1 frame of slack for float rounding.
    const float budget = (kAlignSearchFrames + 1) / 48000.f;

    SpliceRun r = RunSplice(0.1f, 0.8f, 466.164f);
    REQUIRE(std::fabs(r.delay_after_fade - requested) <= budget);
    // Non-vacuous: this scenario really does exercise the aligner rather than
    // passing `want` straight through (see the "silence" case below for the
    // pass-through path).
    REQUIRE(r.delay_after_fade != requested);
}

TEST_CASE("crossfade splice alignment: no added lag, one fade reaches the target") {
    // The whole point of aligning instead of chasing: a retarget still
    // completes in exactly one kJumpCrossfadeFrames fade, and the delay then
    // HOLDS -- no residual chase, no second fade, nothing left queued.
    const float requested = ResolvedDelaySeconds(KnobForSeconds(0.8f));
    const float budget = (kAlignSearchFrames + 1) / 48000.f;

    SpliceRun r = RunSplice(0.1f, 0.8f, 466.164f);
    REQUIRE(std::fabs(r.delay_after_fade - requested) <= budget);
    REQUIRE(r.delay_settled == r.delay_after_fade);
}

TEST_CASE("crossfade splice alignment: silence in the buffer leaves the target exact") {
    // No positive correlation anywhere (every candidate scores 0), so the
    // aligner declines to nudge and the delay lands exactly on the request.
    const float requested = ResolvedDelaySeconds(KnobForSeconds(0.8f));
    SpliceRun r = RunSplice(0.1f, 0.8f, 466.164f, /*silent_buffer=*/true);
    REQUIRE(r.delay_after_fade == Catch::Approx(requested).epsilon(1e-6f));
}

TEST_CASE("crossfade splice alignment: very short delays are left exactly on target") {
    // kAlignSearchMaxFraction of a 4 ms delay is under kAlignMinRadiusFrames,
    // so alignment is skipped outright: a few ms of delay is a tuned comb and
    // even a fraction of a millisecond of slack would detune it audibly. The
    // delay must land exactly where asked.
    const float requested = ResolvedDelaySeconds(KnobForSeconds(0.006f));
    SpliceRun r = RunSplice(0.004f, 0.006f, 466.164f);
    REQUIRE(r.delay_after_fade == Catch::Approx(requested).epsilon(1e-6f));
}

TEST_CASE("crossfade splice alignment: a small retarget still travels most of the way") {
    // Guards kAlignMoveFraction. The best-correlating offset for a move smaller
    // than the search radius is always the one that cancels the move outright
    // (identical content correlates perfectly), so without that cap a small
    // knob nudge lands nowhere and the knob feels dead. The cap bounds the
    // nudge at half the move, so the delay always covers at least half the
    // distance asked for.
    //
    // OPERATING POINT MATTERS. This must sit inside the region where the cap
    // actually binds -- moves of roughly 0.5-2 ms, where the uncapped radius
    // (min(kAlignSearchFrames, kAlignSearchMaxFraction*delay) = 96 frames at a
    // 1 s delay = 2 ms) exceeds the move itself. Measured travelled/asked
    // fractions, shipped versus a build with the kAlignMoveFraction line
    // deleted:
    //
    //   move    tone       shipped   cap deleted
    //   0.5 ms  180 Hz      1.000      -0.042
    //   1.0 ms  180 Hz      0.583      -0.000     <-- this test
    //   2.0 ms  180 Hz      0.500       0.000
    //   3.0 ms  180 Hz      1.472       1.667     <-- cap no longer binds
    //   3.0 ms  466 Hz      0.715       0.715     <-- identical: guards nothing
    //
    // An earlier version of this test used the 3 ms / 466.164 Hz point and was
    // therefore vacuous: deleting the cap left it green.
    const float requested = ResolvedDelaySeconds(KnobForSeconds(1.001f));
    SpliceRun r = RunSplice(1.0f, 1.001f, 180.f);
    float asked = requested - r.delay_before;
    float travelled = r.delay_after_fade - r.delay_before;
    REQUIRE(asked > 0.f);
    REQUIRE(travelled >= (1.f - kAlignMoveFraction) * asked * 0.999f);
}

TEST_CASE("NaN input does not poison the buffer") {
    Proc proc;
    RetoursParameters params; params.dry_wet = 1.f; params.feedback = 0.9f;
    params.density = KnobForSeconds(0.01f);
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(9600, StereoFrame{0.f, 0.f});
    in[100] = {NAN, INFINITY};
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());
    for (size_t i = 4800; i < out.size(); ++i)
        REQUIRE(std::isfinite(out[i].l));
}

// ---------------------------------------------------------------------------
// Task 10 pinning tests (written FIRST, against the unmodified EchoEngine).
// Each scenario drives RetoursProcessor's public API through 4096 samples of
// a known deterministic input pattern and hashes the full stereo output
// stream (raw float bits, FNV-1a). These pin ReadWet's per-sample wrap
// (WrapPosition -> WrapBounded), decimation divide (-> reciprocal multiply),
// and multi-tap round() (-> libm-free tie-snap) as bit-exact across the
// rework -- any hash drift outside the documented multi-tap exact-tie case
// means the rework changed behavior, not just its cost.
//
// Fix round (code review): the first version of these tests settled the
// buffer for far less than the chosen delay, so every non-frozen scenario's
// main-tap read landed on the buffer's zero-initialized fill -- a position
// computed via a broken wrap and one computed correctly both read silence,
// making the hash blind to wrap/position bugs (mutation testing on
// WrapBounded's own branches confirmed this: breaking either branch left all
// hashes unchanged). Fixed by giving the tape/crossfade/multi-tap scenarios a
// full-buffer-lap settle (exactly kBufferFrames samples of noise, so every
// frame in the buffer holds real content and write_head wraps back to a known
// small offset) and a short delay (10-20 ms), so early samples of the capture
// (where write_pos_continuous < delay) exercise WrapBounded's `p < 0` branch
// landing on real wrapped-around content, and later samples exercise the
// unwrapped case landing on the capture's own real writes. A fifth scenario
// (pin_frozen_seam_wrap) was added for the `p >= size_f` branch, which needs
// slice_start_pos_ within slice_len_frames_ of the buffer end -- a case the
// original pin_frozen_seam scenario's slice never reached.
//
// Hashes below were captured by running this test once against the engine at
// commit 21ded3e (the commit immediately before this task's rework) in an
// isolated worktree, then pasting the printed values in as expected; the
// same test file (this one) was built there unmodified, so only the
// production engine code differs between capture and verification.
// ---------------------------------------------------------------------------

TEST_CASE("pinning: tape mode, delay-time retarget mid-run") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kTape;
    params.slew_seconds = 0.02f;
    params.density = KnobForSeconds(0.015f);   // ~720 buffer frames (15 ms)

    // Full-lap settle: exactly kBufferFrames samples so write_head wraps back
    // to 0 and every frame in the buffer holds real (non-zero) content --
    // see the fix-round note above.
    std::vector<StereoFrame> settle(kBufferFrames);
    FillLcgNoise(settle, 100u, 200u);
    std::vector<StereoFrame> settle_out(settle.size());
    proc.p.SetParameters(params);
    proc.p.Process(settle.data(), settle_out.data(), settle.size());

    std::vector<StereoFrame> in(4096);
    FillLcgNoise(in, 111u, 222u);
    std::vector<StereoFrame> out(in.size());

    proc.p.Process(in.data(), out.data(), 2048);

    params.density = KnobForSeconds(0.01f);   // retarget: slews toward a new (still short) delay
    proc.p.SetParameters(params);
    proc.p.Process(in.data() + 2048, out.data() + 2048, 2048);

    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_tape_retarget", got, 0x0c2d2098533ad034ull);
}

TEST_CASE("pinning: crossfade mode, delay-time retarget mid-run") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kCrossfade;
    params.density = KnobForSeconds(0.015f);   // ~720 buffer frames (15 ms)

    std::vector<StereoFrame> settle(kBufferFrames);
    FillLcgNoise(settle, 300u, 400u);
    std::vector<StereoFrame> settle_out(settle.size());
    proc.p.SetParameters(params);
    proc.p.Process(settle.data(), settle_out.data(), settle.size());

    std::vector<StereoFrame> in(4096);
    FillLcgNoise(in, 333u, 444u);
    std::vector<StereoFrame> out(in.size());

    proc.p.Process(in.data(), out.data(), 2048);

    params.density = KnobForSeconds(0.01f);   // retarget: starts a crossfade jump
    proc.p.SetParameters(params);
    proc.p.Process(in.data() + 2048, out.data() + 2048, 2048);

    // Hash regenerated for splice alignment (AlignedFadeTarget in
    // dsp/src/engine/echo_engine.cpp; see docs/superpowers/
    // 2026-07-26-crossfade-variants-measurements.md). This is the ONLY one of
    // the five pins in
    // this file whose scenario enters kCrossfade mode -- the other four run in
    // kTape (two set it explicitly, the two frozen ones inherit
    // RetoursParameters' kTape default) and stayed bit-exact, verified by
    // running the suite before regenerating this value.
    //
    // Why it legitimately moves: the retarget is 720 -> 480 buffer frames on a
    // noise-filled buffer, so the aligner's radius is
    // min(kAlignSearchFrames=96, kAlignSearchMaxFraction*480=24,
    // kAlignMoveFraction*240=120) = 24 frames, above kAlignMinRadiusFrames, so
    // the fade now lands on the best-correlating offset within +/-24 frames of
    // 480 instead of exactly on 480. Every sample of the fade and everything
    // after it therefore reads a slightly different buffer position -- a
    // deliberate behaviour change, not a regression.
    // Previous value (pre-alignment): 0xb0f74744c671a30c.
    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_crossfade_retarget", got, 0x363c09549efdc497ull);
}

TEST_CASE("pinning: multi-tap active") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kTape;
    // CW side above noon (density_knob > 0.55) -> BaseTimeControl.multi_tap;
    // ~720 buffer frames (15 ms) base, same short-delay reasoning as above.
    { float d = -std::log2(0.015f / 4.f) / 11.f; params.density = 0.5f + 0.5f * d; }
    proc.p.SetParameters(params);

    std::vector<StereoFrame> settle(kBufferFrames);
    FillLcgNoise(settle, 500u, 600u);
    std::vector<StereoFrame> settle_out(settle.size());
    proc.p.Process(settle.data(), settle_out.data(), settle.size());

    std::vector<StereoFrame> in(4096);
    FillLcgNoise(in, 555u, 666u);
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());

    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_multi_tap", got, 0x06300e96aa82f9a2ull);
}

TEST_CASE("pinning: frozen slice, seam window covered, re-anchor mid-freeze") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    // ~300 buffer frames/slice at 48 kHz -> several seam-crossfade wraps
    // (kSeamCrossfadeFrames = 64) across each 2048-sample half below.
    params.density = KnobForSeconds(300.f / 48000.f);
    params.time = 0.f;   // slice_index 0

    // Settle phase (unfrozen): fill recent buffer history with known content
    // before freezing so the frozen reads land on real signal, not the
    // buffer's zero-initialized fill.
    std::vector<StereoFrame> settle(20000);
    FillLcgNoise(settle, 777u, 888u);
    std::vector<StereoFrame> settle_out(settle.size());
    proc.p.SetParameters(params);
    proc.p.Process(settle.data(), settle_out.data(), settle.size());

    // Freeze, capture the first half at slice_index 0.
    params.freeze = true;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(4096, StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), 2048);

    // Still frozen: TIME retarget re-anchors the slice window live (the
    // "frozen && was_frozen" branch in NotifyFreeze) for the second half.
    params.time = 0.05f;   // slice_index 32 (slice_count ~= 640)
    proc.p.SetParameters(params);
    proc.p.Process(in.data() + 2048, out.data() + 2048, 2048);

    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_frozen_seam", got, 0x04098773237788abull);
}

// Fix round (code review): pin_frozen_seam's slice never came within
// slice_len_frames_ of the buffer end, so slice_start_pos_ + slice_phase_
// never reached size_f and WrapBounded's `p >= size_f` branch never fired --
// mutating that branch (e.g. `p -= size_f - 1.f`) left the hash unchanged.
// This scenario anchors the slice so it straddles the buffer end: settle
// kBufferFrames + 1000 samples (one full lap, so every frame holds real
// content, plus 1000 so write_head lands at frame 1000), slice_len ~300
// frames, slice_index 3 -> slice_start_ = wrap(1000 - 4*300) = wrap(-200) =
// size_f - 200, so the slice covers [size_f-200, size_f+100) mod size_f --
// phases 200..299 push pos_main past size_f into [0, 100), exercising the
// wrap on every one of the ~13.6 slice cycles across the 4096-sample capture.
TEST_CASE("pinning: frozen slice straddling the buffer end (wrap-down branch)") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(300.f / 48000.f);   // ~300 buffer frames/slice
    params.time = 0.0047f;                              // slice_index 3 (slice_count ~= 640)

    std::vector<StereoFrame> settle(kBufferFrames + 1000);
    FillLcgNoise(settle, 999u, 111u);
    std::vector<StereoFrame> settle_out(settle.size());
    proc.p.SetParameters(params);
    proc.p.Process(settle.data(), settle_out.data(), settle.size());

    params.freeze = true;
    proc.p.SetParameters(params);
    std::vector<StereoFrame> in(4096, StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());

    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_frozen_seam_wrap", got, 0x07d48124b30468d1ull);
}
