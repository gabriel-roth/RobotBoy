#pragma once

#include <cmath>
#include "dsp_utils.h"

namespace beads {

// State Variable Filter - provides simultaneous LP, HP, BP outputs
class StateVariableFilter {
public:
    void Init() {
        state1_ = 0.0f;
        state2_ = 0.0f;
        UpdateCoefficient();
    }

    // Set frequency as normalized coefficient (0 to 1, where 1 = Nyquist)
    void SetFrequency(float frequency) {
        g_ = std::tan(kPi * Clamp(frequency, 0.0001f, 0.4999f));
        UpdateCoefficient();
    }

    // Set frequency from Hz and sample rate
    void SetFrequencyHz(float hz, float sample_rate) {
        if (sample_rate <= 0.0f) return;
        SetFrequency(hz / sample_rate);
    }

    // Set Q (resonance), 0.5 = no resonance, higher = more resonant
    void SetQ(float q) {
        r_ = 1.0f / Clamp(q, 0.5f, 20.0f);
        UpdateCoefficient();
    }

    // Process one sample, returns low-pass output
    float ProcessLP(float input) {
        return Tick(input).lp;
    }

    // Process one sample, returns high-pass output
    float ProcessHP(float input) {
        return Tick(input).hp;
    }

    // Process one sample, returns band-pass output
    float ProcessBP(float input) {
        return Tick(input).bp;
    }

    void Reset() {
        state1_ = 0.0f;
        state2_ = 0.0f;
    }

private:
    struct Output { float lp, hp, bp; };

    void UpdateCoefficient() {
        denominator_recip_ = 1.0f / (1.0f + g_ * (r_ + g_));
    }

    Output Tick(float input) {
        const float hp = (input - (r_ + g_) * state1_ - state2_) * denominator_recip_;
        const float bp = g_ * hp + state1_;
        const float lp = g_ * bp + state2_;
        state1_ = g_ * hp + bp;
        state2_ = g_ * bp + lp;
        return {lp, hp, bp};
    }

    float g_ = 0.0f;
    float r_ = 1.0f;  // 1/Q, default Q=1 (Butterworth-ish)
    float denominator_recip_ = 1.0f;
    float state1_ = 0.0f;
    float state2_ = 0.0f;
};

} // namespace beads
