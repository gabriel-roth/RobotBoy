#pragma once
#include <cmath>
#include <cstddef>
#include "slow_random_lfo.h"

namespace retours_delay_dsp {

// Delay-flavor attenurandomizer (continuous, block rate).
struct ArModulator {
    SlowRandomLfo lfo;
    // returns modulation in normalized units (caller scales)
    float Process(float ar, float cv_norm, bool cv_connected, size_t block_frames) {
        // Always advance the LFO so its phase stays continuous even when
        // this block's result is discarded (noon, or the CV-direct branch
        // at the processor level).
        float l = lfo.Next(block_frames);
        if (ar == 0.f) return 0.f;   // noon: nothing
        if (cv_connected)
            return ar > 0.f ? ar * cv_norm : (-ar) * l * std::fabs(cv_norm);
        return ar > 0.f ? ar * l : (-ar) * (l * l * l);   // uniform vs peaky
    }
};

} // namespace retours_delay_dsp
