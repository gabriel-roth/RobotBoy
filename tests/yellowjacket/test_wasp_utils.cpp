/**
 * test_wasp_utils.cpp — Test suite for src/yellowjacket/wasp_dsp_utils.hpp
 *
 * Compile & run (or via tests/run.sh):
 *   g++ -std=c++20 -O2 -I../src -o test_wasp_utils test_wasp_utils.cpp && ./test_wasp_utils
 */

#include "../../src/yellowjacket/wasp_dsp_utils.hpp"
#include <cmath>
#include <cstdio>

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
    test_tanh_x_dx();
    test_compute_h1();
    test_dc_blocker();
    test_halfband_roundtrip();
    test_rail_clamp();

    printf("\n=======================\n");
    printf("%d passed, %d failed\n", sPassed, sFailed);
    return sFailed > 0 ? 1 : 0;
}
