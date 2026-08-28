#pragma once
#include <algorithm>

// Plain linear knob-to-semitone mapping for Ondes' pitch knob. Unlike
// Particules'/Retours' pitchKnobToSemitones (pitch_notch_map.hpp), Ondes has
// no notch stretching around octave/fifth/unison landmarks -- turning the
// knob moves pitch at a constant rate across the full +-24 semitone span.
inline float ondesKnobToSemitones(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return (t - 0.5f) * 48.0f;
}

inline float ondesSemitonesToKnob(float st) {
    st = std::max(-24.0f, std::min(24.0f, st));
    return st / 48.0f + 0.5f;
}
