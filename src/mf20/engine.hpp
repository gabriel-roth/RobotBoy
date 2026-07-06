#pragma once
/**
 * engine.hpp — Per-voice DSP state for MF20FilterModule polyphony.
 *
 * VoiceEngine holds all state needed to process one polyphonic voice independently:
 * four MF20Filter instances (LP+HP, L+R) and four OnePoleSmoothers.
 *
 * EnginePool manages an array of VoiceEngine pointers, allocating/freeing them as
 * the voice count changes, and provides a processVoice() helper used by tests.
 */

#include "MF20Filter.hpp"
#include "dsp_utils.hpp"
#include <algorithm>
#include <cmath>

struct VoiceEngine {
    MF20Filter lpFilter,  hpFilter;
    MF20Filter lpFilterR, hpFilterR;

    OnePoleSmoother lpCutoffSlew { std::log2(750.f) };
    OnePoleSmoother hpCutoffSlew { std::log2(120.f) };
    OnePoleSmoother lpResSlew    { 0.25f };
    OnePoleSmoother hpResSlew    { 0.25f };

    float lpCutoffTarget = std::log2(750.f);
    float hpCutoffTarget = std::log2(120.f);
    float lpResTarget    = 0.25f;
    float hpResTarget    = 0.25f;

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
        lpCutoffSlew.reset(std::log2(750.f));
        hpCutoffSlew.reset(std::log2(120.f));
        lpResSlew.reset(0.25f);
        hpResSlew.reset(0.25f);
    }
};

// Manages an array of VoiceEngine* for polyphonic use.
//
// All 16 voices are allocated up front (in the constructor) and never freed
// until destruction — setVoices() only changes activeVoices. This keeps the
// audio thread allocation-free and guarantees every voice inherits the pool's
// sample rate, even voices that become active after setSampleRate() is called.
struct EnginePool {
    VoiceEngine* engines[16] = {};
    int activeVoices = 0;
    float sampleRate = 44100.f;

    EnginePool() {
        for (int i = 0; i < 16; i++) engines[i] = new VoiceEngine();
        activeVoices = 1;
    }

    ~EnginePool() {
        for (int i = 0; i < 16; i++) {
            delete engines[i];
            engines[i] = nullptr;
        }
    }

    void setVoices(int n) {
        activeVoices = std::clamp(n, 1, 16);
    }

    void setSampleRate(float fs) {
        sampleRate = fs;
        for (int i = 0; i < 16; i++)
            if (engines[i]) engines[i]->setSampleRate(fs);
    }

    void resetAll() {
        for (int i = 0; i < 16; i++)
            if (engines[i]) engines[i]->reset();
    }

    // Process one voice (L channel only) through the HP→LP cascade.
    // Returns LP output in VCV scale (±5 V).
    // lpCutoffHz / lpRes and hpCutoffHz / hpRes are already-computed values
    // (not targets); caller is responsible for slew if desired.
    float processVoice(int c, float inVolts, float inRVolts,
                       float lpCutoffHz, float lpRes,
                       float hpCutoffHz, float hpRes) {
        VoiceEngine* e = engines[c];
        if (!e) return 0.f;
        auto hpOut  = e->hpFilter.processVCV(inVolts,  hpCutoffHz, hpRes);
        auto lpOut  = e->lpFilter.processVCV(hpOut.hp, lpCutoffHz, lpRes);
        (void)inRVolts;  // R channel not tested here
        return lpOut.lp;
    }
};
