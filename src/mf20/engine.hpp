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
    MF20Filter lpFilter,  hpFilter;
    MF20Filter lpFilterR, hpFilterR;

    // Cutoff smoothing happens in the g (prewarp-gain) domain: modulate()
    // computes tan/exp2 at ~2.5 ms intervals and the audio path only slews g.
    // Init values ≈ the 750 Hz / 120 Hz defaults at 48 kHz; the first
    // modulate() corrects them within 2.5 ms.
    OnePoleSmoother lpGSlew { 0.0491f };
    OnePoleSmoother hpGSlew { 0.0079f };
    OnePoleSmoother lpResSlew { 0.25f };
    OnePoleSmoother hpResSlew { 0.25f };

    float lpGTarget   = 0.0491f;
    float hpGTarget   = 0.0079f;
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
        lpGSlew.reset(0.0491f);
        hpGSlew.reset(0.0079f);
        lpResSlew.reset(0.25f);
        hpResSlew.reset(0.25f);
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

    void setVoices(int n) { activeVoices = std::clamp(n, 1, 16); }

    void setSampleRate(float fs) {
        for (auto& e : engines) e.setSampleRate(fs);
    }

    void resetAll() {
        for (auto& e : engines) e.reset();
    }
};
