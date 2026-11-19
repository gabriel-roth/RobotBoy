/**
 * test_wasp_cutoff_ceiling.cpp — Output stays bounded at the cutoff ceiling.
 *
 * Guards kMaxCutoffHz / kMaxCutoffRatio (engine.hpp). The core's bp/lp
 * outputs aren't rail-bounded, so a cutoff pushed near Nyquist by frequency
 * CV (or by the knob alone at a low host rate) made a full-drive signal run
 * away: British mode measured ~180 V peak on LP with the old 0.45*fsInt
 * ceiling, ~10 V with the current one. The outputs now also pass through
 * outputSaturate (11.7 V ceiling), which would hide a runaway as a pinned
 * 11.7 V, so the British limit sits below that: British never reaches
 * the saturation ceiling unless the core runs away.
 *
 * Method: one VoiceEngine at full drive, resonance 0.5, a 110 Hz +/-5 V saw,
 * and a retriggered 10 V envelope summed onto a 750 Hz knob (1 V/oct),
 * mirroring Vespid::modulate's cutoff math, for 4 s. Peak output across
 * LP/BP/HP/Mix must stay under each case's limit.
 *
 * Compile & run (or via tests/run.sh):
 *   g++ -std=c++20 -O2 -I../src -o test_wasp_cutoff_ceiling test_wasp_cutoff_ceiling.cpp && ./test_wasp_cutoff_ceiling
 */

#include "../../src/vespid/engine.hpp"
#include <cmath>
#include <cstdio>

static int sPassed = 0, sFailed = 0;
static void report(bool ok, const char* name, const char* detail = nullptr) {
    if (ok) { printf("  PASS  %s\n", name); ++sPassed; }
    else {
        printf("  FAIL  %s", name);
        if (detail) printf("  (%s)", detail);
        printf("\n"); ++sFailed;
    }
}

static float peakOutput(const wasp::ModeConfig& mode, float fPole, float fs,
                        float knobHz, float envV) {
    using namespace wasp;
    const float fsInt = fs * kOversample;
    const float res = 0.5f;
    VoiceEngine eng;
    eng.setSampleRate(fs);
    eng.setAlpha(robotboy::smootherAlpha(fs, 0.005f));
    eng.l.filt.setMode(mode);
    eng.reset();

    const int modSteps = std::max(1, (int)(fs * 0.0025f));
    int steps = modSteps;
    float dither = 1e-4f, peak = 0.f;
    double ph = 0.0;
    for (int n = 0; n < (int)(fs * 4.f); n++) {
        // 2 ms linear attack, 80 ms exponential decay, retriggered at 2 Hz.
        float t = std::fmod(n / fs, 0.5f);
        float env = t < 0.002f ? envV * t / 0.002f
                               : envV * std::exp(-(t - 0.002f) / 0.08f);
        if (++steps >= modSteps) {
            steps = 0;
            eng.sanitize();
            float fc = std::clamp(std::exp2(std::log2(knobHz) + env), 1.f, kMaxCutoffHz);
            float fcInt = std::clamp(fc * mode.wcComp, 0.25f, kMaxCutoffRatio * fsInt);
            eng.gTarget = std::tan(float(M_PI) * fcInt / fsInt);
            float wcInt = 2.f * float(M_PI) * fcInt;
            eng.kC2Target = 27e3f * 100e-12f * wcInt
                          - mode.kR2 * wcInt / (2.f * float(M_PI) * fPole);
            eng.rhoTarget = res;
            eng.driveTarget = 2.f * std::exp2(5.f) * mode.inGain;   // drive 1
            H1Coeffs h = computeH1(res, fsInt);
            eng.beta0Target = h.beta0;
            eng.beta1Target = h.beta1;
            eng.alpha1Target = h.alpha1;
        }
        dither = -dither;
        float g = eng.gSlew.process(eng.gTarget);
        float kC2 = eng.kC2Slew.process(eng.kC2Target);
        float drive = eng.driveSlew.process(eng.driveTarget);
        H1Coeffs h1{eng.beta0Slew.process(eng.beta0Target),
                    eng.beta1Slew.process(eng.beta1Target),
                    eng.alpha1Slew.process(eng.alpha1Target)};
        ph += 110.0 / fs;
        if (ph >= 1.0) ph -= 1.0;
        float in = 5.f * (float)(2.0 * ph - 1.0);
        Channel::Out o = eng.l.process(in * drive + dither, g, h1, kC2,
                                       kOutLp | kOutBp | kOutHp | kOutMix,
                                       0.5f, mode.makeup);
        for (float v : {o.lp, o.bp, o.hp, o.mix})
            peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

int main() {
    printf("Wasp cutoff ceiling: full-drive peak output under cutoff CV\n");
    char detail[128];
    struct Case { const char* name; const wasp::ModeConfig& mode; float fPole;
                  float fs, knobHz, envV, maxV; };
    const Case cases[] = {
        // Old ceiling measured ~180 V (British) / ~20 V (German) at 48 kHz,
        // and ~225 V for the British knob alone at 20 kHz, 11.025 kHz host.
        { "British 48 kHz, 10 V envelope",     wasp::kBritish, 60000.f, 48000.f,   750.f, 10.f, 11.f },
        { "British 44.1 kHz, 10 V envelope",   wasp::kBritish, 60000.f, 44100.f,   750.f, 10.f, 11.f },
        { "British 11.025 kHz, knob at max",   wasp::kBritish, 60000.f, 11025.f, 20000.f,  0.f, 11.f },
        { "British 11.025 kHz, 10 V envelope", wasp::kBritish, 60000.f, 11025.f,   750.f, 10.f, 11.f },
        // German full drive sits in the saturation knee by design (~17.5 V
        // before outputSaturate); this checks the 11.7 V ceiling end to end.
        { "German 48 kHz, 10 V envelope",      wasp::kGerman,  50000.f, 48000.f,   750.f, 10.f, 11.7f },
    };
    for (const Case& c : cases) {
        float peak = peakOutput(c.mode, c.fPole, c.fs, c.knobHz, c.envV);
        snprintf(detail, sizeof detail, "peak %.2f V, limit %.1f V", peak, c.maxV);
        report(peak < c.maxV, c.name, detail);
    }

    printf("\n%d passed, %d failed\n", sPassed, sFailed);
    return sFailed ? 1 : 0;
}
