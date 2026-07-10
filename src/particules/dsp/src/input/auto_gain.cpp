#include "auto_gain.h"
#include "../util/dsp_utils.h"

#include <cmath>

namespace beads {

// Fast dB-to-linear approximation using exp2.
// pow(10, db/20) = exp2(db * log2(10)/20) = exp2(db * 0.16609640474)
static inline float FastDbToGain(float db) {
    float x = db * 0.16609640474f;
    if (x < -20.0f) return 0.0f;
    if (x > 20.0f) x = 20.0f;
    return std::exp2(x);
}

// Fast linear-to-dB using logf.
// 20 * log10(x) = 20 * ln(x) / ln(10) = ln(x) * 8.6858896381
static inline float FastGainToDb(float gain) {
    if (gain < 1e-10f) return -200.0f;
    return logf(gain) * 8.6858896381f;
}

// Soft limiter: unity gain below threshold, tanh compression above.
// Threshold at ~-2dB (0.8), asymptotic ceiling at 0dB (1.0).
static inline float SoftLimit(float x) {
    constexpr float kThreshold = 0.8f;
    constexpr float kHeadroom = 1.0f - kThreshold;

    float ax = std::abs(x);
    if (ax <= kThreshold) return x;

    float sign = (x > 0.0f) ? 1.0f : -1.0f;
    float excess = ax - kThreshold;
    return sign * (kThreshold + kHeadroom * SoftClip(excess / kHeadroom));
}

// Hoisted to file scope: a function-local `static const` needs a
// thread-safety guard check on every call (dynamic init on first use only,
// but the compiler can't prove that here); initializing once at load time
// keeps this off the audio-thread hot path in the kLocked branch below.
static const float kSilenceGain = FastDbToGain(AutoGain::kMinGainDb);

void AutoGain::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    envelope_ = 0.0f;
    gain_ = 1.0f;
    target_gain_ = 1.0f;
    locked_gain_ = 1.0f;
    last_gain_db_ = 0.0f;
    state_ = State::kDisabled;
    calibration_counter_ = 0;
    silence_samples_ = 0;

    // Fast attack: ~1ms time constant
    attack_coeff_ = 1.0f - std::exp(-1.0f / (0.001f * sample_rate_));

    // ~500ms release for calibration measurement
    release_coeff_ = 1.0f - std::exp(-1.0f / (0.5f * sample_rate_));

    // ~2 second calibration window
    calibration_samples_ = static_cast<int>(2.0f * sample_rate_);

    // ~10 second silence threshold window
    silence_threshold_samples_ = static_cast<int>(10.0f * sample_rate_);
}

void AutoGain::StartCalibration() {
    state_ = State::kCalibrating;
    calibration_counter_ = calibration_samples_;
    silence_samples_ = 0;
    // Don't reset envelope — keep whatever level info we already have
}

StereoFrame AutoGain::Process(StereoFrame input, float manual_gain_db, bool auto_gain_on) {
    // Peak detection (always runs for the input level meter)
    float abs_l = std::abs(input.l);
    float abs_r = std::abs(input.r);
    if (std::isnan(abs_l)) abs_l = 0.0f;
    if (std::isnan(abs_r)) abs_r = 0.0f;
    float peak = std::max(abs_l, abs_r);

    // Envelope follower: fast attack, moderate release
    if (peak > envelope_) {
        OnePole(envelope_, peak, attack_coeff_);
    } else {
        OnePole(envelope_, peak, release_coeff_);
    }

    if (!auto_gain_on) {
        // Manual gain mode
        state_ = State::kDisabled;
        silence_samples_ = 0;
        float gain_db = Clamp(manual_gain_db, kMinGainDb, kMaxGainDb);
        if (gain_db != last_gain_db_) {
            last_gain_db_ = gain_db;
            target_gain_ = FastDbToGain(gain_db);
        }
        OnePole(gain_, target_gain_, 0.0001f);
        StereoFrame out = input * gain_;
        return {SoftLimit(out.l), SoftLimit(out.r)};
    }

    // Auto-gain mode
    if (state_ == State::kCalibrating) {
        // Track input level and compute gain in real-time during calibration
        // Target -6dB (kTargetHeadroomDb) to leave headroom for transient peaks
        float input_db = FastGainToDb(envelope_);
        float gain_db = Clamp(-input_db - kTargetHeadroomDb, kMinGainDb, kMaxGainDb);
        if (gain_db != last_gain_db_) {
            last_gain_db_ = gain_db;
            target_gain_ = FastDbToGain(gain_db);
        }
        OnePole(gain_, target_gain_, 0.001f);

        calibration_counter_--;
        if (calibration_counter_ <= 0) {
            // Lock the current gain
            locked_gain_ = gain_;
            state_ = State::kLocked;
        }
    } else if (state_ == State::kLocked) {
        // Check for silence -> sound transition
        bool is_silent = peak < kSilenceGain;

        if (is_silent) {
            if (silence_samples_ < silence_threshold_samples_) {
                silence_samples_++;
            }
        } else {
            // Sound detected - recalibrate if we were silent for 10+ seconds
            if (silence_samples_ >= silence_threshold_samples_) {
                StartCalibration();
            }
            silence_samples_ = 0;
        }

        // Smoothly hold locked gain
        OnePole(gain_, locked_gain_, 0.01f);
    } else {
        // State::kDisabled but auto_gain_on — first enable, start calibrating
        StartCalibration();
        OnePole(gain_, target_gain_, 0.001f);
    }

    StereoFrame out = input * gain_;
    return {SoftLimit(out.l), SoftLimit(out.r)};
}

float AutoGain::InputLevel() const {
    return envelope_;
}

float AutoGain::GainDb() const {
    return FastGainToDb(gain_);
}

} // namespace beads
