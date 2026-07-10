#pragma once

#include "../../include/beads/types.h"

namespace beads {

class AutoGain {
public:
    void Init(float sample_rate);

    // Process a stereo frame, returns the gain-adjusted frame.
    // auto_gain_on: true = calibrate-and-lock mode, false = manual gain.
    StereoFrame Process(StereoFrame input, float manual_gain_db, bool auto_gain_on);

    // Signal that auto-gain was just toggled on — start fresh calibration.
    void StartCalibration();

    // Get current detected input level (0-1)
    float InputLevel() const;

    // Get current gain in dB (valid when auto-gain is active)
    float GainDb() const;

    // Public so auto_gain.cpp can hoist derived constants (e.g. kSilenceGain)
    // to file scope instead of recomputing them behind a function-local
    // static's init-guard check on every call.
    static constexpr float kMinGainDb = -60.0f;
    static constexpr float kMaxGainDb = 32.0f;
    static constexpr float kTargetHeadroomDb = 8.0f;  // Leave headroom for peaks

private:
    enum class State { kDisabled, kCalibrating, kLocked };

    float sample_rate_ = 48000.0f;
    float envelope_ = 0.0f;        // Peak envelope follower
    float gain_ = 1.0f;            // Current applied gain
    float target_gain_ = 1.0f;
    float locked_gain_ = 1.0f;     // Snapshot when entering kLocked
    float last_gain_db_ = 0.0f;    // Cache to skip DbToGain when unchanged

    State state_ = State::kDisabled;
    int calibration_counter_ = 0;

    // Timing
    float attack_coeff_ = 0.0f;     // Fast attack (~1ms)
    float release_coeff_ = 0.0f;    // ~500ms release for calibration
    int calibration_samples_ = 0;   // ~1 second of calibration
    int silence_samples_ = 0;           // Counter for consecutive silent samples
    int silence_threshold_samples_ = 0; // 10 seconds worth of samples
};

} // namespace beads
