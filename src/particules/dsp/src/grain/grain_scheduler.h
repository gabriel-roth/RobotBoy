#pragma once

#include <cstddef>
#include <cstdint>
#include "../../include/particules_dsp/types.h"
#include "../../include/particules_dsp/parameters.h"
#include "../random/random.h"

namespace particules_dsp {

class GrainScheduler {
public:
    void Init(float sample_rate);

    // Call once per block to compute trigger points.
    // Returns the number of triggers in this block, fills trigger_samples[]
    // with the sample offsets within the block where grains should start.
    int Process(const ParticulesParameters& params, size_t block_size,
                int* trigger_samples, int max_triggers);

    bool GrainTriggeredThisBlock() const { return grain_triggered_; }

private:
    static constexpr float kMaxRateHz = 80.0f;
    // Exponent for the density→rate mapping in DensityToRate(); std::log2 is
    // not constexpr, so this is defined out-of-line in the .cpp file.
    static const float kRateExponent;

    float DensityToRate(float density);

    float sample_rate_ = 48000.0f;

    // Trigger mode as of the last Process() call, so a mode change can be
    // detected and the mode-specific timing state below reset (see
    // Process()). Defaults to kLatched to match both ParticulesParameters'
    // default and Init()'s all-zero state, so the very first call never
    // spuriously looks like a "mode change".
    TriggerMode prev_trigger_mode_ = TriggerMode::kLatched;

    // Latched mode: internal phasor
    float latched_phase_ = 0.0f;

    // Gated mode. gate_phase_ is also reused as the kClocked clock-
    // division counter and the kMidi repeat-rate phasor — see the
    // mode-change reset in Process().
    bool prev_gate_ = false;
    float gate_phase_ = 0.0f;

    // Clocked mode
    bool prev_clock_ = false;

    Random random_;
    bool grain_triggered_ = false;
};

} // namespace particules_dsp
