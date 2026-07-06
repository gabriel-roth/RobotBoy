/**
 * test_module_dsp.cpp — Test suite for src/dsp_utils.hpp pure math
 *
 * Compile & run:
 *   g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o test_module_dsp test_module_dsp.cpp && ./test_module_dsp
 */

#include "../../src/mf20/dsp_utils.hpp"
#include <cmath>
#include <cstdio>

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

static bool near(float actual, float expected, float tol = 0.01f) {
    if (expected == 0.f) return std::fabs(actual) < tol;
    return std::fabs(actual - expected) / std::fabs(expected) < tol;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. Smoother initial value is set correctly
static void test_smoother_initial_value() {
    printf("\n1. Smoother initial value\n");
    OnePoleSmoother s(3.5f);
    char buf[48];
    snprintf(buf, sizeof(buf), "value=%.4f (want 3.5)", s.value);
    report(s.value == 3.5f, "OnePoleSmoother(3.5f).value == 3.5f exactly", buf);
}

// 2. Smoother converges to target after 5τ
//    Alpha at 44100 Hz, τ=5 ms → 5τ = 1102 steps → ≥ 99.3% of target.
static void test_smoother_convergence() {
    printf("\n2. Smoother converges after 5τ\n");
    float alpha = smootherAlpha(44100.f, 0.005f);
    OnePoleSmoother s(0.f, alpha);
    int N = (int)(5.f * 0.005f * 44100.f);  // ≈ 1102
    for (int i = 0; i < N; i++)
        s.process(1.f);
    char buf[64];
    snprintf(buf, sizeof(buf), "value=%.5f after %d steps (want ≥ 0.99)", s.value, N);
    report(s.value >= 0.99f, "after 5τ steps toward 1.0 from 0.0, value ≥ 0.99", buf);
}

// 3. Smoother moves in correct direction after one step
static void test_smoother_overshoot() {
    printf("\n3. Smoother moves toward target\n");
    OnePoleSmoother s(1.f);
    s.process(0.f);
    char buf[48];
    snprintf(buf, sizeof(buf), "value=%.6f (want < 1.0)", s.value);
    report(s.value < 1.f, "smoother at 1.0 targeting 0.0 decreases after 1 step", buf);
}

// 4. reset() snaps value and next process() returns target exactly
static void test_smoother_reset() {
    printf("\n4. Smoother reset()\n");
    OnePoleSmoother s(0.f);
    for (int i = 0; i < 10; i++) s.process(0.5f);  // advance partway
    s.reset(7.f);
    float next = s.process(7.f);
    char buf[96];
    snprintf(buf, sizeof(buf), "after reset: value=%.4f; process(7.0)=%.6f (both want 7.0)",
             s.value, next);
    report(s.value == 7.f && next == 7.f,
           "reset(7.0) sets value=7.0, process(7.0) returns 7.0 exactly", buf);
}

// 5. smootherAlpha formula matches expected value
static void test_smoother_alpha_formula() {
    printf("\n5. smootherAlpha formula\n");
    float alpha    = smootherAlpha(44100.f, 0.005f);
    float expected = 1.f - std::exp(-1.f / (44100.f * 0.005f));
    char buf[96];
    snprintf(buf, sizeof(buf), "alpha=%.6f, expected=%.6f", alpha, expected);
    report(near(alpha, expected, 0.001f),
           "smootherAlpha(44100, 0.005) ≈ 1 - exp(-1/220.5)", buf);
}

// 6. resTaper(0) == 0 exactly
static void test_res_taper_zero() {
    printf("\n6. resTaper(0.0) == 0.0\n");
    float r = resTaper(0.f);
    char buf[48];
    snprintf(buf, sizeof(buf), "resTaper(0)=%.6f (want 0.0)", r);
    report(r == 0.f, "resTaper(0.0) == 0.0 exactly", buf);
}

// 7. resTaper(1) ≈ 1.025
static void test_res_taper_unity() {
    printf("\n7. resTaper(1.0) ≈ 1.025\n");
    float r = resTaper(1.f);
    char buf[48];
    snprintf(buf, sizeof(buf), "resTaper(1)=%.6f (want 1.025)", r);
    report(near(r, 1.025f, 0.001f), "resTaper(1.0) ≈ 1.025 (within 0.1%)", buf);
}

// 8. resTaper(0.5) ≈ 0.76875
//    (2·0.5 − 0.5²) · 1.025 = 0.75 · 1.025 = 0.76875
static void test_res_taper_half() {
    printf("\n8. resTaper(0.5) ≈ 0.76875\n");
    float r        = resTaper(0.5f);
    float expected = 0.75f * 1.025f;
    char buf[64];
    snprintf(buf, sizeof(buf), "resTaper(0.5)=%.6f, expected=%.6f", r, expected);
    report(near(r, expected, 0.001f), "resTaper(0.5) ≈ 0.75 × 1.025 = 0.76875 (within 0.1%)", buf);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("DSP utils test suite\n");
    printf("=======================\n");

    test_smoother_initial_value();
    test_smoother_convergence();
    test_smoother_overshoot();
    test_smoother_reset();
    test_smoother_alpha_formula();
    test_res_taper_zero();
    test_res_taper_unity();
    test_res_taper_half();

    printf("\n=======================\n");
    printf("%d passed, %d failed\n", sPassed, sFailed);
    return sFailed > 0 ? 1 : 0;
}
