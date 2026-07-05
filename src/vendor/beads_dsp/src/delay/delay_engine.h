#pragma once

#include "../../include/beads/types.h"
#include "../../include/beads/parameters.h"

namespace beads {

class RecordingBuffer;

class DelayEngine {
public:
    void Init(float sample_rate, RecordingBuffer* buffer);

    void Process(const BeadsParameters& params, StereoFrame* output, size_t num_frames);

    // Set pitch modulation ratio (tape mode wow/flutter). 1.0 = no modulation.
    void SetPitchModulation(float ratio) { pitch_mod_ratio_ = ratio; }

    // Returns true if the delay cycle wrapped at least once during the last Process() call.
    // Used to drive the grain trigger output in delay mode.
    bool TriggerOutput() const { return delay_trigger_; }

private:
    float sample_rate_   = 48000.0f;
    int   stable_needed_ = 0;
    RecordingBuffer* buffer_ = nullptr;

    // Primary delay read position
    float read_position_ = 0.0f;

    // Secondary tap (golden ratio of primary)
    float secondary_tap_ratio_ = 1.6180339887f;  // Golden ratio

    // Pitch shifter (single read-head with crossfade reset)
    float head_drift_           = 0.0f;  // active head's offset from primary_pos (buf frames)
    float xfade_progress_       = 0.0f;  // 1.0 = xfade just started, 0.0 = not xfading
    float xfade_drift_at_start_ = 0.0f;  // head_drift_ value when current xfade began
    float xfade_length_inv_     = 0.0f;  // 1 / xfade_length (precomputed each xfade)
    float pitch_shift_increment_ = 0.0f;

    // Tempo-synced amplitude envelope (SHAPE as tremolo/slicer)
    float envelope_phase_ = 0.0f;

    // Freeze loop state
    bool frozen_ = false;
    float loop_start_ = 0.0f;
    float loop_length_ = 0.0f;

    // Smoothing
    float smoothed_envelope_gain_ = 1.0f;  // Smoothed slicer envelope to avoid hard edges
    float smoothed_secondary_mix_ = 0.0f;  // Smoothed secondary tap mix level

    // Tape mode wow/flutter pitch modulation (ratio, 1.0 = none)
    float pitch_mod_ratio_ = 1.0f;

    // Trigger output: fires once per delay cycle (one full repeat period).
    bool  delay_trigger_  = false;
    float trigger_phase_  = 0.0f;

    // Loop crossfade length (samples) to avoid click at loop boundary
    static constexpr int kLoopXfadeSamples = 64;

    // Maximum head drift before a crossfade reset is triggered.
    // Adaptive max_drift = min(kMaxHeadDrift, delay * 0.7), so the comb
    // resonance during crossfade = sample_rate / max_drift ≈ sub-bass for
    // delays >= ~150ms.
    static constexpr float kMaxHeadDrift = 8192.0f;

    // Smoothed base delay used in clocked mode (avoids abrupt jumps on subdivision snap)
    float clocked_base_delay_ = 0.0f;

    // DENSITY-only smoothing — replaces smoothed_delay_time_.
    // OnePole tracks base_delay (DENSITY-driven), preserving tape character.
    float smoothed_base_delay_ = 0.0f;

    // Dual-tap TIME crossfade (architecture from Veno-Echo, MIT).
    // Each tap holds a locked effective_time; DENSITY flows through
    // smoothed_base_delay_ and moves both taps proportionally.
    float tap_eff_time_[2]   = {0.0f, 0.0f}; // locked effective_time per tap
    float tap_gain_[2]        = {1.0f, 0.0f}; // current mix gain (active=1, inactive=0)
    int   active_tap_         = 0;
    float xfade_rate_         = 0.0f;         // gain step/sample, set in Init()
    bool  xfade_in_progress_  = false;

    // Approach A: stabilization timer (knob-only mode, from Veno-Echo timethresh)
    float time_last_seen_      = 0.0f;
    int   time_stable_counter_ = 0;
    bool  time_waiting_        = false;

    // Cold-start flag: true until first Process() call, to skip the stabilization
    // timer and snap tap_eff_time_ directly to the current TIME knob position.
    bool  cold_start_          = true;

    // Constants (values from Veno-Echo constants.h / DelayMulti.h)
    static constexpr float kXFadeTime          = 0.010f; // 10ms crossfade
    static constexpr float kStabilizeTime      = 0.025f; // 25ms stabilization timer
    static constexpr float kStabilizeThreshold = 0.001f; // 0.1% hysteresis (Veno-Echo: 0.001*delayLast)
    static constexpr float kCVChangeThreshold  = 0.005f; // 0.5% threshold for CV/AR mode

    // Cached exponential-range constant for delay-time mapping.
    // Recomputed only when the decimation factor changes.
    int   cached_df_ = -1;          // -1 forces recompute on first Process()
    float cached_min_delay_samples_ = 0.0f;
};

} // namespace beads
