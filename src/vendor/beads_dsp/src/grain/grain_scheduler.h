#pragma once

#include <cstddef>
#include <cstdint>
#include "../../include/beads/types.h"
#include "../../include/beads/parameters.h"
#include "../random/random.h"

namespace beads {

class GrainScheduler {
public:
    void Init(float sample_rate);

    // Call once per block to compute trigger points.
    // Returns the number of triggers in this block, fills trigger_samples[]
    // with the sample offsets within the block where grains should start.
    int Process(const BeadsParameters& params, size_t block_size,
                int* trigger_samples, int max_triggers);

    bool GrainTriggeredThisBlock() const { return grain_triggered_; }

private:
    static constexpr float kMaxRateHz = 80.0f;

    float DensityToRate(float density);

    float sample_rate_ = 48000.0f;

    // Latched mode: internal phasor
    float latched_phase_ = 0.0f;

    // Gated mode
    bool prev_gate_ = false;
    float gate_phase_ = 0.0f;

    // Clocked mode
    bool prev_clock_ = false;
    float clock_period_ = 0.0f;   // Estimated period between clocks
    uint32_t samples_since_clock_ = 0;

    Random random_;
    bool grain_triggered_ = false;
};

} // namespace beads
