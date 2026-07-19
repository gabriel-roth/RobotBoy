// tests/onbetap/test_overdrive_stability.cpp — deep-overdrive pathologies.
//
// Guards two failure modes of the core at extreme input drive, both found
// by ear in patch auditioning (2026-07-18):
//
// 1. Subsonic burst / note swallow: at deep drive the huge input swamps the
//    first stage's secant gain (n1 -> 0), collapsing the loop's damping; the
//    resonant mode comes unhooked and rings at ~cutoff. With cutoff swept
//    subsonic that is a rail-scale 20-40 Hz rumble that replaces the audible
//    note ("the signal disappears"). Reproduced from a user patch: fc 20,
//    saw ~131 Hz +-5 V, res 0.68, onset trim +0.0441, Drive span 36, max Drive.
//
// 2. Rail-pin silence: asymmetric-clipping rectification DC drags both
//    integrator states to the negative rail, where the saturator used to be
//    exactly flat (zero small-signal gain) -> absolute silence. Needs even
//    hotter node drive (~span 48 + 10 V).
//
// Fix under test: (a) drive-gated state leak — pole ~15 Hz at full gate,
// gate = min(|xin|/8, 1), so zero-input self-oscillation is untouched;
// (b) residual slope (5%) on the core saturator beyond its former hard-flat
// clamp, so small-signal gain never reaches exactly zero. Both live in
// OnbetapFilter.hpp. Legitimate self-oscillation at low cutoff must survive
// unchanged — that is asserted here too.
#include "onbetap/OnbetapFilter.hpp"
#include "onbetap/engine.hpp"
#include "onbetap/drive.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

static constexpr float kTwoPi = 6.28318530717959f;

struct Meas { float rms, note, peak; };

// Full module glue (2x/LP path) as in Onbetap.cpp::processSide, with the
// wrapper's leak configuration mirrored. amp <= 0 means no input (self-osc).
static Meas measure(float cutoff, float tone, float amp, float res, float onset,
                    float span, float grit, float drive, bool saw,
                    OnbetapFilter::Limit lim = OnbetapFilter::Limit::Hard) {
    const float fs = 48000.f;
    float fsOs = fs * 2.f;
    float g = OnbetapFilter::cutoffToG(cutoff, fsOs);
    float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f) + onset;
    auto gn = onbetap::driveGains(drive, span, 1.f, 0.f, grit);
    // NOTE: replicates the module kEff law; numerically equivalent to
    // onbetap::cutoffLagCorr here only because this harness runs at
    // fsOs = kCLagRefFsOs (96 kHz). Revisit if the module kEff law changes.
    float kEff = std::max(k - 0.25f * g * g / (1.f + g * g), -0.31f);

    OnbetapFilter f; f.setLimit(lim);
    f.setMismatch(0, 0); f.setOffset(0);
    // Mirror the wrapper: leak pole boosted below kLeakCornerHz.
    float boost = std::clamp(OnbetapFilter::kLeakCornerHz / cutoff,
                             1.f, OnbetapFilter::kLeakBoostMax);
    f.setLeak(kTwoPi * OnbetapFilter::kLeakPoleHz / fsOs * boost);
    f.reset();
    DCBlock dc; DecimFir13 fLp, fBp, fHp; float xPrev = 0.f;
    float dcCoef = 1.f - kTwoPi * 1.6f / fs;

    float ftone = tone > 0 ? tone : cutoff;
    int warm = (int)(fs * 2.f);
    int spc = (int)std::lround(fs / ftone), W = spc * 128;
    std::vector<float> buf(W);
    float peak = 0.f;
    for (int n = 0; n < warm + W; n++) {
        float ph = std::fmod((float)n * ftone / fs, 1.f);
        float in = (amp <= 0.f) ? 0.f
                 : saw ? amp * (2.f * ph - 1.f)
                       : amp * std::sin(kTwoPi * ftone * n / fs);
        in += (n & 1) ? 1e-9f : -1e-9f;
        float lp = 0;
        float x1 = in * gn.driveScale;
        for (int i = 1; i <= 2; i++) {
            float t = (float)i / 2.f;
            float x = xPrev + (x1 - xPrev) * t;
            auto o = f.processG(x, g, kEff);
            float fl = fLp.push(o.lp); fBp.push(o.bp); fHp.push(o.hp);
            if (i == 2) lp = fl;
        }
        xPrev = x1;
        float v = -lp * gn.makeup;
        v = dc.process(v, dcCoef);
        float out = 9.f * OnbetapFilter::tanhish(gn.vcaPush * v / 9.f);
        if (n >= warm) {
            buf[n - warm] = out;
            peak = std::max(peak, std::fabs(out));
        }
    }
    double re = 0, im = 0, w = kTwoPi * (double)ftone / fs, tot = 0;
    for (int n = 0; n < W; n++) {
        re += buf[n] * std::cos(w * n); im += buf[n] * std::sin(w * n);
        tot += (double)buf[n] * buf[n];
    }
    return { (float)std::sqrt(tot / W),
             (float)(2.0 * std::sqrt(re * re + im * im) / W), peak };
}

int main() {
    char nm[128];

    // --- 1. Subsonic burst / note swallow (user patch conditions) ---
    Meas b85 = measure(20, 131, 5, 0.68f, 0.0441f, 36, 3.5f, 0.85f, true);
    Meas b10 = measure(20, 131, 5, 0.68f, 0.0441f, 36, 3.5f, 1.0f, true);
    snprintf(nm, sizeof nm, "burst: note@1.0 (%.2f V) >= 0.6 V", b10.note);
    CHECK(b10.note >= 0.6f, nm);
    snprintf(nm, sizeof nm, "burst: note@1.0 (%.2f) >= 0.8 x note@0.85 (%.2f)",
             b10.note, b85.note);
    CHECK(b10.note >= 0.8f * b85.note, nm);
    snprintf(nm, sizeof nm, "burst: rumble RMS@1.0 (%.2f V) <= 2 V", b10.rms);
    CHECK(b10.rms <= 2.f, nm);

    // --- 2. Rail-pin silence (extreme node drive) ---
    Meas pin = measure(20, 110, 10, 0.4f, 0.0441f, 48, 3.5f, 1.0f, false);
    snprintf(nm, sizeof nm, "rail-pin: output RMS (%.3f V) >= 0.5 V (was 0.000)", pin.rms);
    CHECK(pin.rms >= 0.5f, nm);

    // --- 3. Legitimate self-oscillation survives (no input -> no gate) ---
    Meas so750 = measure(750, 0, 0, 0.9f, 0.f, 30, 6, 0.f, false);
    Meas so80  = measure(80,  0, 0, 0.9f, 0.f, 30, 6, 0.f, false);
    Meas so40  = measure(40,  0, 0, 0.9f, 0.f, 30, 6, 0.f, false);
    snprintf(nm, sizeof nm, "self-osc fc 750 (%.2f V) >= 8 V", so750.rms);
    CHECK(so750.rms >= 8.f, nm);
    snprintf(nm, sizeof nm, "self-osc fc 80 (%.2f V) >= 7 V", so80.rms);
    CHECK(so80.rms >= 7.f, nm);
    snprintf(nm, sizeof nm, "self-osc fc 40 (%.2f V) >= 3.5 V", so40.rms);
    CHECK(so40.rms >= 3.5f, nm);

    // --- 4. Output bound unchanged ---
    snprintf(nm, sizeof nm, "peak (%.2f V) <= 9 V at burst conditions", b10.peak);
    CHECK(b10.peak <= 9.001f, nm);

    // --- 5. Same guards under Soft limiting (the shipped default) ---
    const auto S = OnbetapFilter::Limit::Soft;
    Meas sb85 = measure(20, 131, 5, 0.68f, 0.0441f, 36, 3.5f, 0.85f, true, S);
    Meas sb10 = measure(20, 131, 5, 0.68f, 0.0441f, 36, 3.5f, 1.0f, true, S);
    Meas spin = measure(20, 110, 10, 0.4f, 0.0441f, 48, 3.5f, 1.0f, false, S);
    Meas sso80 = measure(80, 0, 0, 0.9f, 0.f, 30, 6, 0.f, false, S);
    snprintf(nm, sizeof nm, "Soft burst: note@1.0 (%.2f V) >= 0.6 V", sb10.note);
    CHECK(sb10.note >= 0.6f, nm);
    snprintf(nm, sizeof nm, "Soft burst: note@1.0 (%.2f) >= 0.8 x note@0.85 (%.2f)",
             sb10.note, sb85.note);
    CHECK(sb10.note >= 0.8f * sb85.note, nm);
    snprintf(nm, sizeof nm, "Soft burst: rumble RMS@1.0 (%.2f V) <= 2 V", sb10.rms);
    CHECK(sb10.rms <= 2.f, nm);
    snprintf(nm, sizeof nm, "Soft rail-pin: output RMS (%.3f V) >= 0.5 V", spin.rms);
    CHECK(spin.rms >= 0.5f, nm);
    snprintf(nm, sizeof nm, "Soft self-osc fc 80 (%.2f V) >= 5 V", sso80.rms);
    CHECK(sso80.rms >= 5.f, nm);

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
