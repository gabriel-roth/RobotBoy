#pragma once

#include <cmath>
#include "dsp/LoopEngine.hpp"

namespace loooop {

constexpr float kLinearSpeedMin = -2.0f;
constexpr float kLinearSpeedMax = 2.0f;
constexpr float kVOctSpeedMin = -16.0f;
constexpr float kVOctSpeedMax = 16.0f;

// NaN-suppressing clamp, matching rack::clamp (fmax/fmin based): a non-finite
// v lands on a bound instead of propagating. The engine's param setters rely
// on this — a NaN speed CV must not reach PlayHead::pos. This intentionally
// differs from the codex source, which used std::clamp (NaN-propagating) and
// so regressed the modules' original unqualified (rack::) clamp behavior.
inline float clampSafe(float v, float lo, float hi) {
    return std::fmax(lo, std::fmin(v, hi));
}

inline float speedFromControls(float knob, float cvVolts) {
    return clampSafe(knob + cvVolts * 0.4f, kLinearSpeedMin, kLinearSpeedMax);
}

inline float speedFromVOct(float knob, float cvVolts) {
    const float octave = clampSafe(cvVolts, -5.0f, 5.0f);
    return clampSafe(knob * std::exp2(octave), kVOctSpeedMin, kVOctSpeedMax);
}

// Per-head memo for speedFromVOct: exp2f is a libm call on MetaModule; the
// (knob, cv) pair is static or slow-moving in practice. Exactly equivalent.
struct VOctSpeedMemo {
    float knob = -1e9f, cv = -1e9f, out = 0.f;
    float get(float k, float c) {
        if (k != knob || c != cv) { knob = k; cv = c; out = speedFromVOct(k, c); }
        return out;
    }
};

inline float normalizedControl(float knob, float cvVolts) {
    return clampSafe(knob + cvVolts * 0.1f, 0.0f, 1.0f);
}

inline float panControl(float knob, float cvVolts) {
    return clampSafe(knob + cvVolts * 0.2f, -1.0f, 1.0f);
}

struct StereoInput { float l, r; };

inline StereoInput normalledStereo(bool leftConnected, float left,
                                   bool rightConnected, float right) {
    return {
        leftConnected ? left : right,
        rightConnected ? right : left,
    };
}

inline float dryWet(float dry, float wet, float mix) {
    return dry * (1.0f - mix) + wet * mix;
}

inline float panLeftGain(float pan) {
    return pan <= 0.0f ? 1.0f : 1.0f - pan;
}

inline float panRightGain(float pan) {
    return pan >= 0.0f ? 1.0f : 1.0f + pan;
}

// Grid menu choice (0..5: Off/4/8/16/32/64) -> LoopEngine::setGrid segment count.
inline int gridSegments(int choiceIdx) {
    constexpr int kGridChoices[6] = {0, 4, 8, 16, 32, 64};
    return (choiceIdx < 0 || choiceIdx > 5) ? 0 : kGridChoices[choiceIdx];
}

// Engine write mode for an overdub choice index, in the 5-state Overdub order
// (Layer, Decay, Add, Replace). Index 4 (Lock) is never passed here — while
// Locked the hosts leave the last write mode set (see OverdubControl.hpp);
// out-of-range indices fall back to Layer.
// Shared so VCV's Overdub button and MetaModule's Write-mode alt-param
// map a given index to the same behavior — the write-mode analogue of
// gridSegments. Both hosts default to index 0 = Layer (MM's alt-param loader
// zero-inits unset params, so index 0 is the fresh/legacy default).
inline LoopEngine::WriteMode overdubWriteMode(int choiceIdx) {
    constexpr LoopEngine::WriteMode kModes[4] = {
        LoopEngine::WriteMode::Layer, LoopEngine::WriteMode::Decay,
        LoopEngine::WriteMode::Add,   LoopEngine::WriteMode::Replace};
    return kModes[(choiceIdx < 0 || choiceIdx > 3) ? 0 : choiceIdx];
}

// Overdub state colors (Layer/Decay/Add/Replace/Lock), shared by VCV's Overdub
// LED bezel and the MetaModule cores' RGB button. Kept here (Rack-free) so both
// hosts index the same table.
// Fully-saturated primaries/secondaries (channels snapped to 0/0.5/1), matching
// the Particules Quality button. Mixed sub-channel values wash an RGB LED out and
// blur the states together; clean saturated hues read distinctly on the bezel.
static constexpr float kOverdubColors[5][3] = {
    {0.f, 0.5f, 1.f},   // Layer   - blue
    {1.f, 0.5f, 0.f},   // Decay   - orange
    {0.f, 1.f, 0.f},    // Add     - green
    {1.f, 0.f, 0.f},    // Replace - red
    {1.f, 0.f, 1.f},    // Lock    - magenta
};

// Apply the 5-state Overdub index to the engine: 0..3 = write modes
// (Layer/Decay/Add/Replace), 4 = Lock (overdub off, loop untouchable).
inline void applyOverdub(LoopEngine& engine, int od) {
    engine.setOverdub(od != 4);   // 4 = Lock
    if (od >= 0 && od < 4)
        engine.setWriteMode(overdubWriteMode(od));
}

// One-pole smoother for zipper-noise suppression (mirrors mf20/dsp_utils.hpp
// so the loooop headless lane stays self-contained). alpha 1 = passthrough.
struct OnePoleSmoother {
    float value = 0.f;
    float alpha = 1.f;
    float process(float target) { value += alpha * (target - value); return value; }
    void reset(float v) { value = v; }
};

// One-pole alpha for a time constant (seconds); saturates to 1 at low rates.
inline float smootherAlpha(float sampleRate, float tauSec) {
    return 1.f - std::exp(-1.f / (tauSec * sampleRate));
}

}  // namespace loooop
