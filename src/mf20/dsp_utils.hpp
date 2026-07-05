#pragma once
#include <cmath>

// One-pole smoother (IIR lowpass for zipper-noise suppression)
struct OnePoleSmoother {
    float value;
    float alpha;
    explicit OnePoleSmoother(float init, float a = 0.0045f) : value(init), alpha(a) {}
    void setAlpha(float a) { alpha = a; }
    float process(float target) { value += alpha * (target - value); return value; }
    void reset(float v) { value = v; }
};

// Compute one-pole alpha for a given time constant (seconds) and sample rate.
inline float smootherAlpha(float sampleRate, float tauSec) {
    return 1.f - std::exp(-1.f / (tauSec * sampleRate));
}

// Quadratic resonance taper: maps [0,1] → [0,1.025], adds slight curve.
inline float resTaper(float r) {
    return (2.f * r - r * r) * 1.025f;
}
