#include <catch2/catch_amalgamated.hpp>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include "retours_delay_dsp/retours_dsp.h"
#include "random/random.h"
#include "util/dsp_utils.h"
#include "buffer/recording_buffer.h"

using namespace retours_delay_dsp;

namespace {

struct Proc {
    void* mem = nullptr;
    RetoursProcessor p;
    explicit Proc(float sr = 48000.f) {
        auto req = RetoursProcessor::GetMemoryRequirements(sr);
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

// Mirrors BaseTimeControl::Update()'s unclocked/manual-mode formula
// (base_time.cpp) for a fixed density knob and a given quality's buffer
// duration. Used by the corner-stress test below to prove a quality
// transition actually changed the buffer duration that DENSITY's mapping
// sees (rather than just re-deriving the production formula for its own
// sake) — BaseTimeSeconds() is the cleanest observable for this because
// BaseTimeControl::Update() recomputes it synchronously from
// buffer_samples_ every block (no slew), unlike DelayTimeSeconds() which
// one-pole slews toward its target over ~0.3 s.
// Live recording-buffer duration for a quality mode, derived the same way
// production does (RecordingBuffer::FramesForConfig over the fixed byte pool,
// times the mode's decimation factor). Quality modes differ by more than 4x
// here -- kScorchedCassette packs mu-law 8-bit at decimation 2, kBrightDigital
// float32 at decimation 1 -- which is exactly why a mode change can leave a
// latched delay value larger than the buffer it now indexes.
float BufferSecondsFor(QualityMode quality, float sr) {
    auto cfg = particules_dsp::QualityConfigFor(quality);
    size_t capacity_bytes =
        (kBufferFrames + particules_dsp::kInterpolationTail) * 2 * sizeof(float);
    size_t frames = particules_dsp::RecordingBuffer::FramesForConfig(
        capacity_bytes, /*channels=*/2, cfg.format, cfg.max_bytes);
    return static_cast<float>(frames) * static_cast<float>(cfg.decimation) / sr;
}

float ExpectedBaseSeconds(float density, QualityMode quality, float sr) {
    float buffer_seconds = BufferSecondsFor(quality, sr);
    float buffer_samples = sr * buffer_seconds;
    float d = std::clamp(std::fabs(density - 0.5f) * 2.f, 0.f, 1.f);
    float base = buffer_samples * std::exp2(-kManualOctaves * d);
    float min_samples = kMinDelaySeconds * sr;
    return std::clamp(base, min_samples, buffer_samples) / sr;
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

    RetoursParameters params;
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
// 4th SetParameters step (leaving 4-step unfrozen stretches -- see below),
// pitch alternating +/-24 every step, quality cycling roughly every 0.5 s,
// over 5 s of noise input. No non-finite samples, no crash, and the
// quality-cycling dimension is proven to be alive (transitions do apply,
// checked in a dedicated settle phase after the churn -- see below).
//
// Freeze toggling on *every* step (the original pattern) meant the
// quality-apply guard in ProcessBlock (retours_processor.cpp ~130:
// `!params.freeze && !freeze_falling_edge`, the Task-8 one-block deferral)
// was false on every single block, so quality transitions
// (SetDecimationFactor/recording_buffer.Clear()/the duck/SetBufferSeconds)
// never fired -- the "quality cycling" stress dimension was silently dead.
// Toggling only every 4th step leaves stretches where 3 of the 4 steps have
// both this block and the previous block unfrozen, satisfying the guard;
// `guard_open` below mirrors that exact condition so a scheduled quality
// change is only committed at a step where the production code will
// actually start a transition.
//
// Fix round (mid-fade freeze abort): a later fix made the apply point
// (retours_processor.cpp, the kFadeOut case) itself abort back to kFadeIn
// if freeze is engaged (or its falling edge lands) on the very block the
// fade-out counter reaches zero, deferring the apply rather than
// Configure()+Clear()-ing the buffer out from under frozen content. Under
// this test's period-4 freeze churn, that abort/retry cycle (32 blocks
// kFadeOut + 32 blocks kFadeIn, both exact multiples of the 4-step freeze
// period) can stay phase-locked with the freeze toggle for a long,
// unpredictable number of retries before a variable guard-reopen delay
// finally desyncs it -- observed, in one run, taking ~1500 steps for a
// single commit to actually land, ~5x this test's old fixed 300-step
// settle margin and past the *next* scheduled commit's own window. That
// isn't a bug: correctly protecting frozen content is worth an unbounded
// apply delay under adversarial, exact-multiple freeze churn (not a
// realistic playing pattern) -- but it means checking each transition's
// apply against a small fixed per-commit deadline mid-churn is no longer a
// sound test. Instead: churn proves nothing crashes; a dedicated,
// churn-free settle phase afterward (freeze forced off for good, so the
// abort guard can never re-trigger) proves the last scheduled transition
// really does apply.
// ---------------------------------------------------------------------------
TEST_CASE("corner stress: extreme params with freeze/quality churn stay finite") {
    const float sr = 48000.f;
    const size_t total = static_cast<size_t>(5.0f * sr);
    const size_t chunk = 64;
    const size_t steps = total / chunk;
    const size_t quality_period_steps =
        std::max<size_t>(1, static_cast<size_t>(0.5f * sr) / chunk);

    QualityMode qualities[4] = {QualityMode::kBrightDigital, QualityMode::kColdDigital,
                                 QualityMode::kSunnyTape, QualityMode::kScorchedCassette};
    float densities[2] = {0.f, 1.f};
    float times[2] = {0.f, 1.f};

    // Freeze value changes only once every 4 steps (steps 0-3 one value,
    // 4-7 the other, ...), not every step -- see the fix-round comment
    // above for why transitions are no longer checked mid-churn against a
    // fixed deadline keyed off this period.
    const size_t kFreezeGroup = 4;
    auto freeze_at = [&](size_t s) { return ((s / kFreezeGroup) % 2) == 1; };

    for (float density : densities) {
        for (float time : times) {
            Proc proc(sr);
            particules_dsp::Random rng;
            rng.Init(0x5EED0000u ^ static_cast<uint32_t>(density * 1000) ^
                     static_cast<uint32_t>(time * 37));

            RetoursParameters params;
            params.density = density;
            params.time = time;
            params.shape = 1.f;
            params.feedback = 1.f;

            bool pitch_high = true;
            bool all_finite = true;
            size_t first_bad_step = SIZE_MAX;

            size_t quality_idx = 0;
            size_t next_boundary = quality_period_steps;
            size_t commits = 0;

            std::vector<StereoFrame> in(chunk), out(chunk);
            for (size_t step = 0; step < steps; ++step) {
                params.pitch_semitones = pitch_high ? 24.f : -24.f;
                pitch_high = !pitch_high;

                bool cur_frozen = freeze_at(step);
                bool prev_frozen = (step == 0) ? false : freeze_at(step - 1);
                bool guard_open = !cur_frozen && !prev_frozen;
                params.freeze = cur_frozen;

                // Only commit the next quality once the guard is actually
                // open, so the transition being scheduled is one the
                // production code will genuinely start this step.
                if (step >= next_boundary && guard_open) {
                    quality_idx = (quality_idx + 1) % 4;
                    next_boundary += quality_period_steps;
                    ++commits;
                }
                params.quality = qualities[quality_idx];
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

            // Sanity net on the scheduling itself: the quality-cycling
            // dimension must actually be alive (matches the original
            // fixed expected-transitions count -- every nominal ~0.5 s
            // boundary got a chance to commit).
            size_t expected_commits = (steps - 1) / quality_period_steps;
            REQUIRE(commits == expected_commits);

            // Dedicated settle phase: freeze forced off for good (the
            // churn is over), so the mid-fade abort guard can never
            // re-trigger and the last committed transition is guaranteed
            // to run its full fade(2048)+clear(~8192)+fade(2048) cycle
            // (~192 blocks) to completion. 400 blocks is comfortable
            // headroom over that, plus the up-to-one-block
            // freeze-falling-edge deferral before it even starts.
            params.freeze = false;
            const size_t kSettleBlocks = 400;
            std::vector<StereoFrame> settle_in(kSettleBlocks * chunk),
                settle_out(kSettleBlocks * chunk);
            for (auto& f : settle_in) {
                float n = rng.NextBipolar();
                f = {n, n};
            }
            proc.p.SetParameters(params);
            proc.p.Process(settle_in.data(), settle_out.data(), settle_in.size());

            for (auto& f : settle_out) {
                REQUIRE(std::isfinite(f.l));
                REQUIRE(std::isfinite(f.r));
            }

            float expected = ExpectedBaseSeconds(density, qualities[quality_idx], sr);
            float actual = proc.p.BaseTimeSeconds();
            INFO("density=" << density << " time=" << time
                             << " final quality index=" << quality_idx);
            REQUIRE(actual == Catch::Approx(expected).margin(1e-5));
        }
    }
}

// ---------------------------------------------------------------------------
// (b2) Quality churn in kCrossfade mode with a large retarget queued on every
// block. This is the coverage net for the two fade-start re-clamps in
// EchoEngine (SetTargets' idle->fade branch and ReadWet's fade-complete
// dequeue), which exist because a quality-mode SIZE SHRINK can land while a
// fade is in flight: the shrink is queued rather than applied, so
// target_frames_/delay_frames_ can still hold the OLD, larger buffer's
// magnitude when they become the next fade's endpoints and the alignment
// correlation reference. Nothing else in the suite drives that combination --
// the five pinning scenarios never change quality, and the (b) corner-stress
// case above runs in kTape mode, where the crossfade fade-start paths are
// never reached.
//
// TWO TRAPS, both found by instrumenting this case with counters rather than
// trusting that it did what its name said:
//
//  1. Do NOT churn FREEZE on a short period here. ReadWet's frozen branch
//     returns before the crossfade code, so fade_pos_ only advances while
//     unfrozen, and NotifyFreeze's unfreeze edge resets fade_pos_ to 0. An
//     earlier version of this test alternated freeze every 4 blocks, which
//     restarted the fade faster than kJumpCrossfadeFrames could ever elapse:
//     the whole 5-second run produced exactly ONE fade start and zero
//     dequeues. Freeze is therefore pulsed for 64 blocks out of every 512
//     (~12%), leaving long uninterrupted runs for fades to complete in.
//     Quality transitions still commit freely -- production gates them on the
//     freeze guard being open, which it is for most of this run.
//
//  2. Do NOT retarget between the two EXTREMES of the density mapping. The
//     aligner's radius is capped at kAlignSearchMaxFraction of
//     min(cur, want), so if either end of the swing is the ~2 ms minimum
//     delay, the radius collapses below kAlignMinRadiusFrames and alignment is
//     skipped on every fade. Density sweeps 0.30..0.50 instead: both ends are
//     long (buffer/21 up to the whole buffer, a >20x span), so the retargets
//     stay large AND the aligner actually runs -- including in the decimated
//     quality modes, which nothing else covers.
//
//  3. Do NOT square-wave the knob between two values, either. A fade is
//     exactly kJumpCrossfadeFrames = 1024 samples = 16 blocks long, so any
//     alternation whose period divides 16 blocks is sampled at the same phase
//     by every fade-complete dequeue: `want` comes out equal to `cur`, the
//     move-relative radius cap collapses to zero, and alignment is skipped.
//     An earlier version of this test alternated every block and bailed at the
//     radius guard on 221 of 237 calls for exactly that reason. A continuous
//     triangle sweep gives every fade a genuinely new target, which is also
//     what the feature is actually for.
//
// COVERAGE ACTUALLY DELIVERED, measured by temporarily instrumenting
// AlignedFadeTarget and the two fade-start sites with counters (do this again
// if you change the timings above -- all three traps produced a test that
// passed while covering almost nothing):
//
//     fade-start dequeues        237   (ReadWet's fade-complete path)
//     alignment calls            237
//       bailed at radius guard     1
//       reached a decision       236   (max radius reached: 96, the cap)
//       actually moved a target   12
//
// The SetTargets idle->fade site runs once, at the very first retarget; after
// that the knob never stops moving, so the engine is permanently mid-fade and
// every subsequent fade start comes through the dequeue path. That is the
// realistic split for a sweep, not a gap in the test.
//
// What this asserts is robustness, not a tight numeric bound: output stays
// finite, the reported delay stays finite/non-negative and never exceeds the
// longest buffer any cycled mode provides, and the delay genuinely swings over
// the requested range. It deliberately does NOT claim a mutation-proof bound on
// the clamps themselves -- during a transition the live quality lags the
// requested one, so the exact live buffer size is not observable from the public
// API at that instant. Its job is to run these paths under churn, and under
// ASan/UBSan to prove the reads stay in bounds (verified: the whole 77-case
// suite is clean under -fsanitize=address,undefined).
// ---------------------------------------------------------------------------
TEST_CASE("corner stress: crossfade retargets across quality shrinks stay bounded") {
    const float sr = 48000.f;
    const size_t chunk = 64;
    const size_t steps = static_cast<size_t>(8.0f * sr) / chunk;
    // ~0.6 s per quality. This has to be comfortably LONGER than a full
    // transition (fade 2048 + clear ~8192 + fade 2048 samples, ~0.26 s) plus
    // enough steady-state afterwards for the freshly-cleared buffer to refill
    // with signal -- otherwise the buffer is perpetually mid-clear, the
    // correlation windows are all zeros, and the aligner bails at its
    // `best <= 0` guard on nearly every call (measured: at a 0.1 s period it
    // reached a decision 13 times in 5 s and never once moved a target).
    // Fades are only 1024 samples (16 blocks) long, and the knob moves every
    // block, so shrinks still land mid-fade constantly at this period.
    const size_t quality_period_steps =
        std::max<size_t>(1, static_cast<size_t>(0.6f * sr) / chunk);

    QualityMode qualities[4] = {QualityMode::kScorchedCassette, QualityMode::kBrightDigital,
                                QualityMode::kSunnyTape, QualityMode::kColdDigital};
    float longest_buffer_s = 0.f;
    for (auto q : qualities)
        longest_buffer_s = std::max(longest_buffer_s, BufferSecondsFor(q, sr));

    // Sparse freeze pulses: 64 blocks frozen out of every 512 (see trap 1).
    auto freeze_at = [](size_t s) { return ((s / 64) % 8) == 7; };

    Proc proc(sr);
    particules_dsp::Random rng;
    rng.Init(0xC0FFEE01u);

    RetoursParameters params;
    params.time = 0.f;
    params.shape = 1.f;
    params.feedback = 1.f;
    params.time_change_mode = TimeChangeMode::kCrossfade;

    bool all_finite = true;
    bool delay_in_range = true;
    size_t first_bad_step = SIZE_MAX;
    float worst_delay = 0.f;

    size_t quality_idx = 0;
    size_t next_boundary = quality_period_steps;
    size_t commits = 0;

    std::vector<StereoFrame> in(chunk), out(chunk);
    for (size_t step = 0; step < steps; ++step) {
        // Continuous 3 s triangle sweep over 0.30..0.50 density (see traps 2
        // and 3): the requested delay covers buffer/2^4.4 up to the whole
        // buffer, moving every block, so every fade lands a real distance from
        // the last one.
        float t = static_cast<float>(step * chunk) / sr;
        float ph = std::fmod(t, 3.0f);
        float frac = (ph < 1.5f) ? (ph / 1.5f) : ((3.0f - ph) / 1.5f);
        params.density = 0.30f + 0.20f * frac;
        params.pitch_semitones = (step % 2 == 0) ? 24.f : -24.f;

        bool cur_frozen = freeze_at(step);
        bool prev_frozen = (step == 0) ? false : freeze_at(step - 1);
        bool guard_open = !cur_frozen && !prev_frozen;
        params.freeze = cur_frozen;

        // Quality transitions are gated on the freeze guard in production, so
        // only commit when it is genuinely open (same discipline as (b)).
        if (step >= next_boundary && guard_open) {
            quality_idx = (quality_idx + 1) % 4;
            next_boundary += quality_period_steps;
            ++commits;
        }
        params.quality = qualities[quality_idx];
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

        float d = proc.p.DelayTimeSeconds();
        worst_delay = std::max(worst_delay, d);
        if (!std::isfinite(d) || d < 0.f || d > longest_buffer_s * 1.001f) {
            delay_in_range = false;
            if (first_bad_step == SIZE_MAX) first_bad_step = step;
        }
    }

    INFO("first_bad_step=" << first_bad_step << " worst_delay=" << worst_delay
                            << " longest_buffer_s=" << longest_buffer_s);
    REQUIRE(all_finite);
    REQUIRE(delay_in_range);
    // Sanity nets, so this case can never silently stop covering what it
    // exists for. The quality-cycling dimension must be alive, and the delay
    // must genuinely be swinging over the range the retargets ask for -- a
    // run that produced one fade and then sat still (the trap-1 failure mode)
    // fails the latter.
    REQUIRE(commits >= 4);
    REQUIRE(worst_delay > 0.5f * BufferSecondsFor(QualityMode::kBrightDigital, sr));
}

// ---------------------------------------------------------------------------
// (c) ClearBuffer() mid-feedback: a loud feedback tail must die within the
// buffer-clear, not linger through the following silence.
// ---------------------------------------------------------------------------
TEST_CASE("ClearBuffer mid-feedback kills the tail") {
    const float sr = 48000.f;
    Proc proc(sr);
    RetoursParameters params;
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
    RetoursParameters params;
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

// ---------------------------------------------------------------------------
// (e) Fix round: a single block of NaN on a CV input must not permanently
// poison the DSP. Pre-fix, a NaN density_cv reaches
// BaseTimeControl::Update() -> base = buffer_samples_ * exp2(-octaves*d) *
// exp2(-density_cv_volts) -> NaN base_samples -> NaN delay_samples fed into
// EchoEngine::SetTargets() -> NaN target_frames_/delay_frames_. The
// first-target snap guard (`delay_frames_ < 0.f`) is false for NaN (all
// comparisons with NaN are false), so a later, valid density_cv can never
// re-snap delay_frames_ away from NaN -- `delay_frames_ += slew_coeff_ *
// (target - delay_frames_)` keeps it NaN forever once poisoned. Separately, a
// NaN dry_wet_cv poisons dry_wet_eff -> smoothed_dry_wet via the per-sample
// OnePole (same permanent-poison mechanism). Fixed by sanitizing every float
// field of RetoursParameters at the SetParameters() ingestion boundary (mirrors
// Particules' pattern) plus a NaN-aware first-target snap in
// EchoEngine::SetTargets(). Density case verified RED against the unfixed
// core (temporarily reverting the RetoursProcessor::SetParameters guard block):
// DelayTimeSeconds() stayed NaN for the remainder of the run even after
// density_cv returned to 0; with the fix, it recovers within this test's
// post-glitch window.
// ---------------------------------------------------------------------------
TEST_CASE("NaN CV input for one block does not permanently poison the DSP") {
    const float sr = 48000.f;
    Proc proc(sr);
    RetoursParameters params;
    params.dry_wet = 1.f;
    params.feedback = 0.f;
    params.density = KnobForSeconds(0.15f);  // ~150 ms
    params.time = 0.f;
    proc.p.SetParameters(params);

    // Settle the delay-time slew before the glitch.
    std::vector<StereoFrame> settle(static_cast<size_t>(0.5f * sr), StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> settle_out(settle.size());
    proc.p.Process(settle.data(), settle_out.data(), settle.size());
    REQUIRE(std::isfinite(proc.p.DelayTimeSeconds()));

    // Glitch block 1: NaN density_cv for one SetParameters/Process call.
    params.density_cv = std::nanf("");
    proc.p.SetParameters(params);
    std::vector<StereoFrame> glitch_in(kMaxBlockSize, StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> glitch_out(kMaxBlockSize);
    proc.p.Process(glitch_in.data(), glitch_out.data(), kMaxBlockSize);

    // Restore density_cv; glitch dry_wet_cv instead for a second block.
    params.density_cv = 0.f;
    params.dry_wet_cv = std::nanf("");
    proc.p.SetParameters(params);
    proc.p.Process(glitch_in.data(), glitch_out.data(), kMaxBlockSize);

    // Restore everything to sane values.
    params.dry_wet_cv = 0.f;
    proc.p.SetParameters(params);

    // Run for a further 0.5 s: output must be finite throughout, and the wet
    // delay must still be functioning (impulse echoes at the expected time,
    // DelayTimeSeconds() sane).
    std::vector<StereoFrame> post(static_cast<size_t>(0.5f * sr), StereoFrame{0.f, 0.f});
    std::vector<StereoFrame> post_out(post.size());
    proc.p.Process(post.data(), post_out.data(), post.size());
    for (auto& f : post_out) {
        REQUIRE(std::isfinite(f.l));
        REQUIRE(std::isfinite(f.r));
    }

    float delay_s = proc.p.DelayTimeSeconds();
    REQUIRE(std::isfinite(delay_s));
    REQUIRE(delay_s == Catch::Approx(0.15f).margin(0.02f));

    // Impulse test: the wet delay is still functioning normally.
    std::vector<StereoFrame> imp_in(static_cast<size_t>(1.0f * sr), StereoFrame{0.f, 0.f});
    const int impulse_at = 4800;
    imp_in[impulse_at] = {1.f, 1.f};
    std::vector<StereoFrame> imp_out(imp_in.size());
    proc.p.Process(imp_in.data(), imp_out.data(), imp_in.size());
    int peak = FindPeak(imp_out, impulse_at + 100);
    float measured_seconds = static_cast<float>(peak - impulse_at) / sr;
    REQUIRE(measured_seconds == Catch::Approx(0.15f).epsilon(0.05));
}

TEST_CASE("telemetry: IsClocked false->true after two ticks, holds, clears on demand") {
    const float sr = 48000.f;
    Proc proc(sr);
    RetoursParameters params;
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

    // Clock jack pulled, >5 s of silence: the tempo now HOLDS (no timeout).
    params.clock_tick_offset = -1;
    params.clock_connected = false;
    proc.p.SetParameters(params);
    size_t long_quiet = static_cast<size_t>(5.5f * sr);
    std::vector<StereoFrame> lq_in(long_quiet, StereoFrame{0.f, 0.f}), lq_out(long_quiet);
    proc.p.Process(lq_in.data(), lq_out.data(), long_quiet);
    REQUIRE(proc.p.IsClocked());

    // Explicit clear ("Clear tapped tempo") is the way back to free-running.
    proc.p.ClearTappedTempo();
    proc.p.Process(blk.data(), blk_out.data(), blk.size());
    REQUIRE_FALSE(proc.p.IsClocked());
}
