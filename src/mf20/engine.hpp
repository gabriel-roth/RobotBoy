#pragma once
/**
 * engine.hpp — Per-voice DSP state for MF20FilterModule polyphony.
 *
 * VoiceEngine holds all state needed to process one polyphonic voice independently:
 * four MF20Filter instances (LP+HP, L+R) and four OnePoleSmoothers.
 *
 * EnginePool manages the fixed-size array of VoiceEngines used by MF20FilterModule.
 */

#include "MF20Filter.hpp"
#include "dsp_utils.hpp"
#include <algorithm>
#include <cmath>

struct VoiceEngine {
    // Default g (prewarp-gain) values ≈ the 750 Hz / 120 Hz cutoff defaults
    // at 48 kHz; the first modulate() call corrects them within 2.5 ms.
    static constexpr float kDefaultLpG = 0.0491f;
    static constexpr float kDefaultHpG = 0.0079f;

    MF20Filter lpFilter,  hpFilter;
    MF20Filter lpFilterR, hpFilterR;

    // Cutoff smoothing happens in the g (prewarp-gain) domain: modulate()
    // computes tan/exp2 at ~2.5 ms intervals and the audio path only slews g.
    OnePoleSmoother lpGSlew { kDefaultLpG };
    OnePoleSmoother hpGSlew { kDefaultHpG };
    OnePoleSmoother lpResSlew { 0.25f };
    OnePoleSmoother hpResSlew { 0.25f };

    float lpGTarget   = kDefaultLpG;
    float hpGTarget   = kDefaultHpG;
    float lpResTarget = 0.25f;
    float hpResTarget = 0.25f;

    void setSampleRate(float fs) {
        lpFilter.setSampleRate(fs);
        hpFilter.setSampleRate(fs);
        lpFilterR.setSampleRate(fs);
        hpFilterR.setSampleRate(fs);
    }

    void reset() {
        lpFilter.reset();
        hpFilter.reset();
        lpFilterR.reset();
        hpFilterR.reset();
        lpGSlew.reset(kDefaultLpG);
        hpGSlew.reset(kDefaultHpG);
        lpResSlew.reset(0.25f);
        hpResSlew.reset(0.25f);
        // Also reset the slew targets -- otherwise a growth-reset voice keeps
        // slewing toward whatever stale target it had before, for up to one
        // modulate() interval (~2.5 ms), instead of starting flat at the
        // default cutoff/resonance the slews were just reset to.
        lpGTarget = kDefaultLpG;
        hpGTarget = kDefaultHpG;
        lpResTarget = 0.25f;
        hpResTarget = 0.25f;
    }

    // NaN/inf recovery: one bad upstream sample would otherwise poison the
    // filter state permanently. Called once per modulate block (~2.5 ms) by
    // the module. Resets only the integrator states; the slew smoothers stay
    // (their inputs are clamped params, so they are finite), avoiding a
    // spurious parameter sweep after recovery.
    void sanitize() {
        if (!lpFilter.stateFinite()  || !hpFilter.stateFinite() ||
            !lpFilterR.stateFinite() || !hpFilterR.stateFinite()) {
            lpFilter.reset();
            hpFilter.reset();
            lpFilterR.reset();
            hpFilterR.reset();
        }
    }
};

// Manages the per-voice engines for polyphonic use. All 16 voices live by
// value (no heap, no null checks, better cache behavior on MetaModule);
// setVoices() only changes activeVoices, so the audio thread never allocates
// and every voice always carries the pool's current sample rate.
struct EnginePool {
    VoiceEngine engines[16];
    int activeVoices = 1;

    void setVoices(int n) {
        n = std::clamp(n, 1, 16);
        // Voices (re)entering the active range start clean — a voice that rang
        // at high resonance and went inactive must not re-emit its old state.
        for (int i = activeVoices; i < n; i++) engines[i].reset();
        activeVoices = n;
    }

    void setSampleRate(float fs) {
        for (auto& e : engines) e.setSampleRate(fs);
    }

    void resetAll() {
        for (auto& e : engines) e.reset();
    }
};
