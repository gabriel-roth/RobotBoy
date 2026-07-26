/**
 * test_wasp_purity.cpp — Large-signal spectral purity of the Wasp VCF core.
 *
 * Regression test for the British-mode noise bug (2026-07-26): the bare
 * fixed-pivot solve leaves per-sample solver error that lands in-band as
 * inharmonic, aliasing-like noise — ~10% of audio-band energy on a 1760 Hz
 * saw in British mode (vs 0.07% with the Newton refinement the 2026-07-24
 * CPU pass removed). German mode barely shows it, which is why the golden
 * behavioral suite (small-signal response, self-osc amplitude) never caught
 * it.
 *
 * Method: drive the core at fsInt = 192 kHz with a bandlimited sawtooth at
 * the module's drive-0 staging, discard 0.5 s of settling, then measure one
 * exact second (an integer number of periods for integer f0, so rectangular
 * DFT bins are leakage-free). Harmonic power is summed from exact-bin DFT
 * coefficients at k*f0 up to Nyquist; everything else (minus DC) is solver
 * error, fold-back, or drift. Thresholds sit ~5x above the Newton-refined
 * core's measured values and ~4x below the broken core's.
 *
 * Compile & run (or via tests/run.sh):
 *   g++ -std=c++20 -O2 -I../src -o test_wasp_purity test_wasp_purity.cpp && ./test_wasp_purity
 */

#include "../../src/vespid/WaspFilter.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int sPassed = 0, sFailed = 0;
static void report(bool ok, const char* name, const char* detail = nullptr) {
    if (ok) { printf("  PASS  %s\n", name); ++sPassed; }
    else {
        printf("  FAIL  %s", name);
        if (detail) printf("  (%s)", detail);
        printf("\n"); ++sFailed;
    }
}

// Inharmonic energy fraction (%) of the core's LP output for a bandlimited
// saw at integer f0, one exact second measured after settleSec of warmup.
static double inharmonicPct(const wasp::ModeConfig& mode, float fPole,
                            int f0, double settleSec) {
    const float fsInt = 192000.f;
    const float fc = 2580.f;
    const int N = (int)fsInt;                    // 1 s: f0 integer -> integer periods
    const int settle = (int)(settleSec * fsInt);

    float fcInt = std::min(std::max(fc * mode.wcComp, 0.25f), 0.45f * fsInt);
    float g = std::tan(float(M_PI) * fcInt / fsInt);
    float wcInt = 2.f * float(M_PI) * fcInt;
    float kC2 = 27e3f * 100e-12f * wcInt - mode.kR2 * wcInt / (2.f * float(M_PI) * fPole);
    wasp::H1Coeffs h1 = wasp::computeH1(0.f, fsInt);

    wasp::WaspFilter filt;
    filt.setSampleRate(fsInt);
    filt.reset();
    filt.setMode(mode);

    // Bandlimited saw (harmonics < 20 kHz), +/-5 V, times the module's
    // drive-0 gain staging (2x knob floor * mode.inGain) — see Vespid.cpp.
    float driveGain = 2.f * mode.inGain;
    std::vector<float> lp(N);
    const double w0 = 2.0 * M_PI * f0 / fsInt;
    for (int i = 0; i < settle + N; i++) {
        double x = 0.0;
        for (int k = 1; k * f0 < 20000; k++)
            x += ((k & 1) ? 1.0 : -1.0) * std::sin(w0 * k * i) / k;
        float in = (float)(x * 5.0 * 2.0 / M_PI) * driveGain;
        float y = filt.process(in, g, h1, kC2).lp;
        if (i >= settle)
            lp[i - settle] = y;
    }

    // Exact-bin DFT power at DC and each harmonic k*f0 (bin m = k*f0 since
    // N = fs = 192000); total power from Parseval.
    double total = 0.0, dcRe = 0.0;
    for (int i = 0; i < N; i++) { total += (double)lp[i] * lp[i]; dcRe += lp[i]; }
    total /= N;
    double dcPow = (dcRe / N) * (dcRe / N);

    double harmPow = 0.0;
    for (int k = 1; k * f0 < 96000; k++) {
        double wm = 2.0 * M_PI * (double)(k * f0) / N;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < N; i++) {
            re += lp[i] * std::cos(wm * i);
            im -= lp[i] * std::sin(wm * i);
        }
        harmPow += 2.0 * (re * re + im * im) / ((double)N * N);
    }
    return 100.0 * std::max(0.0, total - dcPow - harmPow) / (total - dcPow);
}

int main() {
    printf("Wasp core large-signal spectral purity (inharmonic %% of LP energy)\n");
    char detail[128];

    struct Case { const char* name; const wasp::ModeConfig& mode; float fPole;
                  int f0; double maxPct; };
    const Case cases[] = {
        // Newton-refined core measures ~0.005 / ~0.06 / ~2.4 / ~0.2 here;
        // the bare fixed-pivot core measures ~6 / ~10 / ~37 / ~0.2.
        { "British saw 440 Hz",  wasp::kBritish, 60000.f,  440, 1.0 },
        { "British saw 1760 Hz", wasp::kBritish, 60000.f, 1760, 1.0 },
        { "British saw 3520 Hz", wasp::kBritish, 60000.f, 3520, 5.0 },
        { "German saw 1760 Hz",  wasp::kGerman,  50000.f, 1760, 1.0 },
    };
    for (const Case& c : cases) {
        double pct = inharmonicPct(c.mode, c.fPole, c.f0, 0.5);
        snprintf(detail, sizeof detail, "inharmonic %.3f%%, limit %.1f%%", pct, c.maxPct);
        report(pct < c.maxPct, c.name, detail);
    }

    printf("\n%d passed, %d failed\n", sPassed, sFailed);
    return sFailed ? 1 : 0;
}
