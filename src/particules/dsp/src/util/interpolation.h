#pragma once

namespace beads {

// Linear interpolation between two samples
inline float InterpolateLinear(float y0, float y1, float frac) {
    return y0 + frac * (y1 - y0);
}

// Hermite cubic interpolation (4-point, 3rd order)
// Provides smooth, overshoot-free interpolation ideal for audio
// y_1 = sample at position -1
// y0  = sample at position 0
// y1  = sample at position 1
// y2  = sample at position 2
// frac = fractional position between y0 and y1 [0, 1)
inline float InterpolateHermite(float y_1, float y0, float y1, float y2, float frac) {
    float c0 = y0;
    float c1 = 0.5f * (y1 - y_1);
    float c2 = y_1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    float c3 = 0.5f * (y2 - y_1) + 1.5f * (y0 - y1);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

} // namespace beads
