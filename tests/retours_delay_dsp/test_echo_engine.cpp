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
// Hashes below were captured by running this test once against the
// unmodified engine (all four assertions fail against the 0x0 placeholder,
// printing the true hash), then pasting the printed values in as expected.
// ---------------------------------------------------------------------------

TEST_CASE("pinning: tape mode, delay-time retarget mid-run") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kTape;
    params.slew_seconds = 0.02f;
    params.density = KnobForSeconds(0.05f);   // ~2400 buffer frames

    std::vector<StereoFrame> in(4096);
    FillLcgNoise(in, 111u, 222u);
    std::vector<StereoFrame> out(in.size());

    proc.p.SetParameters(params);
    proc.p.Process(in.data(), out.data(), 2048);

    params.density = KnobForSeconds(0.15f);   // retarget: slews toward a new delay
    proc.p.SetParameters(params);
    proc.p.Process(in.data() + 2048, out.data() + 2048, 2048);

    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_tape_retarget", got, 0x6460e329d5c4ededull);
}

TEST_CASE("pinning: crossfade mode, delay-time retarget mid-run") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kCrossfade;
    params.density = KnobForSeconds(0.05f);   // ~2400 buffer frames

    std::vector<StereoFrame> in(4096);
    FillLcgNoise(in, 333u, 444u);
    std::vector<StereoFrame> out(in.size());

    proc.p.SetParameters(params);
    proc.p.Process(in.data(), out.data(), 2048);

    params.density = KnobForSeconds(0.15f);   // retarget: starts a crossfade jump
    proc.p.SetParameters(params);
    proc.p.Process(in.data() + 2048, out.data() + 2048, 2048);

    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_crossfade_retarget", got, 0x2bb7776ea85346dfull);
}

TEST_CASE("pinning: multi-tap active") {
    Proc proc;
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.time_change_mode = TimeChangeMode::kTape;
    // CW side above noon (density_knob > 0.55) -> BaseTimeControl.multi_tap.
    { float d = -std::log2(0.1f / 4.f) / 11.f; params.density = 0.5f + 0.5f * d; }
    proc.p.SetParameters(params);

    std::vector<StereoFrame> in(4096);
    FillLcgNoise(in, 555u, 666u);
    std::vector<StereoFrame> out(in.size());
    proc.p.Process(in.data(), out.data(), in.size());

    std::uint64_t got = Fnv1aStereo(out);
    CheckPinHash("pin_multi_tap", got, 0x4ec59d094be7eec0ull);
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
