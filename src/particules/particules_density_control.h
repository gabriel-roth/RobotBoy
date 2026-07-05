#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace particules {

// Slow (non-audio-rate) density CV mapping: ±5 V → ±1.0 density offset.
inline float ComputeSlowDensityOffset(float conditioned_density_cv) {
    return conditioned_density_cv * 0.2f;
}

}  // namespace particules
