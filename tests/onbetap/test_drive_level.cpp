// tests/onbetap/test_drive_level.cpp — module-path Drive behavior (host-free).
//
// Guards the "drive fights resonance" promise: as Drive rises, the output must
// get LOUDER-or-level (never quieter) and DIRTIER. The bug this catches was a
// drive-dependent output makeup gain that double-compensated the core's own
// rail-clamp compression, so Drive made the module quieter and cleaner instead.
// See docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md.
//
// Mirrors Onbetap.cpp::processSide for the 2×/LP path (the stable audio glue),
// but computes the gains under test via the real onbetap::driveGains(), so the
// makeup formula itself is exercised, not a copy.
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

// One host sample through the 2× oversampled LP path (mirrors processSide).
static float sideLP(OnbetapFilter& f, float& xPrev, DCBlock& dc, float inVolts,
                    float g, float kEff, float driveScale, float makeup,
                    DecimFir13& firLp, DecimFir13& firBp, DecimFir13& firHp,
                    float dcCoef) {
    float lp = 0, bp = 0, hp = 0;
    float x1 = inVolts * driveScale;
    for (int i = 1; i <= 2; i++) {
        float t = (float)i / 2.f;
        float x = xPrev + (x1 - xPrev) * t;
        auto o = f.processG(x, g, kEff);
        float fl = firLp.push(o.lp), fb = firBp.push(o.bp), fh = firHp.push(o.hp);
        if (i == 2) { lp = fl; bp = fb; hp = fh; }
    }
    xPrev = x1;
    float v = -lp * makeup;                       // LP tap
    v = dc.process(v, dcCoef);
    return 9.f * OnbetapFilter::tanhish(v / 9.f); // output VCA
}

struct Meas { float outDb, thdPct; };

// Steady-state output level (dB re 1 V) and THD for one Drive point.
static Meas measure(float cutoff, float res, float drive, float amp, float fs) {
    float fsOs = fs * 2.f, tone = cutoff;
    float g = OnbetapFilter::cutoffToG(cutoff, fsOs);
    float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f);   // res→k (tuneOnset 0)
    auto gains = onbetap::driveGains(drive, 30.f, 1.f, 0.f); // spec defaults (span 30 dB)
    float kCLag = 0.25f;
    float kEff = std::max(k - kCLag * g * g / (1.f + g * g), -0.31f);

    OnbetapFilter f; f.setLimit(OnbetapFilter::Limit::Hard);
    f.setMismatch(0, 0); f.setOffset(0); f.reset();
    DCBlock dc; DecimFir13 fLp, fBp, fHp; float xPrev = 0.f;
    float dcCoef = 1.f - kTwoPi * 1.6f / fs;

    int warm = (int)(fs * 1.0f);
    int spc = (int)std::lround(fs / tone), W = spc * 256;   // coherent window
    std::vector<float> buf(W);
    for (int n = 0; n < warm + W; n++) {
        float dither = (n & 1) ? 1e-9f : -1e-9f;
        float in = amp * std::sin(kTwoPi * tone * n / fs) + dither;
        float out = sideLP(f, xPrev, dc, in, g, kEff, gains.driveScale,
                           gains.makeup, fLp, fBp, fHp, dcCoef);
        if (n >= warm) buf[n - warm] = out;
    }
    auto ampAt = [&](int h) {
        double re = 0, im = 0, w = kTwoPi * (double)h * tone / fs;
        for (int n = 0; n < W; n++) { re += buf[n] * std::cos(w * n); im += buf[n] * std::sin(w * n); }
        return 2.0 * std::sqrt(re * re + im * im) / W;
    };
    double fund = ampAt(1), hs = 0;
    for (int h = 2; h <= 12; h++) { double a = ampAt(h); hs += a * a; }
    double tot = 0; for (int n = 0; n < W; n++) tot += (double)buf[n] * buf[n];
    float rms = (float)std::sqrt(tot / W);
    return { 20.f * std::log10(rms + 1e-20f),
             (float)(std::sqrt(hs) / (fund + 1e-20)) * 100.f };
}

int main() {
    const float fs = 48000.f, cutoff = 750.f, amp = 5.f;
    const float resSet[] = { 0.0f, 0.30f, 0.70f };
    for (float res : resSet) {
        Meas d0 = measure(cutoff, res, 0.0f, amp, fs);
        Meas d5 = measure(cutoff, res, 0.5f, amp, fs);
        Meas d1 = measure(cutoff, res, 1.0f, amp, fs);
        char nm[96];

        // Never quieter as Drive rises (the inverted-compensation bug).
        snprintf(nm, sizeof nm,
                 "res %.2f: level@0.5 (%.1f) >= level@0 (%.1f) - 0.5 dB",
                 res, d5.outDb, d0.outDb);
        CHECK(d5.outDb >= d0.outDb - 0.5f, nm);
        snprintf(nm, sizeof nm,
                 "res %.2f: level@1.0 (%.1f) >= level@0 (%.1f) - 1.0 dB",
                 res, d1.outDb, d0.outDb);
        CHECK(d1.outDb >= d0.outDb - 1.0f, nm);

        // Dirtier: THD at half Drive clearly above THD at zero Drive.
        snprintf(nm, sizeof nm,
                 "res %.2f: THD@0.5 (%.1f%%) >= THD@0 (%.1f%%) + 2 pp",
                 res, d5.thdPct, d0.thdPct);
        CHECK(d5.thdPct >= d0.thdPct + 2.0f, nm);
    }
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
