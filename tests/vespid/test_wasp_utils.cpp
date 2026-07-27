/**
 * test_wasp_utils.cpp — Test suite for src/vespid/wasp_dsp_utils.hpp
 *
 * Compile & run (or via tests/run.sh):
 *   g++ -std=c++20 -O2 -I../src -o test_wasp_utils test_wasp_utils.cpp && ./test_wasp_utils
 */

#include "../../src/vespid/wasp_dsp_utils.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>

// ── Reference halfband implementations (Task 5b) ───────────────────────────
// Verbatim copies of the pre-optimization src/vespid/wasp_dsp_utils.hpp
// HalfbandUp/HalfbandDown (plain O(kHbN) shift-register, no sparsity
// exploited). Kept here ONLY as an equivalence oracle for the optimized
// ring-buffer versions in wasp_dsp_utils.hpp — never touched again once the
// rewrite is verified sample-for-sample within 1e-6 against this (small
// nonzero diffs, ~1e-7, are expected float32 reassociation from the
// different summation order).
struct ReferenceHalfbandUp {
    float hist[wasp::kHbN] = {};
    void reset() { for (float& h : hist) h = 0.f; }
    void process(float in, float* out2) {
        for (int i = wasp::kHbN - 1; i > 0; --i) hist[i] = hist[i - 1];
        hist[0] = in;
        float acc0 = 0.f;
        for (int i = 0; i < wasp::kHbN; ++i) acc0 += wasp::kHalfbandTaps[i] * hist[i];
        out2[0] = 2.f * acc0;

        for (int i = wasp::kHbN - 1; i > 0; --i) hist[i] = hist[i - 1];
        hist[0] = 0.f;
        float acc1 = 0.f;
        for (int i = 0; i < wasp::kHbN; ++i) acc1 += wasp::kHalfbandTaps[i] * hist[i];
        out2[1] = 2.f * acc1;
    }
};
struct ReferenceHalfbandDown {
    float hist[wasp::kHbN] = {};
    void reset() { for (float& h : hist) h = 0.f; }
    float process(float in0, float in1) {
        for (int i = wasp::kHbN - 1; i > 0; --i) hist[i] = hist[i - 1];
        hist[0] = in0;
        for (int i = wasp::kHbN - 1; i > 0; --i) hist[i] = hist[i - 1];
        hist[0] = in1;
        float acc = 0.f;
        for (int i = 0; i < wasp::kHbN; ++i) acc += wasp::kHalfbandTaps[i] * hist[i];
        return acc;
    }
};

// ── Test harness (copied pattern from tests/mf20/test_module_dsp.cpp) ──────

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

// Like report(), but prints the detail line on PASS as well — used by the
// halfband equivalence checks so the measured maxDiff evidence is
// reproducible from this shipped harness, not just visible on failure.
static void reportWithDetail(bool ok, const char* name, const char* detail) {
    if (ok) {
        printf("  PASS  %s  (%s)\n", name, detail);
        ++sPassed;
    } else {
        printf("  FAIL  %s  (%s)\n", name, detail);
        ++sFailed;
    }
}

static bool near(float actual, float expected, float tol = 0.01f) {
    if (expected == 0.f) return std::fabs(actual) < tol;
    return std::fabs(actual - expected) / std::fabs(expected) < tol;
}

// ── tanhApprox ──────────────────────────────────────────────────────────────

static void test_tanh_approx() {
    // NOTE ON TOLERANCE: the brief's Step 1 asks for < 0.01 here, but the
    // brief's own verbatim formula x*(27+x^2)/(27+9x^2) has a *measured*
    // max abs error of 0.0235 near x=1.57 (confirmed numerically: at x=1,
    // approx=0.777778 vs tanh=0.761594, diff=0.016184 > 0.01). This is an
    // inherent property of that exact rational approximant, not a bug in
    // this implementation, so the tolerance below is loosened to 0.03 to
    // match verified reality while still catching gross regressions. See
    // task-2-report.md for the full discrepancy writeup.
    printf("\n1. tanhApprox tracks std::tanh within 0.03 (see tolerance note)\n");
    const float xs[] = { -6.f, -3.f, -1.f, -0.1f, 0.f, 0.1f, 1.f, 3.f, 6.f };
    for (float x : xs) {
        float got = wasp::tanhApprox(x);
        float want = std::tanh(x);
        char buf[96];
        snprintf(buf, sizeof(buf), "x=%.2f got=%.6f want=%.6f", x, got, want);
        report(std::fabs(got - want) < 0.03f, "tanhApprox(x) within 0.03 of std::tanh(x)", buf);
    }
}

// ── tanhApproxFast / tanhXdXFast (divide-free solver-path variants) ─────────
// The polynomial must track the rational forms within its documented 7.3e-4
// fit error everywhere on |x| <= 3, and agree exactly with the clamp/1-over
// branches beyond. (Monotonicity is deliberately NOT asserted here — the
// poly wiggles near the flat top, which is why railClamp keeps the rational.)
static void test_tanh_fast_variants() {
    printf("\n1b. tanhApproxFast/tanhXdXFast track the rational forms\n");
    double maxA = 0, maxX = 0;
    bool clampOk = true;
    for (int i = -4000; i <= 4000; ++i) {
        float x = (float)i * 1e-3f;   // -4..4
        maxA = std::max(maxA, (double)std::fabs(wasp::tanhApproxFast(x) - wasp::tanhApprox(x)));
        maxX = std::max(maxX, (double)std::fabs(wasp::tanhXdXFast(x) - wasp::tanhXdX(x)));
        if (std::fabs(x) > 3.f &&
            (wasp::tanhApproxFast(x) != wasp::tanhApprox(x) ||
             wasp::tanhXdXFast(x) != wasp::tanhXdX(x))) clampOk = false;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "max |diff| tanhApprox=%.2e tanhXdX=%.2e (want < 2.5e-3)", maxA, maxX);
    report(maxA < 2.5e-3 && maxX < 2.5e-3, "Fast variants within fit error of rationals", buf);
    report(clampOk, "Fast variants match exactly beyond |x|=3");
}

// ── tanhXdX ─────────────────────────────────────────────────────────────────

static void test_tanh_x_dx() {
    printf("\n2. tanhXdX behavior\n");

    float at0 = wasp::tanhXdX(0.f);
    char buf0[64];
    snprintf(buf0, sizeof(buf0), "tanhXdX(0)=%.6f (want 1.0, no NaN)", at0);
    report(!std::isnan(at0) && at0 == 1.f, "tanhXdX(0) == 1 exactly, no NaN", buf0);

    // NOTE: brief asks for within 1e-3; measured diff at the exact x=3
    // handoff is 0.001648 (tanhXdX(3)=1/3 exactly by construction, since
    // tanhApprox(3)=1.0 exactly, vs true tanh(3)/3=0.331685). Same inherent
    // approximant property as test 1 above; loosened to 2e-3.
    float at3 = wasp::tanhXdX(3.f);
    float want3 = std::tanh(3.f) / 3.f;
    char buf3[96];
    snprintf(buf3, sizeof(buf3), "tanhXdX(3)=%.6f want=%.6f", at3, want3);
    report(std::fabs(at3 - want3) < 2e-3f, "tanhXdX(3) ≈ tanh(3)/3 within 2e-3", buf3);

    // Continuity at the |x|=3 handoff.
    float justBelow = wasp::tanhXdX(2.999f);
    float justAbove = wasp::tanhXdX(3.001f);
    char bufC[96];
    snprintf(bufC, sizeof(bufC), "f(2.999)=%.6f f(3.001)=%.6f", justBelow, justAbove);
    report(std::fabs(justBelow - justAbove) < 1e-3f, "tanhXdX continuous at |x|=3 handoff", bufC);

    // Negative side mirrors positive (even function).
    float atNeg3 = wasp::tanhXdX(-3.f);
    char bufN[64];
    snprintf(bufN, sizeof(bufN), "tanhXdX(-3)=%.6f tanhXdX(3)=%.6f", atNeg3, at3);
    report(near(atNeg3, at3, 1e-4f), "tanhXdX is even: f(-3) == f(3)", bufN);
}

// ── computeH1 ─────────────────────────────────────────────────────────────

static void test_compute_h1() {
    printf("\n3. computeH1 DC gain vs h1_check.py\n");
    const float fsInt = 192000.f;

    struct Case { float rho; float wantDc; };
    Case cases[] = {
        { 0.5f, 0.5151f },   // H1(0)=0.5151 per h1_check.py
        { 0.0f, 0.964f },
        { 1.0f, 0.356f },
    };
    for (auto& c : cases) {
        wasp::H1Coeffs h = wasp::computeH1(c.rho, fsInt);
        float dc = (h.beta0 + h.beta1) / (1.f + h.alpha1);
        char buf[128];
        snprintf(buf, sizeof(buf), "rho=%.2f dc=%.6f want=%.6f", c.rho, dc, c.wantDc);
        report(near(dc, c.wantDc, 0.02f), "computeH1 DC gain within 2% of h1_check.py value", buf);
    }
}

// ── DcBlocker ───────────────────────────────────────────────────────────────

static void test_dc_blocker() {
    printf("\n4. DcBlocker\n");
    const float fs = 48000.f;

    // DC input of 1.0 for 1 s should settle near 0.
    {
        wasp::DcBlocker db;
        db.setSampleRate(fs);
        db.reset();
        float out = 0.f;
        int n = (int)fs;
        for (int i = 0; i < n; ++i) out = db.process(1.f);
        char buf[64];
        snprintf(buf, sizeof(buf), "out after 1s of DC=1.0: %.6f (want < 0.05)", out);
        report(std::fabs(out) < 0.05f, "DcBlocker rejects DC within 1s to < 0.05", buf);
    }

    // 100 Hz sine should pass with little attenuation (within 0.5 dB).
    {
        wasp::DcBlocker db;
        db.setSampleRate(fs);
        db.reset();
        const float freq = 100.f;
        const float kPi = 3.14159265358979f;
        int n = (int)fs; // 1 s, several periods of settle + measure
        float sumInSq = 0.f, sumOutSq = 0.f;
        int measureStart = n / 2; // measure steady state, skip transient
        int measureCount = 0;
        for (int i = 0; i < n; ++i) {
            float in = std::sin(2.f * kPi * freq * (float)i / fs);
            float out = db.process(in);
            if (i >= measureStart) {
                sumInSq += in * in;
                sumOutSq += out * out;
                ++measureCount;
            }
        }
        float rmsIn = std::sqrt(sumInSq / measureCount);
        float rmsOut = std::sqrt(sumOutSq / measureCount);
        float dB = 20.f * std::log10(rmsOut / rmsIn);
        char buf[96];
        snprintf(buf, sizeof(buf), "rmsIn=%.6f rmsOut=%.6f dB=%.4f (want within 0.5 dB of 0)", rmsIn, rmsOut, dB);
        report(std::fabs(dB) < 0.5f, "DcBlocker passes 100 Hz sine within 0.5 dB", buf);
    }
}

// ── Halfband up/down round trip ──────────────────────────────────────────

static void test_halfband_roundtrip() {
    printf("\n5. Halfband up/downsample round trip\n");
    const float fs = 48000.f;
    const float freq = 1000.f;
    const float kPi = 3.14159265358979f;

    wasp::HalfbandUp up;
    wasp::HalfbandDown down;

    int n = (int)fs; // 1 s
    float sumInSq = 0.f, sumOutSq = 0.f;
    int measureStart = n / 4;
    int measureCount = 0;
    bool anyNaN = false;

    for (int i = 0; i < n; ++i) {
        float in = std::sin(2.f * kPi * freq * (float)i / fs);
        float up2[2];
        up.process(in, up2);
        if (std::isnan(up2[0]) || std::isnan(up2[1])) anyNaN = true;
        float out = down.process(up2[0], up2[1]);
        if (std::isnan(out)) anyNaN = true;

        if (i >= measureStart) {
            sumInSq += in * in;
            sumOutSq += out * out;
            ++measureCount;
        }
    }

    report(!anyNaN, "Halfband up/down round trip produces no NaN");

    float rmsIn = std::sqrt(sumInSq / measureCount);
    float rmsOut = std::sqrt(sumOutSq / measureCount);
    float dB = 20.f * std::log10(rmsOut / rmsIn);
    char buf[96];
    snprintf(buf, sizeof(buf), "rmsIn=%.6f rmsOut=%.6f dB=%.4f (want within 0.3 dB of 0)", rmsIn, rmsOut, dB);
    report(std::fabs(dB) < 0.3f, "Halfband round trip RMS within 0.3 dB", buf);
}

// ── Inner (15-tap) halfband: structure, response, round trip ────────────────
// The 4x cascade's inner stages use kHalfbandTapsInner (2026-07-26 CPU pass).
// Verifies the halfband structure the templates rely on, the design targets
// (passband ripple, >= 70 dB stopband above 0.375*fs — the only region whose
// aliases/images can reach the audio band through the outer stage), and a
// round trip through the Inner pair.
static void test_inner_halfband() {
    printf("\n5b. Inner 15-tap halfband\n");
    const float kPi = 3.14159265358979f;
    const int N = wasp::kHbInnerN;
    const float* t = wasp::kHalfbandTapsInner;

    bool structureOk = t[N / 2] == 0.5f;
    for (int i = 0; i < N; ++i) {
        int d = i - N / 2;
        if (d != 0 && d % 2 == 0 && t[i] != 0.f) structureOk = false;
        if (t[i] != t[N - 1 - i]) structureOk = false;   // linear phase
    }
    report(structureOk, "inner taps have exact halfband structure");

    // Direct DFT of the taps: |H| over the passband and stopband.
    double maxPbErr = 0.0, maxSb = 0.0;
    for (int k = 0; k <= 512; ++k) {
        double f = 0.5 * k / 512.0;              // normalized 0..0.5
        double re = 0, im = 0;
        for (int i = 0; i < N; ++i) {
            re += t[i] * std::cos(2.0 * kPi * f * i);
            im -= t[i] * std::sin(2.0 * kPi * f * i);
        }
        double mag = std::sqrt(re * re + im * im);
        if (f <= 0.125) maxPbErr = std::max(maxPbErr, std::fabs(mag - 1.0));
        if (f >= 0.375) maxSb = std::max(maxSb, mag);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "passband ripple %.4f dB (want < 0.01)",
             20.0 * std::log10(1.0 + maxPbErr));
    report(20.0 * std::log10(1.0 + maxPbErr) < 0.01, "inner passband flat", buf);
    snprintf(buf, sizeof(buf), "stopband max %.1f dB (want <= -70)",
             20.0 * std::log10(maxSb));
    report(20.0 * std::log10(maxSb) <= -70.0, "inner stopband >= 70 dB above 0.375*fs", buf);

    // Round trip through the Inner pair (mirrors test 5 for the outer pair).
    wasp::HalfbandUpInner up;
    wasp::HalfbandDownInner down;
    const float fs = 96000.f, freq = 2000.f;
    int n = (int)fs;
    float sumInSq = 0.f, sumOutSq = 0.f;
    int measureStart = n / 4, measureCount = 0;
    bool anyNaN = false;
    for (int i = 0; i < n; ++i) {
        float in = std::sin(2.f * kPi * freq * (float)i / fs);
        float up2[2];
        up.process(in, up2);
        float out = down.process(up2[0], up2[1]);
        if (std::isnan(up2[0]) || std::isnan(up2[1]) || std::isnan(out)) anyNaN = true;
        if (i >= measureStart) { sumInSq += in * in; sumOutSq += out * out; ++measureCount; }
    }
    report(!anyNaN, "inner round trip produces no NaN");
    float dB = 20.f * std::log10(std::sqrt(sumOutSq / measureCount)
                               / std::sqrt(sumInSq / measureCount));
    snprintf(buf, sizeof(buf), "round trip %.4f dB (want within 0.3 dB of 0)", dB);
    report(std::fabs(dB) < 0.3f, "inner round trip RMS within 0.3 dB", buf);
}

// ── Halfband optimized-vs-reference equivalence (Task 5b) ──────────────────
// Drives wasp::HalfbandUp/Down side-by-side with ReferenceHalfbandUp/Down
// (the pre-optimization shift-register implementation, copied verbatim
// above) over noise+sine input, checking sample-for-sample agreement within
// 1e-6 absolute. This is the safety net for the ring-buffer/sparse-tap
// rewrite: same 47 taps, same semantics, only the bookkeeping changed.
static void test_halfband_matches_reference() {
    printf("\n7. Halfband up/down: optimized matches reference implementation\n");
    const float fs = 48000.f;
    const float freq = 1000.f;
    const float kPi = 3.14159265358979f;
    const int n = 4000;
    const float tol = 1e-6f;

    srand(12345);

    // -- Up path --
    {
        wasp::HalfbandUp up;
        ReferenceHalfbandUp refUp;
        float maxDiff = 0.f;
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            float noise = ((float)rand() / (float)RAND_MAX) * 2.f - 1.f;
            float in = std::sin(2.f * kPi * freq * (float)i / fs) * 0.5f + noise * 0.3f;
            float got[2], want[2];
            up.process(in, got);
            refUp.process(in, want);
            for (int k = 0; k < 2; ++k) {
                float d = std::fabs(got[k] - want[k]);
                if (d > maxDiff) maxDiff = d;
                if (d > tol) ok = false;
            }
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "n=%d maxDiff=%.3e (want < %.0e)", n, maxDiff, tol);
        reportWithDetail(ok, "HalfbandUp matches ReferenceHalfbandUp within 1e-6", buf);
    }

    // -- Up path, immediately after reset() (mid-stream) --
    {
        wasp::HalfbandUp up;
        ReferenceHalfbandUp refUp;
        // Warm both up identically first, then reset both and re-check.
        for (int i = 0; i < 500; ++i) {
            float in = std::sin(2.f * kPi * freq * (float)i / fs);
            float o[2];
            up.process(in, o);
            refUp.process(in, o);
        }
        up.reset();
        refUp.reset();
        float maxDiff = 0.f;
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            float noise = ((float)rand() / (float)RAND_MAX) * 2.f - 1.f;
            float in = std::cos(2.f * kPi * freq * (float)i / fs) * 0.7f + noise * 0.2f;
            float got[2], want[2];
            up.process(in, got);
            refUp.process(in, want);
            for (int k = 0; k < 2; ++k) {
                float d = std::fabs(got[k] - want[k]);
                if (d > maxDiff) maxDiff = d;
                if (d > tol) ok = false;
            }
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "post-reset n=%d maxDiff=%.3e (want < %.0e)", n, maxDiff, tol);
        reportWithDetail(ok, "HalfbandUp matches reference immediately after reset()", buf);
    }

    // -- Down path --
    {
        wasp::HalfbandDown down;
        ReferenceHalfbandDown refDown;
        float maxDiff = 0.f;
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            float noise0 = ((float)rand() / (float)RAND_MAX) * 2.f - 1.f;
            float noise1 = ((float)rand() / (float)RAND_MAX) * 2.f - 1.f;
            float in0 = std::sin(2.f * kPi * freq * (float)(2*i)   / fs) * 0.5f + noise0 * 0.3f;
            float in1 = std::sin(2.f * kPi * freq * (float)(2*i+1) / fs) * 0.5f + noise1 * 0.3f;
            float got = down.process(in0, in1);
            float want = refDown.process(in0, in1);
            float d = std::fabs(got - want);
            if (d > maxDiff) maxDiff = d;
            if (d > tol) ok = false;
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "n=%d maxDiff=%.3e (want < %.0e)", n, maxDiff, tol);
        reportWithDetail(ok, "HalfbandDown matches ReferenceHalfbandDown within 1e-6", buf);
    }

    // -- Down path, immediately after reset() (mid-stream) --
    {
        wasp::HalfbandDown down;
        ReferenceHalfbandDown refDown;
        for (int i = 0; i < 500; ++i) {
            float in0 = std::sin(2.f * kPi * freq * (float)(2*i)   / fs);
            float in1 = std::sin(2.f * kPi * freq * (float)(2*i+1) / fs);
            down.process(in0, in1);
            refDown.process(in0, in1);
        }
        down.reset();
        refDown.reset();
        float maxDiff = 0.f;
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            float noise0 = ((float)rand() / (float)RAND_MAX) * 2.f - 1.f;
            float noise1 = ((float)rand() / (float)RAND_MAX) * 2.f - 1.f;
            float in0 = std::cos(2.f * kPi * freq * (float)(2*i)   / fs) * 0.6f + noise0 * 0.2f;
            float in1 = std::cos(2.f * kPi * freq * (float)(2*i+1) / fs) * 0.6f + noise1 * 0.2f;
            float got = down.process(in0, in1);
            float want = refDown.process(in0, in1);
            float d = std::fabs(got - want);
            if (d > maxDiff) maxDiff = d;
            if (d > tol) ok = false;
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "post-reset n=%d maxDiff=%.3e (want < %.0e)", n, maxDiff, tol);
        reportWithDetail(ok, "HalfbandDown matches reference immediately after reset()", buf);
    }
}

// ── Halfband perf bench (Task 5b, informational — not a pass/fail test) ────
// Times a 10s-equivalent 4x-oversampled stereo render loop (3 HalfbandUp +
// 9 HalfbandDown calls/channel/host-sample, per the Task 5 accounting) using
// the CURRENT (optimized) wasp::HalfbandUp/Down, then the same loop against
// the Reference (pre-optimization) implementations, and reports the ratio.
static void bench_halfband() {
    printf("\n8. Halfband perf bench (4x stereo, 10s-equivalent)\n");
    const float fs = 48000.f;
    const int hostSamples = (int)(fs * 10.0); // 10s of host-rate samples
    const float freq = 220.f;
    const float kPi = 3.14159265358979f;

    auto runOptimized = [&]() {
        wasp::HalfbandUp up[2], up4a[2];
        wasp::HalfbandDown downLp[2], downBp[2], downHp[2];
        wasp::HalfbandDown downLp4a[2], downBp4a[2], downHp4a[2];
        double sink = 0.0;
        for (int i = 0; i < hostSamples; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                float in = std::sin(2.f * kPi * freq * (float)i / fs);
                float buf2[2]; up[ch].process(in, buf2);
                float buf4[4];
                up4a[ch].process(buf2[0], &buf4[0]);
                up4a[ch].process(buf2[1], &buf4[2]);
                // Stand in for the 4 WaspFilter::process calls (out of scope
                // here) with the identity, then decimate back down exactly
                // as engine.hpp's Channel::process os==4 path does.
                float midLp0 = downLp4a[ch].process(buf4[0], buf4[1]);
                float midLp1 = downLp4a[ch].process(buf4[2], buf4[3]);
                float midBp0 = downBp4a[ch].process(buf4[0], buf4[1]);
                float midBp1 = downBp4a[ch].process(buf4[2], buf4[3]);
                float midHp0 = downHp4a[ch].process(buf4[0], buf4[1]);
                float midHp1 = downHp4a[ch].process(buf4[2], buf4[3]);
                sink += downLp[ch].process(midLp0, midLp1);
                sink += downBp[ch].process(midBp0, midBp1);
                sink += downHp[ch].process(midHp0, midHp1);
            }
        }
        return sink;
    };
    auto runReference = [&]() {
        ReferenceHalfbandUp up[2], up4a[2];
        ReferenceHalfbandDown downLp[2], downBp[2], downHp[2];
        ReferenceHalfbandDown downLp4a[2], downBp4a[2], downHp4a[2];
        double sink = 0.0;
        for (int i = 0; i < hostSamples; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                float in = std::sin(2.f * kPi * freq * (float)i / fs);
                float buf2[2]; up[ch].process(in, buf2);
                float buf4[4];
                up4a[ch].process(buf2[0], &buf4[0]);
                up4a[ch].process(buf2[1], &buf4[2]);
                float midLp0 = downLp4a[ch].process(buf4[0], buf4[1]);
                float midLp1 = downLp4a[ch].process(buf4[2], buf4[3]);
                float midBp0 = downBp4a[ch].process(buf4[0], buf4[1]);
                float midBp1 = downBp4a[ch].process(buf4[2], buf4[3]);
                float midHp0 = downHp4a[ch].process(buf4[0], buf4[1]);
                float midHp1 = downHp4a[ch].process(buf4[2], buf4[3]);
                sink += downLp[ch].process(midLp0, midLp1);
                sink += downBp[ch].process(midBp0, midBp1);
                sink += downHp[ch].process(midHp0, midHp1);
            }
        }
        return sink;
    };

    using clk = std::chrono::steady_clock;

    auto t0 = clk::now();
    double sinkOpt = runOptimized();
    auto t1 = clk::now();
    double sinkRef = runReference();
    auto t2 = clk::now();

    double msOpt = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double msRef = std::chrono::duration<double, std::milli>(t2 - t1).count();
    double ratio = msRef / msOpt;

    printf("  optimized: %.2f ms  (sink=%.6f, ignore)\n", msOpt, sinkOpt);
    printf("  reference: %.2f ms  (sink=%.6f, ignore)\n", msRef, sinkRef);
    printf("  speedup:   %.2fx\n", ratio);
    // Informational bench, not a strict gate (timing varies by machine), but
    // fail loudly if the "optimization" regressed rather than helped.
    report(ratio > 1.0, "optimized halfband render loop is faster than reference");
}

// ── railClamp ───────────────────────────────────────────────────────────────

static void test_rail_clamp() {
    printf("\n6. railClamp\n");
    const float vHi = 5.f, vLo = 5.f;

    // Identity inside +/- (limit - 0.3).
    const float insideVals[] = { 0.f, 1.f, -1.f, 4.f, -4.f, 4.6f, -4.6f };
    for (float v : insideVals) {
        float out = wasp::railClamp(v, vHi, vLo);
        char buf[64];
        snprintf(buf, sizeof(buf), "railClamp(%.2f)=%.6f (want == input)", v, out);
        report(out == v, "railClamp is identity within 0.3 V of rail", buf);
    }

    // Output never exceeds vHi/vLo + 1e-3, even far outside.
    const float outsideVals[] = { 5.f, 10.f, 100.f, 1000.f, -5.f, -10.f, -100.f, -1000.f };
    for (float v : outsideVals) {
        float out = wasp::railClamp(v, vHi, vLo);
        char buf[64];
        snprintf(buf, sizeof(buf), "railClamp(%.2f)=%.6f (want within [-vLo-1e-3, vHi+1e-3])", v, out);
        report(out <= vHi + 1e-3f && out >= -vLo - 1e-3f,
               "railClamp output never exceeds vHi/vLo + 1e-3", buf);
    }

    // Monotone: sample across the range and confirm non-decreasing output.
    bool monotone = true;
    float prev = wasp::railClamp(-20.f, vHi, vLo);
    for (int i = -199; i <= 200; ++i) {
        float v = (float)i / 10.f; // -19.9 .. 20.0
        float out = wasp::railClamp(v, vHi, vLo);
        if (out < prev - 1e-6f) monotone = false;
        prev = out;
    }
    report(monotone, "railClamp is monotone non-decreasing across the swept range");
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("Wasp DSP utils test suite\n");
    printf("=======================\n");

    test_tanh_approx();
    test_tanh_fast_variants();
    test_tanh_x_dx();
    test_compute_h1();
    test_dc_blocker();
    test_halfband_roundtrip();
    test_inner_halfband();
    test_halfband_matches_reference();
    bench_halfband();
    test_rail_clamp();

    printf("\n=======================\n");
    printf("%d passed, %d failed\n", sPassed, sFailed);
    return sFailed > 0 ? 1 : 0;
}
