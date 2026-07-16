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
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
