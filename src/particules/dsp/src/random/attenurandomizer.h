#pragma once

#include "random.h"
#include "../util/dsp_utils.h"

namespace particules_dsp {

// Attenurandomizer: per-parameter CV/random modulation
// One instance per grain parameter (time, size, shape, pitch)
class Attenurandomizer {
public:
    void Init(Random* random) {
        random_ = random;
    }

    // Process one grain trigger, returning the modulated parameter value
    // base: the knob value (-1..1 for size, 0-1 for others, semitones for pitch)
    // ar_amount: attenurandomizer knob (-1 to +1, 0=noon)
    // cv: CV input value (volts)
    // cv_connected: whether a CV cable is patched
    float Process(float base, float ar_amount, float cv, bool cv_connected) {
        if (ar_amount == 0.0f || !random_) return base;

        float modulation = 0.0f;

        if (cv_connected) {
            if (ar_amount > 0.0f) {
                // CW from noon: CV attenuator
                modulation = ar_amount * cv;
            } else {
                // CCW from noon: CV-controlled randomization
                modulation = random_->NextGaussian() * (-ar_amount) * std::abs(cv);
            }
        } else {
            if (ar_amount > 0.0f) {
                // CW from noon: uniform random
                modulation = random_->NextBipolar() * ar_amount;
            } else {
                // CCW from noon: peaked random (clustered near center)
                modulation = random_->NextPeaked() * (-ar_amount);
            }
        }

        return base + modulation;
    }

private:
    Random* random_ = nullptr;
};

} // namespace particules_dsp
