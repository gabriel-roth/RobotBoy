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
    }

    // Set frequency as normalized coefficient (0 to 1, where 1 = Nyquist)
    void SetFrequency(float frequency) {
        g_ = std::tan(kPi * Clamp(frequency, 0.0001f, 0.4999f));
    }

    // Set frequency from Hz and sample rate
    void SetFrequencyHz(float hz, float sample_rate) {
        if (sample_rate <= 0.0f) return;
        SetFrequency(hz / sample_rate);
    }

    // Set Q (resonance), 0.5 = no resonance, higher = more resonant
    void SetQ(float q) {
        r_ = 1.0f / Clamp(q, 0.5f, 20.0f);
    }

    // Process one sample, returns low-pass output
    float ProcessLP(float input) {
        float hp = (input - (r_ + g_) * state1_ - state2_) / (1.0f + g_ * (r_ + g_));
        float bp = g_ * hp + state1_;
        float lp = g_ * bp + state2_;
        state1_ = g_ * hp + bp;
        state2_ = g_ * bp + lp;
        return lp;
    }

    // Process one sample, returns high-pass output
    float ProcessHP(float input) {
        float hp = (input - (r_ + g_) * state1_ - state2_) / (1.0f + g_ * (r_ + g_));
        float bp = g_ * hp + state1_;
        float lp = g_ * bp + state2_;
        state1_ = g_ * hp + bp;
        state2_ = g_ * bp + lp;
        return hp;
    }

    // Process one sample, returns band-pass output
    float ProcessBP(float input) {
        float hp = (input - (r_ + g_) * state1_ - state2_) / (1.0f + g_ * (r_ + g_));
        float bp = g_ * hp + state1_;
        float lp = g_ * bp + state2_;
        state1_ = g_ * hp + bp;
        state2_ = g_ * bp + lp;
        return bp;
    }

    void Reset() {
        state1_ = 0.0f;
        state2_ = 0.0f;
    }

private:
    float g_ = 0.0f;
    float r_ = 1.0f;  // 1/Q, default Q=1 (Butterworth-ish)
    float state1_ = 0.0f;
    float state2_ = 0.0f;
};

} // namespace beads
