// tests/onbetap/test_engine.cpp — OnbetapVoice/Pool + DriftWalker
#include "onbetap/engine.hpp"
#include <cmath>
#include <cstdio>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

int main() {
    // Pool: activating voices resets them
    {
        OnbetapPool pool;
        float g = OnbetapFilter::cutoffToG(1000.f, 96000.f);
        for (int i = 0; i < 100; i++) pool.voices[3].fL.processG(1.f, g, 0.2f);
        pool.setVoices(2);
        pool.setVoices(8);   // voice 3 re-enters: must be clean
        auto o = pool.voices[3].fL.processG(0.f, g, 1.0f);
        CHECK(o.lp == 0.f && o.bp == 0.f, "re-entering voice starts clean");
        CHECK(pool.activeVoices == 8, "activeVoices tracks setVoices");
    }
    // sanitize clears NaN states but leaves smoother values alone
    {
        OnbetapVoice v;
        float g = OnbetapFilter::cutoffToG(1000.f, 96000.f);
        v.fL.processG(std::nanf(""), g, 0.5f);
        CHECK(!v.fL.stateFinite(), "NaN poisons state");
        v.gSlew.reset(0.123f);
        v.sanitize();
        CHECK(v.fL.stateFinite(), "sanitize restores filters");
        CHECK(v.gSlew.value == 0.123f, "sanitize leaves smoothers");
    }
    // DriftWalker: deterministic, zero-mean-ish, bounded by depth
    {
        onbetap::DriftWalker a(0x1234), b(0x1234), c(0x5678);
        float maxAbs = 0; bool same = true, differs = false;
        for (int i = 0; i < 100000; i++) {           // ~4 min of 2.5 ms steps
            float va = a.step(0.0025f, 0.3f);
            float vb = b.step(0.0025f, 0.3f);
            float vc = c.step(0.0025f, 0.3f);
            same = same && (va == vb);
            differs = differs || (va != vc);
            maxAbs = std::max(maxAbs, std::fabs(va));
        }
        CHECK(same, "same seed -> identical walk");
        CHECK(differs, "different seed -> different walk");
        CHECK(maxAbs > 0.02f && maxAbs < 0.9f, "drift wanders but stays bounded");
        CHECK(std::isfinite(a.value), "drift finite");
    }
    // DecimFir9 (stage-A decimator, 4x path): DC gain, impulse, reset
    {
        DecimFir9 f;
        float y = 0.f;
        for (int i = 0; i < 32; i++) y = f.push(1.f);
        CHECK(std::fabs(y - 1.f) < 1e-4f, "DecimFir9 DC gain ~1");
        f.reset();
        float imp[9];
        imp[0] = f.push(1.f);
        for (int i = 1; i < 9; i++) imp[i] = f.push(0.f);
        bool match = true;
        for (int i = 0; i < 9; i++)
            match = match && std::fabs(imp[i] - DecimFir9::h[i]) < 1e-6f;
        CHECK(match, "DecimFir9 impulse response = taps");
        f.reset();
        CHECK(f.push(0.f) == 0.f, "DecimFir9 reset clears state");
    }
    // sanitize() clears stage-A FIR state on NaN recovery
    {
        OnbetapVoice v;
        float g = OnbetapFilter::cutoffToG(1000.f, 192000.f);
        v.fir4LpL.push(1.f);
        v.fL.processG(std::nanf(""), g, 0.5f);
        v.sanitize();
        CHECK(v.fir4LpL.push(0.f) == 0.f, "sanitize clears stage-A FIRs");
    }
    // Composed 4x cascade: low-frequency sine passes at unity-ish gain
    {
        float fs = 48000.f, fsOs = fs * 4.f, tone = 100.f;
        float g = OnbetapFilter::cutoffToG(20000.f, fsOs);
        OnbetapFilter f; f.reset();
        DecimFir9 aLp, aBp, aHp; DecimFir13 bLp, bBp, bHp;
        float xPrev = 0.f, peak = 0.f;
        const float kTwoPiT = 6.28318530717959f;
        for (int n = 0; n < (int)fs; n++) {
            float x1 = std::sin(kTwoPiT * tone * n / fs);
            float lp = 0.f;
            for (int i = 1; i <= 4; i++) {
                float t = (float)i / 4.f;
                float x = xPrev + (x1 - xPrev) * t;
                auto o = f.processG(x, g, 1.02f);
                float al = aLp.push(o.lp);
                aBp.push(o.bp); aHp.push(o.hp);
                if ((i & 1) == 0) {
                    float fl = bLp.push(al);
                    if (i == 4) lp = fl;
                }
            }
            xPrev = x1;
            if (n > (int)fs / 2) peak = std::max(peak, std::fabs(lp));
        }
        // processG applies kGin (1.2, Erica input ratio) ahead of the core,
        // so "unity" through the raw filter is kGin, not 1.
        CHECK(peak > 0.85f * OnbetapFilter::kGin && peak < 1.15f * OnbetapFilter::kGin,
              "4x cascade passes 100 Hz at unity-ish gain (x kGin)");
    }
    // cutoffLagCorr: matches the calibrated 2x/48k per-sample expression
    // (g²/(1+g²) at fsOs = 96 kHz) and is a pure function of fc — rate
    // independence is by construction (no fsOs parameter).
    {
        const float fcs[] = {20.f, 200.f, 1000.f, 5000.f, 8000.f,
                             18000.f, 20000.f, 23500.f};
        bool match = true;
        for (float fc : fcs) {
            float g = OnbetapFilter::cutoffToG(fc, onbetap::kCLagRefFsOs);
            float old2x = g * g / (1.f + g * g);
            match = match && std::fabs(onbetap::cutoffLagCorr(fc) - old2x) < 1e-6f;
        }
        CHECK(match, "cutoffLagCorr matches calibrated 2x/48k correction");
        CHECK(onbetap::cutoffLagCorr(20000.f) > onbetap::cutoffLagCorr(200.f),
              "cutoffLagCorr grows with cutoff");
    }
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
