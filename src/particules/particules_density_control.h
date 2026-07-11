#pragma once

namespace particules {

// Slow (non-audio-rate) density CV mapping: raw ±5 V → ±1.0 density offset.
inline float ComputeSlowDensityOffset(float density_cv_volts) {
    return density_cv_volts * 0.2f;
}

}  // namespace particules
