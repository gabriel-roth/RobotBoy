// tests/onbetap/test_onbetap.cpp — OnbetapFilter core tests (host-free)
#include "onbetap/OnbetapFilter.hpp"
#include <cmath>
#include <cstdio>

using namespace onbetap;

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

static constexpr float kFs = 96000.f;  // core runs at the oversampled rate
static constexpr float kPi = 3.14159265358979f;

// Run a sine of amplitude a at freq f through a fresh filter; return output
// RMS of the chosen tap over the last half of n samples (steady state).
enum Tap { LP, BP, HP };
struct SineResult { float rms, peak; };
static SineResult runSine(float f, float a, float fc, float k, Tap tap,
                          int n = 48000) {
    OnbetapFilter flt;
    float g = OnbetapFilter::cutoffToG(fc, kFs);
    double sumSq = 0; float peak = 0; int count = 0;
    for (int i = 0; i < n; i++) {
        float x = a * std::sin(2 * kPi * f * i / kFs);
        auto o = flt.processG(x, g, k);
        float y = (tap == LP) ? o.lp : (tap == BP) ? o.bp : o.hp;
        if (i >= n / 2) { sumSq += (double)y * y; peak = std::max(peak, std::fabs(y)); count++; }
    }
    return { (float)std::sqrt(sumSq / count), peak };
}

// Self-oscillation run: no input except a tiny alternating seed; returns RMS,
// peak and zero-crossing frequency estimate over the last half.
struct OscResult { float rms, peak, freq; };
static OscResult runOsc(float fc, float k, int n = 96000) {
    OnbetapFilter flt;
    float g = OnbetapFilter::cutoffToG(fc, kFs);
    double sumSq = 0; float peak = 0; int count = 0, crossings = 0;
    float prev = 0, dither = 1e-9f;
    for (int i = 0; i < n; i++) {
        auto o = flt.processG(dither, g, k);
        dither = -dither;
        if (i >= n / 2) {
            sumSq += (double)o.bp * o.bp; peak = std::max(peak, std::fabs(o.bp));
            if (prev <= 0.f && o.bp > 0.f) crossings++;
            prev = o.bp; count++;
        }
    }
    return { (float)std::sqrt(sumSq / count), peak, crossings * kFs / (float)count };
}

int main() {
    // ---- linear regime (tiny signals: secant gains ~1) ----
    // LP passband gain ≈ Gin = 1.2 (input resistor ratio 47k/39k)
    {
        auto r = runSine(100.f, 0.01f, 2000.f, 1.0f, LP);
        CHECK(std::fabs(r.rms / (0.01f / std::sqrt(2.f)) - 1.2f) < 0.06f,
              "LP passband gain ~1.2 at Q=1");
    }
    // LP slope: 12 dB/oct well above cutoff -> one octave = x4 amplitude
    {
        auto a1 = runSine(4000.f, 0.01f, 500.f, 1.0f, LP);
        auto a2 = runSine(8000.f, 0.01f, 500.f, 1.0f, LP);
        float ratio = a1.rms / a2.rms;
        CHECK(ratio > 3.3f && ratio < 4.8f, "LP slope ~12 dB/oct");
    }
    // BP skirts: 6 dB/oct -> one octave = x2
    {
        auto a1 = runSine(4000.f, 0.01f, 500.f, 1.0f, BP);
        auto a2 = runSine(8000.f, 0.01f, 500.f, 1.0f, BP);
        float ratio = a1.rms / a2.rms;
        CHECK(ratio > 1.7f && ratio < 2.4f, "BP skirt ~6 dB/oct");
    }
    // HP: passes highs, kills lows (12 dB/oct below cutoff)
    {
        auto hi = runSine(8000.f, 0.01f, 500.f, 1.0f, HP);
        auto lo1 = runSine(100.f, 0.01f, 2000.f, 1.0f, HP);
        auto lo2 = runSine(50.f,  0.01f, 2000.f, 1.0f, HP);
        CHECK(hi.rms > 0.008f, "HP passes highs");
        float ratio = lo1.rms / lo2.rms;
        CHECK(ratio > 3.3f && ratio < 4.8f, "HP slope ~12 dB/oct");
    }
    // Resonance peak at fc grows as k shrinks (Q = 1/k)
    {
        auto q1 = runSine(1000.f, 0.005f, 1000.f, 1.0f, BP);
        auto q4 = runSine(1000.f, 0.005f, 1000.f, 0.25f, BP);
        CHECK(q4.rms / q1.rms > 3.f, "BP peak scales with 1/k");
    }
    // ---- self-oscillation ----
    {
        auto osc = runOsc(1000.f, -0.06f);
        printf("info: self-osc rms=%.3f peak=%.3f freq=%.0f\n", osc.rms, osc.peak, osc.freq);
        CHECK(osc.rms > 0.3f, "self-osc sustains");
        CHECK(osc.peak < 4.2f, "self-osc bounded by the soft state limit");
        // Deep self-oscillation is a slew-limited relaxation oscillator, not
        // a sinusoid at fc: the state ramps at the maximum rate ωc across
        // most of its swing, landing near 0.3-0.4·fc.
        CHECK(osc.freq > 200.f && osc.freq < 450.f, "self-osc in slew-limited band");
    }
    // no oscillation at k = 1 (Q = 1)
    {
        auto osc = runOsc(1000.f, 1.0f);
        CHECK(osc.rms < 1e-3f, "no self-osc at Q=1");
    }
    // ---- drive suppresses resonance (the signature interaction) ----
    {
        float k = 0.15f;  // high resonance, below self-osc
        auto quiet = runSine(1000.f, 0.02f, 1000.f, k, BP);
        auto loud  = runSine(1000.f, 2.0f,  1000.f, k, BP);
        float gQuiet = quiet.rms / 0.02f, gLoud = loud.rms / 2.0f;
        CHECK(gLoud < 0.5f * gQuiet, "drive suppresses resonance gain");
    }
    // ---- mismatch and offset hooks ----
    {
        OnbetapFilter flt;
        flt.setMismatch(0.06f, -0.05f);
        float g = OnbetapFilter::cutoffToG(1000.f, kFs);
        for (int i = 0; i < 1000; i++) flt.processG(0.01f, g, 0.5f);
        CHECK(flt.stateFinite(), "mismatch stays finite");
        OnbetapFilter f2;
        f2.setOffset(0.05f);
        float dc = 0;
        for (int i = 0; i < 48000; i++) dc = f2.processG(0.f, g, 1.0f).lp;
        CHECK(std::fabs(dc) > 0.005f && std::fabs(dc) < 0.5f, "offset shifts LP DC");
    }
    // ---- stability torture: max res, huge input, cutoff sweep ----
    {
        OnbetapFilter flt;
        bool finite = true;
        float dither = 1e-9f;
        for (int i = 0; i < 96000; i++) {
            float fc = 20.f * std::exp2(10.f * (0.5f + 0.5f * std::sin(2 * kPi * 3.f * i / kFs)));
            float g = OnbetapFilter::cutoffToG(fc, kFs);
            float x = 20.f * std::sin(2 * kPi * 55.f * i / kFs) + dither;
            dither = -dither;
            auto o = flt.processG(x, g, -0.31f);   // worst-case kEff floor
            finite = finite && std::isfinite(o.lp) && std::isfinite(o.bp) && std::isfinite(o.hp);
        }
        CHECK(finite && flt.stateFinite(), "torture sweep stays finite");
    }
    // ---- determinism: two identical runs bit-match ----
    {
        auto a = runOsc(2000.f, -0.02f, 24000);
        auto b = runOsc(2000.f, -0.02f, 24000);
        CHECK(a.rms == b.rms && a.peak == b.peak, "deterministic");
    }
    // ---- reset / sanitize ----
    {
        OnbetapFilter flt;
        float g = OnbetapFilter::cutoffToG(1000.f, kFs);
        for (int i = 0; i < 100; i++) flt.processG(1.f, g, 0.5f);
        flt.reset();
        auto o = flt.processG(0.f, g, 1.0f);
        CHECK(o.lp == 0.f && o.bp == 0.f, "reset clears state");
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
