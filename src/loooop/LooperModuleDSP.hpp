#pragma once

#include <cmath>

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

}  // namespace loooop
