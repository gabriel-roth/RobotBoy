// tests/onbetap/test_drive_grit.cpp — Drive-grit VCA push (host-free).
//
// Guards the "top of the Drive knob keeps getting dirtier" promise: at high Q
// the (authentic) resonance choke used to take the resonance-derived grit and
// ~1 dB of level with it, so Drive got smoother/softer past ~90% of travel.
// The fix pushes the signal harder into the existing output VCA as Drive
// rises: out = 9*tanhish(push*v/9), push = exp2(gritDb/6.0206 * drive^2).
// See docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md.
//
// Mirrors Onbetap.cpp::processSide for the 2x/LP path (gains from the real
// onbetap::driveGains). Run with any argument to print the calibration sweep
// table instead of asserting.
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

// One host sample through the 2x oversampled LP path (mirrors processSide,
// including the Drive-grit VCA push).
static float sideLP(OnbetapFilter& f, float& xPrev, DCBlock& dc, float inVolts,
                    float g, float kEff, float driveScale, float makeup,
                    float push, DecimFir13& firLp, DecimFir13& firBp,
                    DecimFir13& firHp, float dcCoef) {
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
    float v = -lp * makeup;                              // LP tap
    v = dc.process(v, dcCoef);
    return 9.f * OnbetapFilter::tanhish(push * v / 9.f); // output VCA + push
}

struct Meas { float outDb, thdPct, peak; };

// Steady-state output level (dB re 1 V), THD, and peak for one Drive point.
static Meas measure(float cutoff, float res, float drive, float amp, float fs,
                    float gritDb) {
    float fsOs = fs * 2.f, tone = cutoff;
    float g = OnbetapFilter::cutoffToG(cutoff, fsOs);
    float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f);   // res→k (tuneOnset 0)
    auto gains = onbetap::driveGains(drive, onbetap::kDriveSpanDb, 1.f, 0.f, gritDb);
    float kCLag = 0.25f;
    float kEff = std::max(k - kCLag * g * g / (1.f + g * g), -0.31f);

    OnbetapFilter f; f.setLimit(OnbetapFilter::Limit::Hard);
    f.setMismatch(0, 0); f.setOffset(0); f.reset();
    DCBlock dc; DecimFir13 fLp, fBp, fHp; float xPrev = 0.f;
    float dcCoef = 1.f - kTwoPi * 1.6f / fs;

    int warm = (int)(fs * 1.0f);
    int spc = (int)std::lround(fs / tone), W = spc * 256;   // coherent window
    std::vector<float> buf(W);
    float peak = 0.f;
    for (int n = 0; n < warm + W; n++) {
        float dither = (n & 1) ? 1e-9f : -1e-9f;
        float in = amp * std::sin(kTwoPi * tone * n / fs) + dither;
        float out = sideLP(f, xPrev, dc, in, g, kEff, gains.driveScale,
                           gains.makeup, gains.vcaPush, fLp, fBp, fHp, dcCoef);
        if (n >= warm) {
            buf[n - warm] = out;
            peak = std::max(peak, std::fabs(out));
        }
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
             (float)(std::sqrt(hs) / (fund + 1e-20)) * 100.f,
             peak };
}

int main(int argc, char**) {
    const float fs = 48000.f, cutoff = 750.f, amp = 5.f;
    const float dflt = onbetap::kDefaultGritDb;
    char nm[128];

    // --- Law unit checks (pure driveGains math) ---
    CHECK(onbetap::driveGains(0.f, 30.f, 1.f, 0.f, 12.f).vcaPush == 1.f,
          "push == 1 exactly at Drive 0 (bit-identity)");
    CHECK(onbetap::driveGains(0.5f, 30.f, 1.f, 0.f, 0.f).vcaPush == 1.f,
          "gritDb 0 -> push == 1 (escape hatch)");
    float p1 = onbetap::driveGains(1.f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(std::fabs(p1 - std::exp2(6.f / 6.0206f)) < 1e-4f,
          "push(drive 1, 6 dB) == exp2(6/6.0206)");
    float pa = onbetap::driveGains(0.25f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pb = onbetap::driveGains(0.50f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pc = onbetap::driveGains(0.75f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(1.f < pa && pa < pb && pb < pc && pc < p1,
          "push monotone increasing in drive");
    auto ga = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 0.f);
    auto gb = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 12.f);
    CHECK(ga.driveScale == gb.driveScale && ga.makeup == gb.makeup,
          "gritDb does not touch driveScale/makeup");

    // --- Calibration sweep mode (no asserts) ---
    if (argc > 1) {
        printf("gritDb | r.60 thd@.9 thd@1 lvl@1 | r.70 lvl@.9 lvl@1 | r.30 thd@.5 | r.50 thd@.5\n");
        for (float gdb : { 0.f, 3.5f, 4.f, 6.f, 8.f, 10.f, 12.f }) {
            Meas s9  = measure(cutoff, 0.60f, 0.9f, amp, fs, gdb);
            Meas s10 = measure(cutoff, 0.60f, 1.0f, amp, fs, gdb);
            Meas a = measure(cutoff, 0.70f, 0.9f, amp, fs, gdb);
            Meas b = measure(cutoff, 0.70f, 1.0f, amp, fs, gdb);
            Meas c = measure(cutoff, 0.30f, 0.5f, amp, fs, gdb);
            Meas d = measure(cutoff, 0.50f, 0.5f, amp, fs, gdb);
            printf("%6.1f | %10.1f %6.1f %6.1f | %10.1f %6.1f | %11.1f | %11.1f\n",
                   gdb, s9.thdPct, s10.thdPct, s10.outDb, a.outDb, b.outDb,
                   c.thdPct, d.thdPct);
        }
        return 0;
    }

    // --- Criterion 1a: top decile keeps getting dirtier (stable choked
    //     regime, res 0.60 — harmonic THD is a valid instrument here; at the
    //     res 0.70 knee the added edge is inharmonic and invisible to
    //     harmonic bins, see the grit spec's metric amendment) ---
    Meas s9  = measure(cutoff, 0.60f, 0.9f, amp, fs, dflt);
    Meas s10 = measure(cutoff, 0.60f, 1.0f, amp, fs, dflt);
    snprintf(nm, sizeof nm, "res 0.60: THD@1.0 (%.1f%%) >= THD@0.9 (%.1f%%) + 1 pp",
             s10.thdPct, s9.thdPct);
    CHECK(s10.thdPct >= s9.thdPct + 1.f, nm);
    snprintf(nm, sizeof nm, "res 0.60: THD@1.0 (%.1f%%) >= 25%%", s10.thdPct);
    CHECK(s10.thdPct >= 25.f, nm);
    snprintf(nm, sizeof nm, "res 0.60: level@1.0 (%.1f) >= level@0.9 (%.1f) - 0.5 dB",
             s10.outDb, s9.outDb);
    CHECK(s10.outDb >= s9.outDb - 0.5f, nm);

    // --- Criterion 1b: at the self-osc knee the push adds level (never
    //     softer), vs both the knob position below and the gritDb-0 baseline ---
    Meas k9   = measure(cutoff, 0.70f, 0.9f, amp, fs, dflt);
    Meas k10  = measure(cutoff, 0.70f, 1.0f, amp, fs, dflt);
    Meas k10b = measure(cutoff, 0.70f, 1.0f, amp, fs, 0.f);
    snprintf(nm, sizeof nm, "res 0.70: level@1.0 default (%.1f) >= gritDb0 (%.1f) + 0.5 dB",
             k10.outDb, k10b.outDb);
    CHECK(k10.outDb >= k10b.outDb + 0.5f, nm);
    snprintf(nm, sizeof nm, "res 0.70: level@1.0 (%.1f) >= level@0.9 (%.1f) - 0.5 dB",
             k10.outDb, k9.outDb);
    CHECK(k10.outDb >= k9.outDb - 0.5f, nm);

    // --- Criterion 4: mid-knob voicing preserved vs gritDb 0 baseline ---
    for (float res : { 0.30f, 0.50f }) {
        Meas base = measure(cutoff, res, 0.5f, amp, fs, 0.f);
        Meas grit = measure(cutoff, res, 0.5f, amp, fs, dflt);
        snprintf(nm, sizeof nm, "res %.2f: THD@0.5 default (%.1f%%) <= gritDb0 (%.1f%%) + 8 pp",
                 res, grit.thdPct, base.thdPct);
        CHECK(grit.thdPct <= base.thdPct + 8.f, nm);
    }

    // --- Criterion 5: level never drops > 0.5 dB per 0.1 Drive.
    //     res 0.30/0.50 only: res >= 0.60 has authentic brief self-osc
    //     pockets (investigation report section 5.2) that bump the level at
    //     isolated Drive points, so strict monotony is physically wrong there. ---
    for (float res : { 0.30f, 0.50f }) {
        Meas prev = measure(cutoff, res, 0.f, amp, fs, dflt);
        bool mono = true;
        float worst = 0.f;
        for (int i = 1; i <= 10; i++) {
            Meas cur = measure(cutoff, res, 0.1f * i, amp, fs, dflt);
            worst = std::min(worst, cur.outDb - prev.outDb);
            if (cur.outDb < prev.outDb - 0.5f) mono = false;
            prev = cur;
        }
        snprintf(nm, sizeof nm, "res %.2f: level monotone within 0.5 dB (worst step %.2f dB)",
                 res, worst);
        CHECK(mono, nm);
    }

    // --- Bound: 9 V VCA ceiling holds at max settings ---
    Meas worst = measure(cutoff, 0.70f, 1.0f, amp, fs, 12.f);
    snprintf(nm, sizeof nm, "peak (%.2f V) <= 9 V at res 0.70 / drive 1 / grit 12", worst.peak);
    CHECK(worst.peak <= 9.001f, nm);

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
