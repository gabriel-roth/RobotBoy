/**
 * test_kota20.cpp — Test suite for MF20Filter.hpp
 *
 * Compile & run:
 *   g++ -std=c++17 -O2 -Wall -o test_kota20 test_kota20.cpp && ./test_kota20
 *
 * All expected values are derived from the filter's transfer function;
 * references in comments point to the source papers:
 *   [H] Huovilainen (2010) MSc thesis
 *   [S] Stinchcombe (2006) MS10/MS20 filter study
 */

#include "../../src/mf20/MF20Filter.hpp"
#include "../../src/mf20/engine.hpp"
#include "../../src/mf20/dsp_utils.hpp"   // resTaper — the module applies it before the filter
#include <cmath>
#include <cstdio>
#include <cassert>
#include <cfloat>

// ── Test harness ─────────────────────────────────────────────────────────────

static int sPassed = 0, sFailed = 0;

static void report(bool ok, const char* name, const char* detail = nullptr) {
    if (ok) {
        printf("  PASS  %s\n", name);
        ++sPassed;
    } else {
        printf("  FAIL  %s", name);
        if (detail) printf("  (%s)", detail);
        printf("\n");
        ++sFailed;
    }
}

// Check float within a relative tolerance.
static bool near(float actual, float expected, float tol = 0.05f) {
    if (expected == 0.f) return std::fabs(actual) < tol;
    return std::fabs(actual - expected) / std::fabs(expected) < tol;
}

// ── Signal utilities ─────────────────────────────────────────────────────────

// Compute RMS of a run of samples.
static float rmsRun(const float* buf, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += (double)buf[i] * buf[i];
    return (float)std::sqrt(acc / n);
}

// Check any sample in buf[0..n) is NaN or Inf.
static bool anyNonFinite(const float* buf, int n) {
    for (int i = 0; i < n; i++)
        if (!std::isfinite(buf[i])) return true;
    return false;
}

/**
 * Feed a sine wave through the filter and return the RMS of the LP and HP
 * outputs over the measurement window (after a settling period).
 *
 * Returned amplitudes relate to the input amplitude by |H(jω)|:
 *   rms_out / rms_in  ≈  |H(jω)| × (1/√2) / (1/√2) = |H(jω)|
 * (the √2 factors cancel when comparing RMS of two sine waves at the same
 *  amplitude, so we can compare rms_out directly to |H| × amp/√2.)
 */
struct SineResult { float lpRMS, bpRMS, hpRMS; };

static SineResult runSine(MF20Filter& f,
                           float freqHz, float amp,
                           float cutoffHz, float res,
                           float sampleRate,
                           int settleSamples,
                           int measureSamples) {
    f.reset();
    const float phaseInc = 2.f * 3.14159265f * freqHz / sampleRate;
    double accLp = 0.0, accBp = 0.0, accHp = 0.0;
    for (int i = 0; i < settleSamples + measureSamples; i++) {
        float in = amp * std::sin(phaseInc * (float)i);
        auto [lp, bp, hp] = f.process(in, cutoffHz, res);
        if (i >= settleSamples) {
            accLp += (double)lp * lp;
            accBp += (double)bp * bp;
            accHp += (double)hp * hp;
        }
    }
    return {
        (float)std::sqrt(accLp / measureSamples),
        (float)std::sqrt(accBp / measureSamples),
        (float)std::sqrt(accHp / measureSamples)
    };
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. Zero input → zero output
//    With state initialised to 0 and input = 0, all outputs must be exactly 0.
//    Verifies no spontaneous energy / DC drift at idle.
static void test_zero_io() {
    printf("\n1. Zero input → zero output\n");
    MF20Filter f;
    bool allOk = true;
    for (float res : {0.f, 0.5f, 1.f}) {
        for (float fc : {200.f, 1000.f, 8000.f}) {
            f.setSampleRate(44100.f);
            f.reset();
            bool ok = true;
            for (int i = 0; i < 1000; i++) {
                auto [lp, bp, hp] = f.process(0.f, fc, res);
                if (lp != 0.f || bp != 0.f || hp != 0.f) { ok = false; break; }
            }
            allOk = allOk && ok;
        }
    }
    report(allOk, "zero input → zero LP and HP for all (fc, res) combos");
}

// 2. reset() clears all state
//    Drive the filter to a non-zero state (impulse + resonance), reset it,
//    then verify the very next output with zero input is exactly zero.
static void test_reset() {
    printf("\n2. reset() clears state\n");
    MF20Filter f;
    f.setSampleRate(44100.f);

    // Kick the filter so it has non-zero internal state.
    for (int i = 0; i < 200; i++)
        f.process(i == 0 ? 1.f : 0.f, 1000.f, 0.9f);

    f.reset();

    // After reset, zero input → zero output immediately.
    auto [lp, bp, hp] = f.process(0.f, 1000.f, 0.f);
    report(lp == 0.f && bp == 0.f && hp == 0.f, "output is exactly 0 immediately after reset()");
}

// 3. DC passthrough (LP, res = 0)
//    At res=0 there is no feedback, so the LP output at DC must equal the input.
//    The HP must converge to 0 (DC is fully blocked by the first-order HP).
//    DC gain derivation: at steady state all derivatives = 0, so the integrators
//    pass through DC unchanged, giving lp = in.  HP = in − lp1 → 0.
static void test_dc_passthrough() {
    printf("\n3. DC passthrough (res = 0)\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();

    // Settle for 5× the time constant (τ = fs/(π·fc) samples).
    const float fc = 500.f, sr = 44100.f;
    const int settle = (int)(5.f * sr / (3.14159f * fc)) + 10;

    float lastLp = 0.f, lastHp = 0.f;
    for (int i = 0; i < settle + 200; i++) {
        auto [lp, bp, hp] = f.process(1.f, fc, 0.f);
        if (i >= settle) { lastLp = lp; lastHp = hp; }
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "lp=%.4f (want 1.0)", lastLp);
    report(near(lastLp, 1.f, 0.01f), "LP → 1.0 at DC with res=0", buf);
    snprintf(buf, sizeof(buf), "hp=%.6f (want 0.0)", lastHp);
    report(std::fabs(lastHp) < 0.001f, "HP → 0 at DC with res=0", buf);
}

// 4. LP amplitude at fc (two-pole response, no resonance)
//    Each one-pole LP stage attenuates by 1/√2 at its own cutoff frequency.
//    Two identical stages → combined gain = (1/√2)² = 0.5 at fc.
//    We check: rms_out / rms_in ≈ 0.5 × 1/√2 / 1/√2 = 0.5.
//    (RMS of the sine cancels; the amplitude ratio = |H(jωc)|.)
static void test_lp_at_fc() {
    printf("\n4. LP amplitude at fc = 0.5 (−6 dB)\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    const float fc = 1000.f, amp = 1.f;
    // Settle for ~100 cycles, measure over 100 cycles.
    int settle  = (int)(44100.f / fc * 100.f);
    int measure = (int)(44100.f / fc * 100.f);
    auto res = runSine(f, fc, amp, fc, 0.f, 44100.f, settle, measure);

    // Expected RMS of output: amp/√2 × |H| = amp/√2 × 0.5
    float expectedRMS = amp / std::sqrt(2.f) * 0.5f;
    char buf[64];
    snprintf(buf, sizeof(buf), "lp_rms=%.4f, expected=%.4f", res.lpRMS, expectedRMS);
    report(near(res.lpRMS, expectedRMS, 0.05f), "LP RMS at fc ≈ 0.5 × amp/√2", buf);
}

// 5. LP rolloff: more than 6 dB/oct above fc
//    Between fc and 2×fc the amplitude should drop by more than half.
//    (Exact two-pole 2nd order: |H(2fc)| = 1/(1+4) = 0.2, so the drop from
//     fc to 2fc is 0.5 → 0.2, well over 6 dB in one octave.)
static void test_lp_rolloff() {
    printf("\n5. LP rolloff > 6 dB/oct above fc\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    const float fc = 500.f, amp = 1.f;
    int settle  = (int)(44100.f / fc * 100.f);
    int measure = (int)(44100.f / fc * 100.f);

    auto at_fc  = runSine(f, fc,       amp, fc, 0.f, 44100.f, settle, measure);
    auto at_2fc = runSine(f, fc * 2.f, amp, fc, 0.f, 44100.f, settle, measure);

    // lp at 2fc should be less than lp at fc × 0.6
    // (exact ratio ≈ 0.2/0.5 = 0.4; we allow generous tolerance)
    float ratio = at_2fc.lpRMS / at_fc.lpRMS;
    char buf[64];
    snprintf(buf, sizeof(buf), "ratio(2fc/fc)=%.3f (want < 0.6)", ratio);
    report(ratio < 0.6f, "LP(2fc) < LP(fc) × 0.6 → steeper than 6 dB/oct", buf);

    // LP at 4fc should drop by another factor ≥ 2 relative to 2fc.
    auto at_4fc = runSine(f, fc * 4.f, amp, fc, 0.f, 44100.f, settle, measure);
    float ratio2 = at_4fc.lpRMS / at_2fc.lpRMS;
    snprintf(buf, sizeof(buf), "ratio(4fc/2fc)=%.3f (want < 0.5)", ratio2);
    report(ratio2 < 0.5f, "LP drops further octave above 2fc", buf);
}

// 6. BP rolloff: 6 dB/oct below fc (per Stinchcombe §7)
//    bp = x₁_mid (the BP node); transfer function has 6 dB/oct slope below fc.
//    Well below fc: amplitude doubles for each octave going up.
//    We test two frequencies an octave apart, both at fc/8 and fc/4.
static void test_hp_rolloff() {
    printf("\n6. BP rolloff: 6 dB/oct below fc\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    const float fc = 2000.f, amp = 1.f;
    const float f_lo = fc / 8.f;   // 250 Hz
    const float f_hi = fc / 4.f;   // 500 Hz  (one octave above f_lo)
    int settle  = (int)(44100.f / f_lo * 20.f);
    int measure = (int)(44100.f / f_lo * 20.f);

    auto at_lo = runSine(f, f_lo, amp, fc, 0.f, 44100.f, settle, measure);
    auto at_hi = runSine(f, f_hi, amp, fc, 0.f, 44100.f, settle, measure);

    // 6 dB/oct means amplitude doubles each octave going up → ratio ≈ 2.
    float ratio = at_hi.bpRMS / at_lo.bpRMS;
    char buf[64];
    snprintf(buf, sizeof(buf), "ratio(f_hi/f_lo)=%.3f (want ≈ 2.0, tol 15%%)", ratio);
    report(near(ratio, 2.f, 0.15f), "BP doubles in amplitude per octave (6 dB/oct)", buf);
}

// 7. Self-oscillation at res = 1.0
//    At res=1 the loop gain reaches the self-oscillation threshold (k=2,
//    per Stinchcombe eq. 5 and the loop-gain analysis at fc).
//    We kick with a single impulse then feed zeros and check the oscillation
//    persists (sustained RMS well above zero after initial transient).
static void test_self_oscillation() {
    printf("\n7. Self-oscillation at res = 1.0\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();

    const float fc = 1000.f;
    const int kickSamples   = 1;
    const int settleSamples = 4000;
    const int measureSamples = 1024;
    float buf[1024];

    for (int i = 0; i < kickSamples + settleSamples + measureSamples; i++) {
        float in = (i == 0) ? 1.f : 0.f;
        auto [lp, bp, hp] = f.process(in, fc, 1.f);
        int j = i - kickSamples - settleSamples;
        if (j >= 0) buf[j] = lp;
    }
    float rms = rmsRun(buf, measureSamples);
    char detail[64];
    snprintf(detail, sizeof(detail), "RMS after 4000 samples = %.4f (want > 0.05)", rms);
    report(rms > 0.05f, "LP sustains oscillation after impulse (res=1)", detail);
}

// 8. Stability just below oscillation threshold (res = 0.9)
//    With res=0.9 the loop gain is 0.9 × 0.5 × 2 = 0.9 < 1 at fc.
//    The impulse response must decay: RMS well after the impulse must
//    be much smaller than immediately after it.
static void test_stable_below_threshold() {
    printf("\n8. Stability just below threshold (res = 0.9)\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();

    const float fc = 1000.f;
    // Measure early RMS (first 64 samples after impulse) vs late (last 64 of 8192).
    float early[64], late[64];
    for (int i = 0; i < 8192; i++) {
        float in = (i == 0) ? 1.f : 0.f;
        auto [lp, bp, hp] = f.process(in, fc, 0.9f);
        if (i < 64)          early[i] = lp;
        if (i >= 8192 - 64)  late[i - (8192 - 64)] = lp;
    }
    float earlyRMS = rmsRun(early, 64);
    float lateRMS  = rmsRun(late,  64);
    char detail[64];
    snprintf(detail, sizeof(detail),
             "earlyRMS=%.4f, lateRMS=%.6f (want late << early)", earlyRMS, lateRMS);
    report(lateRMS < earlyRMS * 0.01f,
           "impulse decays to <1%% of initial within 8192 samples (res=0.9)", detail);
}

// 9. Numerical robustness: no NaN/Inf under adversarial inputs
//    Sweep cutoff, resonance, and input amplitude (including above clip threshold).
//    Nothing should produce a non-finite output.
static void test_no_nan_inf() {
    printf("\n9. Numerical robustness (no NaN / Inf)\n");
    MF20Filter f;
    f.setSampleRate(44100.f);

    bool ok = true;
    float out[2];
    for (float fc : {20.f, 100.f, 500.f, 1000.f, 5000.f, 15000.f, 20000.f}) {
        for (float res : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
            f.reset();
            for (int i = 0; i < 512 && ok; i++) {
                // Loud input: well above the diode clip threshold.
                float in = 3.f * std::sin(2.f * 3.14159f * 1000.f / 44100.f * (float)i);
                auto [lp, bp, hp] = f.process(in, fc, res);
                out[0] = lp; out[1] = hp;
                if (anyNonFinite(out, 2) || !std::isfinite(bp)) { ok = false; }
            }
        }
    }
    report(ok, "no NaN or Inf across (fc, res, loud-input) sweep");
}

// 10. Sample-rate independence of cutoff frequency
//     At both 44100 Hz and 48000 Hz, a sine at 500 Hz with fc=1000 Hz should
//     produce approximately the same LP amplitude.  The bilinear prewarping
//     (tan(π·fc/fs)) corrects for the different sampling periods, keeping the
//     −3 dB point at exactly fc regardless of sample rate.
//     Expected |H(j·500)| with fc=1000 (two poles): 1/(1+(0.5)²) = 1/1.25 = 0.8
static void test_samplerate_independence() {
    printf("\n10. Sample-rate independence of cutoff frequency\n");
    const float fc = 1000.f, testFreq = 500.f, amp = 1.f;
    float rms44 = 0.f, rms48 = 0.f;
    {
        MF20Filter f;
        f.setSampleRate(44100.f);
        int settle  = (int)(44100.f / testFreq * 100.f);
        int measure = (int)(44100.f / testFreq * 100.f);
        auto r = runSine(f, testFreq, amp, fc, 0.f, 44100.f, settle, measure);
        rms44 = r.lpRMS;
    }
    {
        MF20Filter f;
        f.setSampleRate(48000.f);
        int settle  = (int)(48000.f / testFreq * 100.f);
        int measure = (int)(48000.f / testFreq * 100.f);
        auto r = runSine(f, testFreq, amp, fc, 0.f, 48000.f, settle, measure);
        rms48 = r.lpRMS;
    }
    // Both should be ≈ amp/√2 × 0.8 (|H| at 500 Hz with fc=1000 Hz, two poles).
    float expected = amp / std::sqrt(2.f) * 0.8f;
    char detail[96];
    snprintf(detail, sizeof(detail),
             "rms44=%.4f, rms48=%.4f, expected≈%.4f", rms44, rms48, expected);
    report(near(rms44, expected, 0.05f), "44100 Hz LP RMS ≈ 0.8 × amp/√2", detail);
    report(near(rms48, expected, 0.05f), "48000 Hz LP RMS ≈ 0.8 × amp/√2", detail);
    report(near(rms44, rms48, 0.02f),   "amplitudes match within 2% between sample rates", detail);
}

// 11. HP stage blocks DC
//     Feed a constant 1.0 into the filter. After settling, the true HP output
//     (hp = in − 2·x1 + clip(k·x1) − x2) must converge to ~0 at DC.
static void test_hp_blocks_dc() {
    printf("\n11. HP stage blocks DC\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();

    const float fc = 500.f, sr = 44100.f;
    const int settle = (int)(10.f * sr / (3.14159f * fc)) + 10;

    float lastHp = 1.f;
    for (int i = 0; i < settle + 200; i++) {
        auto [lp, bp, hp] = f.process(1.f, fc, 0.f);
        if (i >= settle) lastHp = hp;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "hp=%.6f (want ≈ 0.0)", lastHp);
    report(std::fabs(lastHp) < 0.005f, "true HP → 0 at DC with res=0", buf);
}

// 12. HP stage 12 dB/oct rolloff
//     The true HP should pass signals well above fc and attenuate below.
//     RMS at 4×fc should be substantially larger than at fc/4.
static void test_hp_12dboct() {
    printf("\n12. HP stage 12 dB/oct rolloff\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    const float fc = 500.f, amp = 1.f;
    int settle  = (int)(44100.f / fc * 50.f);
    int measure = (int)(44100.f / fc * 50.f);

    auto at_low  = runSine(f, fc / 4.f, amp, fc, 0.f, 44100.f, settle, measure);
    auto at_high = runSine(f, fc * 4.f, amp, fc, 0.f, 44100.f, settle, measure);

    float ratio = at_high.hpRMS / (at_low.hpRMS + 1e-9f);
    char buf[64];
    snprintf(buf, sizeof(buf), "hpRMS(4fc)=%.4f, hpRMS(fc/4)=%.4f, ratio=%.2f (want>5)",
             at_high.hpRMS, at_low.hpRMS, ratio);
    report(ratio > 5.f, "HP at 4×fc is >5× RMS of HP at fc/4 (12 dB/oct rolloff)", buf);
}

// 13. Cascaded HP→LP midband pass
//     With HP fc = 200 Hz and LP fc = 2000 Hz, a midband tone at 500 Hz
//     should pass through the HP stage much better than tones near either cutoff.
//     We directly test the HP output of a single filter: hp passes high freqs.
//     Test: 500 Hz (midband) vs 50 Hz (below fc/4) — HP should be much larger.
static void test_hp_midband() {
    printf("\n13. HP passes midband, attenuates below cutoff\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    const float fc = 500.f, amp = 1.f;
    int settle  = (int)(44100.f / (fc / 10.f) * 20.f);
    int measure = (int)(44100.f / (fc / 10.f) * 20.f);

    auto at_mid  = runSine(f, fc * 2.f,  amp, fc, 0.f, 44100.f, settle, measure);
    auto at_low  = runSine(f, fc / 10.f, amp, fc, 0.f, 44100.f, settle, measure);

    float ratio = at_mid.hpRMS / (at_low.hpRMS + 1e-9f);
    char buf[64];
    snprintf(buf, sizeof(buf), "hp(2fc)=%.4f, hp(fc/10)=%.4f, ratio=%.2f (want>3)",
             at_mid.hpRMS, at_low.hpRMS, ratio);
    report(ratio > 3.f, "HP at 2×fc is >3× RMS of HP at fc/10", buf);
}

// 14. HP stage unity gain well above cutoff
//     A high-frequency tone far above fc should pass through the HP stage
//     with near-unity amplitude (true 12 dB/oct HP approaches 1 as f→∞).
static void test_hp_unity_above_fc() {
    printf("\n14. HP stage approaches unity gain well above fc\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    const float fc = 200.f, amp = 1.f;
    const float testFreq = fc * 20.f;  // 4000 Hz — well above fc
    int settle  = (int)(44100.f / testFreq * 100.f);
    int measure = (int)(44100.f / testFreq * 100.f);

    auto r = runSine(f, testFreq, amp, fc, 0.f, 44100.f, settle, measure);

    // Expected RMS ≈ amp/√2 (near-unity gain)
    float expectedRMS = amp / std::sqrt(2.f);
    char buf[64];
    snprintf(buf, sizeof(buf), "hpRMS=%.4f, expected≈%.4f", r.hpRMS, expectedRMS);
    report(near(r.hpRMS, expectedRMS, 0.15f), "HP RMS ≈ amp/√2 at 20×fc (near-unity gain)", buf);
}

// 15. Drive character affects saturation at loud inputs
//     With res=0.7 and a loud sine at fc, drive=4 clips the BP feedback harder
//     than drive=1, so LP RMS must be lower under drive=4.
static void test_drive_character() {
    printf("\n15. Drive character scaling\n");
    MF20Filter fLow, fHigh;
    fLow.setSampleRate(44100.f);
    fHigh.setSampleRate(44100.f);
    fLow.setDriveCharacter(1.f);
    fHigh.setDriveCharacter(4.f);

    const float fc = 1000.f, amp = 1.f, res = 0.7f;
    int settle  = (int)(44100.f / fc * 50.f);
    int measure = (int)(44100.f / fc * 50.f);

    auto rLow  = runSine(fLow,  fc, amp, fc, res, 44100.f, settle, measure);
    auto rHigh = runSine(fHigh, fc, amp, fc, res, 44100.f, settle, measure);

    char buf[96];
    snprintf(buf, sizeof(buf), "lpRMS(drive=1)=%.4f, lpRMS(drive=4)=%.4f (want drive4 < drive1)",
             rLow.lpRMS, rHigh.lpRMS);
    report(rHigh.lpRMS < rLow.lpRMS,
           "drive=4 produces lower LP RMS than drive=1 (harder saturation)", buf);
}

// 16. processVCV is a ×5 scaling wrapper around process()
//     processVCV(in×5, fc, res) == process(in, fc, res) × 5 sample-by-sample.
//     Internally, processVCV divides input by 5 (×0.2), calls process, then
//     multiplies outputs by 5.  Both paths get the same normalised input, so
//     states track and the output ratio is exactly 5.
static void test_processVCV_scaling() {
    printf("\n16. processVCV scaling (×5 in/out)\n");
    MF20Filter fA, fB;
    fA.setSampleRate(44100.f);
    fB.setSampleRate(44100.f);

    const float fc = 500.f, res = 0.5f;
    const float phaseInc = 2.f * 3.14159265f * 300.f / 44100.f;
    bool ok = true;

    for (int i = 0; i < 1000 && ok; i++) {
        float in = std::sin(phaseInc * (float)i);
        auto rA = fA.process(in, fc, res);
        auto rB = fB.processVCV(in * 5.f, fc, res);
        const float tol = 1e-4f;
        if (std::fabs(rB.lp - rA.lp * 5.f) > tol ||
            std::fabs(rB.bp - rA.bp * 5.f) > tol ||
            std::fabs(rB.hp - rA.hp * 5.f) > tol) {
            ok = false;
        }
    }
    report(ok, "processVCV(in×5) == process(in) × 5 for LP, BP, HP");
}

// 17. Instance independence — no shared/static state
//     Two filters: fA is excited with an impulse, fB always receives 0.
//     fB must produce exactly {0,0,0} throughout.
static void test_independent_instances() {
    printf("\n17. Independent instances (no shared state)\n");
    MF20Filter fA, fB;
    fA.setSampleRate(44100.f);
    fB.setSampleRate(44100.f);

    bool ok = true;
    for (int i = 0; i < 500 && ok; i++) {
        float inA = (i == 0) ? 1.f : 0.f;
        fA.process(inA, 1000.f, 0.5f);
        auto [lp, bp, hp] = fB.process(0.f, 1000.f, 0.5f);
        if (lp != 0.f || bp != 0.f || hp != 0.f) ok = false;
    }
    report(ok, "quiescent instance produces exactly {0,0,0} while other is excited");
}

// 18. BP bandpass peaks at fc
//     At res=0, BP RMS at the cutoff frequency must exceed BP at fc/4 and 4fc.
static void test_bp_peaks_at_fc() {
    printf("\n18. BP peaks at fc\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    const float fc = 1000.f, amp = 1.f, res = 0.f;
    int settle  = (int)(44100.f / (fc / 4.f) * 20.f);
    int measure = (int)(44100.f / (fc / 4.f) * 20.f);

    auto at_low  = runSine(f, fc / 4.f, amp, fc, res, 44100.f, settle, measure);
    auto at_fc   = runSine(f, fc,       amp, fc, res, 44100.f, settle, measure);
    auto at_high = runSine(f, fc * 4.f, amp, fc, res, 44100.f, settle, measure);

    char buf[96];
    snprintf(buf, sizeof(buf), "bp(fc/4)=%.4f, bp(fc)=%.4f, bp(4fc)=%.4f",
             at_low.bpRMS, at_fc.bpRMS, at_high.bpRMS);
    report(at_fc.bpRMS > at_low.bpRMS && at_fc.bpRMS > at_high.bpRMS,
           "BP RMS at fc > BP at fc/4 and BP at fc > BP at 4fc", buf);
}

// 19. Rapid cutoff sweep — no NaN/Inf
//     Cutoff swept linearly from 20 Hz to 20 kHz over 2000 samples.
static void test_rapid_cutoff_sweep() {
    printf("\n19. Rapid cutoff sweep (no NaN/Inf)\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();

    bool ok = true;
    const int N = 2000;
    for (int i = 0; i < N && ok; i++) {
        float t  = (float)i / (float)(N - 1);
        float fc = 20.f + t * (20000.f - 20.f);
        float in = std::sin(2.f * 3.14159265f * 1000.f / 44100.f * (float)i);
        auto [lp, bp, hp] = f.process(in, fc, 0.5f);
        if (!std::isfinite(lp) || !std::isfinite(bp) || !std::isfinite(hp)) ok = false;
    }
    report(ok, "no NaN/Inf while sweeping cutoff 20→20 kHz over 2000 samples");
}

// 20. Extreme resonance + loud input — filter stays alive
//     res=0.999, amplitude=5.  Run 2000 samples.
//     No NaN/Inf and RMS must be finite and > 0.
static void test_extreme_resonance_loud() {
    printf("\n20. Extreme resonance + loud input\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();

    float buf[2000];
    for (int i = 0; i < 2000; i++) {
        float in = 5.f * std::sin(2.f * 3.14159265f * 1000.f / 44100.f * (float)i);
        auto [lp, bp, hp] = f.process(in, 1000.f, 0.999f);
        buf[i] = lp;
        (void)bp; (void)hp;
    }
    bool noNan = !anyNonFinite(buf, 2000);
    float rms  = rmsRun(buf, 2000);
    char detail[64];
    snprintf(detail, sizeof(detail), "noNaN=%d, RMS=%.4f (want finite and > 0)",
             (int)noNan, rms);
    report(noNan && std::isfinite(rms) && rms > 0.f,
           "res=0.999 + loud input: no NaN/Inf, filter alive (RMS > 0)", detail);
}

// 21a. K35 output differs from OTA (proves implementation is distinct)
//      With res=0.5 and a sinusoidal input, K35 and OTA must produce
//      different LP outputs (different topology, different resonance scaling).
static void test_k35_differs_from_ota() {
    printf("\n21a. K35 output differs from OTA\n");
    MF20Filter fOTA, fK35;
    fOTA.setSampleRate(44100.f);
    fK35.setSampleRate(44100.f);
    fOTA.setMode(MF20Filter::Mode::OTA);
    fK35.setMode(MF20Filter::Mode::K35);

    bool anyDiff = false;
    for (int i = 0; i < 500; i++) {
        float in  = 0.5f * std::sin(2.f * 3.14159265f * 440.f / 44100.f * (float)i);
        auto rOTA = fOTA.process(in, 1000.f, 0.5f);
        auto rK35 = fK35.process(in, 1000.f, 0.5f);
        if (rOTA.lp != rK35.lp) { anyDiff = true; break; }
    }
    report(anyDiff, "K35 LP output differs from OTA LP output (distinct topology)");
}

// 21b. K35 self-oscillation at res = 1.0
//      K35's self-oscillation onset is at k = 8/3, mapped from res = 1.0.
//      Kick with an impulse, feed zeros, verify oscillation sustains.
static void test_k35_self_oscillation() {
    printf("\n21b. K35 self-oscillation at res = 1.0\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();
    f.setMode(MF20Filter::Mode::K35);

    const float fc = 1000.f;
    const int kickSamples    = 1;
    const int settleSamples  = 4000;
    const int measureSamples = 1024;
    float buf[1024];

    for (int i = 0; i < kickSamples + settleSamples + measureSamples; i++) {
        float in = (i == 0) ? 1.f : 0.f;
        auto [lp, bp, hp] = f.process(in, fc, 1.f);
        int j = i - kickSamples - settleSamples;
        if (j >= 0) buf[j] = lp;
    }
    float rms = rmsRun(buf, measureSamples);
    char detail[64];
    snprintf(detail, sizeof(detail), "RMS after 4000 samples = %.4f (want > 0.05)", rms);
    report(rms > 0.05f, "K35 LP sustains oscillation after impulse (res=1)", detail);
}

// K35 stability through the module's resonance taper.
// MF20FilterModule maps the knob through resTaper() (max 1.025), so the
// filter must stay bounded for res up to 1.025. fc=12000 exercises the
// D1<=0 (bistable) branch at k > 8/3.
static void test_k35_res_taper_stability() {
    printf("\n19. K35 bounded at res = resTaper(knob), knob up to 1.0\n");
    const float fs = 48000.f;
    bool allOk = true;
    char detail[128] = {0};
    for (float knob : {0.85f, 0.90f, 1.00f}) {
        for (float fc : {1000.f, 12000.f}) {
            MF20Filter f;
            f.setSampleRate(fs);
            f.setMode(MF20Filter::Mode::K35);
            f.setDriveCharacter(1.f);
            f.reset();
            const float res = resTaper(knob);
            float peak = 0.f;
            bool finite = true;
            // 0.5 s of 220 Hz sine at 0.2 amplitude, then 2 s of silence.
            for (int i = 0; i < (int)(2.5f * fs); i++) {
                float in = (i < (int)(0.5f * fs))
                    ? 0.2f * std::sin(2.f * 3.14159265f * 220.f * i / fs) : 0.f;
                auto [lp, bp, hp] = f.process(in, fc, res);
                if (!std::isfinite(lp) || !std::isfinite(bp) || !std::isfinite(hp)) {
                    finite = false; break;
                }
                float a = std::fabs(lp);
                if (a > peak) peak = a;
            }
            if (!finite || peak > 2.f) {
                allOk = false;
                snprintf(detail, sizeof(detail), "knob=%.2f fc=%.0f finite=%d peak=%g",
                         knob, fc, (int)finite, peak);
            }
        }
    }
    report(allOk, "K35 finite and |lp| < 2 for knob {0.85,0.9,1.0} × fc {1k,12k}", detail);
}

// K35 self-oscillation must survive (bounded) at the taper's maximum.
static void test_k35_taper_self_oscillation() {
    printf("\n20. K35 sustains bounded self-oscillation at res = resTaper(1.0)\n");
    const float fs = 48000.f;
    MF20Filter f;
    f.setSampleRate(fs);
    f.setMode(MF20Filter::Mode::K35);
    f.setDriveCharacter(1.f);
    f.reset();
    const float res = resTaper(1.f);   // 1.025 → k ≈ 2.733
    f.process(0.5f, 1000.f, res);      // kick
    bool finite = true;
    for (int i = 0; i < 48000 && finite; i++) {          // settle 1 s
        auto [lp, bp, hp] = f.process(0.f, 1000.f, res);
        (void)bp; (void)hp;
        finite = std::isfinite(lp);
    }
    static float buf[24000];
    for (int i = 0; i < 24000 && finite; i++) {          // measure 0.5 s
        auto [lp, bp, hp] = f.process(0.f, 1000.f, res);
        (void)bp; (void)hp;
        if (!std::isfinite(lp)) { finite = false; break; }
        buf[i] = lp;
    }
    float rms = finite ? rmsRun(buf, 24000) : 0.f;
    char detail[64];
    snprintf(detail, sizeof(detail), "finite=%d rms=%g", (int)finite, rms);
    report(finite && rms > 0.02f && rms < 2.f,
           "K35 at resTaper(1.0): finite, 0.02 < RMS < 2", detail);
}

// 21c. K35 clips forward path, OTA does not clip at res=0
//      At res=0 there is no resonance feedback in either mode.
//      K35 clips the INPUT → LP output is amplitude-limited.
//      OTA clips k·x₁ = 0·x₁ = 0, which is never above threshold → no clipping.
//      Feed a large DC input (5.0 >> clipThreshold=1.0) and compare settled LP.
//      K35 LP → clip(5.0) ≈ 2.0;  OTA LP → 5.0 (no clip at res=0).
static void test_k35_forward_clip_isolation() {
    printf("\n21c. K35 clips forward path; OTA does not clip at res=0\n");
    MF20Filter fOTA, fK35;
    fOTA.setSampleRate(44100.f);
    fK35.setSampleRate(44100.f);
    fOTA.setMode(MF20Filter::Mode::OTA);
    fK35.setMode(MF20Filter::Mode::K35);

    // Settle for ~10 time constants at fc=100 Hz.
    const float fc = 100.f, sr = 44100.f;
    const int settle = (int)(10.f * sr / (3.14159f * fc)) + 10;

    float lastLpOTA = 0.f, lastLpK35 = 0.f;
    for (int i = 0; i < settle + 200; i++) {
        auto rOTA = fOTA.process(5.f, fc, 0.f);
        auto rK35 = fK35.process(5.f, fc, 0.f);
        if (i >= settle) { lastLpOTA = rOTA.lp; lastLpK35 = rK35.lp; }
    }
    // K35 LP must be limited below the raw input level.
    bool k35Clipped = lastLpK35 < 3.f;
    // OTA LP must converge near the raw input (no clipping at res=0).
    bool otaNotClipped = lastLpOTA > 3.f;

    char buf[96];
    snprintf(buf, sizeof(buf), "lpOTA=%.3f (want>3), lpK35=%.3f (want<3)", lastLpOTA, lastLpK35);
    report(k35Clipped && otaNotClipped,
           "K35 LP < 3.0 (forward clip active); OTA LP > 3.0 (no clip at res=0)", buf);
}

// 22. Mode switch mid-stream — no NaN/Inf
//     Start in OTA mode, run 100 samples, switch to K35, run 100 more.
static void test_mode_switch_mid_stream() {
    printf("\n22. Mode switch mid-stream (no NaN/Inf)\n");
    MF20Filter f;
    f.setSampleRate(44100.f);
    f.reset();
    f.setMode(MF20Filter::Mode::OTA);

    bool ok = true;
    for (int i = 0; i < 200 && ok; i++) {
        if (i == 100) f.setMode(MF20Filter::Mode::K35);
        float in = 0.5f * std::sin(2.f * 3.14159265f * 440.f / 44100.f * (float)i);
        auto [lp, bp, hp] = f.process(in, 1000.f, 0.5f);
        if (!std::isfinite(lp) || !std::isfinite(bp) || !std::isfinite(hp)) ok = false;
    }
    report(ok, "no NaN/Inf when switching OTA→K35 at sample 100");
}

// 23. K35 asymmetric clipping: positive half clips less hard than negative
//     At Drive=4, T_pos = clipThreshold, T_neg = clipThreshold * (1 - 0.15).
//     The positive output peak should exceed the negative peak by at least 3%.
static void test_k35_asymmetric_clip() {
    printf("\n23. K35 asymmetric clip (positive peak > negative peak)\n");

    // 23a: K35 asymmetric — positive peak > |negative peak| by ≥3%
    {
        MF20Filter f;
        f.setSampleRate(44100.f);
        f.setMode(MF20Filter::Mode::K35);
        f.setDriveCharacter(4.f);

        float peak_pos = 0.f, peak_neg = 0.f;
        for (int i = 0; i < 500; i++) {
            float in = 0.8f * std::sin(2.f * 3.14159f * 440.f * i / 44100.f);
            auto out = f.process(in, 1000.f, 0.5f);
            if (out.lp > peak_pos) peak_pos = out.lp;
            if (out.lp < peak_neg) peak_neg = out.lp;
        }
        peak_neg = -peak_neg;  // make positive for comparison
        char buf[96];
        snprintf(buf, sizeof(buf), "peak_pos=%.5f, peak_neg=%.5f, ratio=%.4f (want>1.03)",
                 peak_pos, peak_neg, peak_pos / (peak_neg + 1e-9f));
        report(peak_pos > peak_neg * 1.03f,
               "K35 asymmetric clip: positive peak > negative peak by >=3%", buf);
    }

    // 23b: OTA clip remains symmetric
    {
        MF20Filter f;
        f.setSampleRate(44100.f);
        f.setMode(MF20Filter::Mode::OTA);
        f.setDriveCharacter(4.f);

        float peak_pos = 0.f, peak_neg = 0.f;
        for (int i = 0; i < 500; i++) {
            float in = 0.8f * std::sin(2.f * 3.14159f * 440.f * i / 44100.f);
            auto out = f.process(in, 1000.f, 0.5f);
            if (out.lp > peak_pos) peak_pos = out.lp;
            if (out.lp < peak_neg) peak_neg = out.lp;
        }
        peak_neg = -peak_neg;
        float asymm = std::fabs(peak_pos - peak_neg) / (peak_pos + 1e-9f);
        char buf[96];
        snprintf(buf, sizeof(buf), "peak_pos=%.5f, peak_neg=%.5f, asymm=%.4f (want<0.01)",
                 peak_pos, peak_neg, asymm);
        report(asymm < 0.01f,
               "OTA clip symmetric: positive and negative peaks within 1%", buf);
    }
}

static void test_nan_recovery() {
    printf("\nNaN recovery (stateFinite + reset)\n");
    for (auto mode : {MF20Filter::Mode::OTA, MF20Filter::Mode::K35}) {
        const char* mn = (mode == MF20Filter::Mode::OTA) ? "OTA" : "K35";
        MF20Filter f;
        f.setSampleRate(48000.f);
        f.setMode(mode);
        for (int i = 0; i < 200; ++i) f.process(0.5f, 1000.f, 0.5f);
        char b1[64]; snprintf(b1, sizeof(b1), "%s: finite after normal use", mn);
        report(f.stateFinite(), b1);
        f.process(NAN, 1000.f, 0.5f);
        char b2[64]; snprintf(b2, sizeof(b2), "%s: non-finite after NaN input", mn);
        report(!f.stateFinite(), b2);
        f.reset();
        bool ok = f.stateFinite();
        for (int i = 0; i < 200; ++i) {
            auto [lp, bp, hp] = f.process(0.5f, 1000.f, 0.5f);
            ok = ok && std::isfinite(lp) && std::isfinite(bp) && std::isfinite(hp);
        }
        char b3[64]; snprintf(b3, sizeof(b3), "%s: finite output after reset", mn);
        report(ok, b3);
    }
}

static void test_voice_sanitize() {
    printf("\nVoiceEngine::sanitize recovers poisoned voice\n");
    VoiceEngine v;
    v.setSampleRate(48000.f);
    v.hpFilter.process(NAN, 1000.f, 0.5f);      // poison one filter
    v.sanitize();
    bool ok = v.hpFilter.stateFinite();
    for (int i = 0; i < 200; ++i) {
        auto [lp, bp, hp] = v.hpFilter.process(0.5f, 1000.f, 0.5f);
        ok = ok && std::isfinite(lp);
        (void)bp; (void)hp;
    }
    report(ok, "sanitize() resets non-finite filter state");
}

// EnginePool::setVoices(n) with n > activeVoices must reset the engines
// newly entering [activeVoices, n) — a voice that rang hard while inactive
// (e.g. left self-oscillating at res=1 from a previous polyphony setting)
// must not re-emit its stale state once it becomes active again.
static void test_engine_pool_reset_on_growth() {
    printf("\nEnginePool::setVoices resets voices (re)entering the active range\n");
    EnginePool pool;
    pool.setSampleRate(44100.f);
    pool.setVoices(1);   // baseline: only voice 0 active

    // Ring voice index 1 hard: impulse + res=1 self-oscillation (OTA, hot input),
    // exactly as test_self_oscillation() establishes sustains after 4000 samples.
    const float fc = 1000.f;
    for (int i = 0; i < 5000; i++) {
        float in = (i == 0) ? 1.f : 0.f;
        pool.engines[1].lpFilter.process(in, fc, 1.f);
    }
    bool ringing = std::fabs(pool.engines[1].lpFilter.process(0.f, fc, 1.f).lp) > 0.05f;
    report(ringing, "voice 1 is ringing before growth (sanity check)");

    pool.setVoices(3);   // growth 1 -> 3: engines[1], engines[2] must be reset

    bool clean = true;
    float firstOut[2] = {0.f, 0.f};
    for (int idx = 1; idx <= 2; idx++) {
        float out = pool.engines[idx].lpFilter.process(0.f, fc, 1.f).lp;
        firstOut[idx - 1] = out;
        clean = clean && std::fabs(out) < 1e-6f;
    }
    char detail[96];
    snprintf(detail, sizeof(detail), "voice1=%.6f voice2=%.6f (want ~0)", firstOut[0], firstOut[1]);
    report(clean, "voices entering active range start clean (not ringing)", detail);

    // No-op / shrink must not touch any engine state (loop range is empty).
    EnginePool pool2;
    pool2.setSampleRate(44100.f);
    pool2.setVoices(3);
    for (int i = 0; i < 5000; i++) {
        float in = (i == 0) ? 1.f : 0.f;
        pool2.engines[2].lpFilter.process(in, fc, 1.f);
    }
    float beforeNoop = pool2.engines[2].lpFilter.process(0.f, fc, 1.f).lp;
    pool2.setVoices(3);   // no growth (n <= activeVoices): must be a no-op
    float afterNoop = pool2.engines[2].lpFilter.process(0.f, fc, 1.f).lp;
    char detail2[96];
    snprintf(detail2, sizeof(detail2), "before=%.6f after=%.6f", beforeNoop, afterNoop);
    report(std::fabs(beforeNoop) > 0.05f && std::fabs(afterNoop) > 0.05f,
           "setVoices(n) with n <= activeVoices does not reset any engine", detail2);
}

// setDriveCharacterFromThreshold(1/√d) must produce output identical to
// setDriveCharacter(d) — the smoothed-drive path (module) stores a
// precomputed threshold instead of recomputing sqrt/divide per sample, but
// the filter-level math must be exactly the same.
static void test_drive_from_threshold_matches_setDriveCharacter() {
    printf("\nsetDriveCharacterFromThreshold(1/sqrt(d)) == setDriveCharacter(d)\n");
    for (float d : {1.f, 2.f, 4.f, 8.f}) {
        MF20Filter fA, fB;
        fA.setSampleRate(44100.f);
        fB.setSampleRate(44100.f);
        fA.setDriveCharacter(d);
        fB.setDriveCharacterFromThreshold(1.f / std::sqrt(d));

        bool ok = true;
        for (int i = 0; i < 200 && ok; i++) {
            float in = 0.8f * std::sin(2.f * 3.14159265f * 440.f * i / 44100.f);
            auto ra = fA.process(in, 1000.f, 0.5f);
            auto rb = fB.process(in, 1000.f, 0.5f);
            ok = ok && ra.lp == rb.lp && ra.bp == rb.bp && ra.hp == rb.hp;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "drive=%.0f", d);
        report(ok, "setDriveCharacterFromThreshold matches setDriveCharacter", buf);
    }
}

static void test_processG_matches_process() {
    printf("\nprocessG(cutoffToG(fc)) == process(fc)\n");
    for (auto mode : {MF20Filter::Mode::OTA, MF20Filter::Mode::K35}) {
        MF20Filter a, b;
        a.setSampleRate(48000.f); b.setSampleRate(48000.f);
        a.setMode(mode); b.setMode(mode);
        bool ok = true;
        for (int i = 0; i < 500; ++i) {
            float in = std::sin(2.f * float(M_PI) * 220.f * i / 48000.f);
            float fc = 200.f + 30.f * i;
            auto ra = a.process(in, fc, 0.6f);
            auto rb = b.processG(in, MF20Filter::cutoffToG(fc, 48000.f), 0.6f);
            ok = ok && ra.lp == rb.lp && ra.bp == rb.bp && ra.hp == rb.hp;
        }
        report(ok, mode == MF20Filter::Mode::OTA ? "OTA identical" : "K35 identical");
    }
}

// K35 forward clip, extracted as a static helper (Q6 groundwork).
// Full-range equivalence against the previous inline piecewise expression.
static void test_k35_forward_clip_helper_equivalence() {
    printf("\n24. k35ForwardClip helper matches the inline piecewise clip\n");
    auto oldClip = [](float in_n) -> float {
        constexpr float kSatSlope = 0.25f;
        if (in_n >= 0.f)
            return (in_n <= 1.f) ? in_n : kSatSlope * in_n + (1.f - kSatSlope) * 1.f;
        constexpr float T_neg_n = 0.85f;   // 1 - kK35Asymmetry
        return (in_n >= -T_neg_n) ? in_n : kSatSlope * in_n - (1.f - kSatSlope) * T_neg_n;
    };
    bool ok = true;
    float maxErr = 0.f;
    for (int i = -30000; i <= 30000; ++i) {           // x in [-3, 3], step 1e-4
        float x = i * 1e-4f;
        float err = std::fabs(MF20Filter::k35ForwardClip(x) - oldClip(x));
        if (err > maxErr) maxErr = err;
        if (err > 1e-6f) ok = false;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "maxErr=%.3g", maxErr);
    report(ok, "k35ForwardClip == previous inline clip over [-3, 3]", buf);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("MF20Filter test suite\n");
    printf("=======================\n");

    test_zero_io();
    test_reset();
    test_dc_passthrough();
    test_lp_at_fc();
    test_lp_rolloff();
    test_hp_rolloff();
    test_self_oscillation();
    test_stable_below_threshold();
    test_no_nan_inf();
    test_samplerate_independence();
    test_hp_blocks_dc();
    test_hp_12dboct();
    test_hp_midband();
    test_hp_unity_above_fc();
    test_drive_character();
    test_processVCV_scaling();
    test_independent_instances();
    test_bp_peaks_at_fc();
    test_rapid_cutoff_sweep();
    test_extreme_resonance_loud();
    test_k35_differs_from_ota();
    test_k35_self_oscillation();
    test_k35_res_taper_stability();
    test_k35_taper_self_oscillation();
    test_k35_forward_clip_isolation();
    test_mode_switch_mid_stream();
    test_k35_asymmetric_clip();
    test_k35_forward_clip_helper_equivalence();
    test_nan_recovery();
    test_voice_sanitize();
    test_engine_pool_reset_on_growth();
    test_drive_from_threshold_matches_setDriveCharacter();
    test_processG_matches_process();

    printf("\n=======================\n");
    printf("%d passed, %d failed\n", sPassed, sFailed);
    return sFailed > 0 ? 1 : 0;
}
