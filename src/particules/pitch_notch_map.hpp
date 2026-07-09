#pragma once
#include <algorithm>
#include <cstddef>

// Piecewise-linear mapping between knob position [0,1] and semitones [-24,24].
// Seven notch positions (±19, ±12, ±7, 0) each receive 3× the knob range of
// a normal semitone gap, making them easier to land on.
//
// Weight accounting:
//   41 normal gaps × 1 + 7 notch zones × 3 = 41 + 21 = 62 total units
//
struct PitchMapPoint {
    float t;   // knob position [0, 1]
    float st;  // semitones [-24, 24]
};

inline constexpr PitchMapPoint kPitchMap[] = {
    { 0.0f  / 62.0f, -24.0f },  // 0
    { 4.5f  / 62.0f, -19.5f },  // 1
    { 7.5f  / 62.0f, -18.5f },  // 2
    { 13.5f / 62.0f, -12.5f },  // 3
    { 16.5f / 62.0f, -11.5f },  // 4
    { 20.5f / 62.0f,  -7.5f },  // 5
    { 23.5f / 62.0f,  -6.5f },  // 6
    { 29.5f / 62.0f,  -0.5f },  // 7
    { 32.5f / 62.0f,   0.5f },  // 8
    { 38.5f / 62.0f,   6.5f },  // 9
    { 41.5f / 62.0f,   7.5f },  // 10
    { 45.5f / 62.0f,  11.5f },  // 11
    { 48.5f / 62.0f,  12.5f },  // 12
    { 54.5f / 62.0f,  18.5f },  // 13
    { 57.5f / 62.0f,  19.5f },  // 14
    { 62.0f / 62.0f,  24.0f },  // 15
};

inline constexpr std::size_t kPitchMapSize = sizeof(kPitchMap) / sizeof(kPitchMap[0]);

// Map knob position t ∈ [0,1] → semitones ∈ [-24, 24].
inline float pitchKnobToSemitones(float t) {
    t = std::max(0.0f, std::min(1.0f, t));

    // Find the segment containing t
    std::size_t hi = 1;
    while (hi < kPitchMapSize - 1 && kPitchMap[hi].t < t) {
        ++hi;
    }
    std::size_t lo = hi - 1;

    float t0 = kPitchMap[lo].t;
    float t1 = kPitchMap[hi].t;
    float s0 = kPitchMap[lo].st;
    float s1 = kPitchMap[hi].st;

    float frac = (t - t0) / (t1 - t0);
    return s0 + frac * (s1 - s0);
}

// Map semitones st ∈ [-24, 24] → knob position ∈ [0, 1].
inline float semitonesToPitchKnob(float st) {
    st = std::max(-24.0f, std::min(24.0f, st));

    // Find the segment containing st
    std::size_t hi = 1;
    while (hi < kPitchMapSize - 1 && kPitchMap[hi].st < st) {
        ++hi;
    }
    std::size_t lo = hi - 1;

    float s0 = kPitchMap[lo].st;
    float s1 = kPitchMap[hi].st;
    float t0 = kPitchMap[lo].t;
    float t1 = kPitchMap[hi].t;

    float frac = (st - s0) / (s1 - s0);
    return t0 + frac * (t1 - t0);
}
