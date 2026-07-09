#pragma once

#include <algorithm>
#include <cmath>

namespace loooop {

constexpr float kLinearSpeedMin = -2.0f;
constexpr float kLinearSpeedMax = 2.0f;
constexpr float kVOctSpeedMin = -16.0f;
constexpr float kVOctSpeedMax = 16.0f;

inline float speedFromControls(float knob, float cvVolts) {
    return std::clamp(knob + cvVolts * 0.4f, kLinearSpeedMin, kLinearSpeedMax);
}

inline float speedFromVOct(float knob, float cvVolts) {
    const float octave = std::clamp(cvVolts, -5.0f, 5.0f);
    return std::clamp(knob * std::exp2(octave), kVOctSpeedMin, kVOctSpeedMax);
}

inline float normalizedControl(float knob, float cvVolts) {
    return std::clamp(knob + cvVolts * 0.1f, 0.0f, 1.0f);
}

inline float panControl(float knob, float cvVolts) {
    return std::clamp(knob + cvVolts * 0.2f, -1.0f, 1.0f);
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
