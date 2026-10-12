#include "grain.h"

#include <cmath>
#include <algorithm>

namespace particules_dsp {

void Grain::Init() {
    active_ = false;
    pending_kill_ = false;
    spawn_serial_ = 0;
    kill_deadline_ = 0;
    fallback_fade_ = false;
    fallback_counter_ = 0;
    prev_mono_ = 0.0f;
    position_q_ = 0;
    increment_q_ = 0;
    envelope_phase_ = 0.0f;
    envelope_increment_ = 0.0f;
    smoothness_ = 1.0f;
    slope_ = 0.5f;
    steepness_ = 1.0f;
    inv_slope_ = 2.0f;
    inv_one_minus_slope_ = 2.0f;
    gain_ = 1.0f;
    pan_l_ = 1.0f;
    pan_r_ = 1.0f;
    pre_delay_ = 0;
}

void Grain::StartPendingKill() {
    if (active_ && !pending_kill_) {
        pending_kill_ = true;
        kill_deadline_ = kZeroCrossDeadline;
        fallback_fade_ = false;
        fallback_counter_ = 0;
    }
}

void Grain::Start(const GrainParameters& params) {
    active_ = true;
    pending_kill_ = false;
    kill_deadline_ = 0;
    fallback_fade_ = false;
    fallback_counter_ = 0;
    prev_mono_ = 0.0f;

    position_q_ = static_cast<int64_t>(static_cast<double>(params.position) * kQ32One);
    increment_q_ = static_cast<int64_t>(static_cast<double>(params.pitch_ratio) * kQ32One);

    // Envelope increments once per sample over the grain's duration.
    // Phase goes from 0 to 1 over params.size samples.
    if (params.size > 0.0f) {
        envelope_increment_ = 1.0f / params.size;
    } else {
        envelope_increment_ = 1.0f;
    }
    envelope_phase_ = 0.0f;

    // Precompute envelope shape parameters (constant for grain lifetime).
    // Morphs through 4 shapes matching the hardware Beads manual:
    //   0.0:  Rectangle      (clicky, full amplitude)
    //   0.33: Snappy decay   (fast attack, slow release)
    //   0.66: Smooth bell    (symmetric Hann window)
    //   1.0:  Reversed       (slow attack, fast release)
    //
    // Three zones with piecewise-linear interpolation of slope, smoothness,
    // and steepness between these waypoints.
    float shape = Clamp(params.shape, 0.0f, 1.0f);
    constexpr float kOneThird = 1.0f / 3.0f;
    constexpr float kTwoThirds = 2.0f / 3.0f;

    if (shape < kOneThird) {
        // Zone 1: Rectangle → Snappy Decay
        float t = shape * 3.0f;
        smoothness_ = 0.0f;
        slope_ = 0.5f - t * 0.45f;     // 0.50 → 0.05
        steepness_ = 20.0f - t * 18.0f; // 20 → 2
    } else if (shape < kTwoThirds) {
        // Zone 2: Snappy Decay → Smooth Bell
        float t = (shape - kOneThird) * 3.0f;
        smoothness_ = t;                // 0 → 1
        slope_ = 0.05f + t * 0.45f;    // 0.05 → 0.50
        steepness_ = 2.0f - t;          // 2 → 1
    } else {
        // Zone 3: Smooth Bell → Reversed
        float t = (shape - kTwoThirds) * 3.0f;
        smoothness_ = 1.0f;
        slope_ = 0.5f + t * 0.4f;      // 0.50 → 0.90
        steepness_ = 1.0f;
    }

    // Clamp slope to safe range for reciprocal computation.
    slope_ = Clamp(slope_, 0.005f, 0.995f);
    inv_slope_ = 1.0f / slope_;
    inv_one_minus_slope_ = 1.0f / (1.0f - slope_);

    // Equal-power panning.
    // pan: -1 (full left) to +1 (full right), 0 = center.
    float p = Clamp(params.pan * 0.5f + 0.5f, 0.0f, 1.0f);  // map to 0..1
    // pan_l = cos(p*pi/2), pan_r = sin(p*pi/2) = cos(p*pi/2 - pi/2).
    // CosLookup(x) = cos(2*pi*x), so cos(p*pi/2) = CosLookup(p*0.25) and
    // cos(p*pi/2 - pi/2) = CosLookup(p*0.25 - 0.25). Table-lerped, so this
    // differs from libm by ~1e-4 -- fine for a per-spawn equal-power pan.
    pan_l_ = CosLookup(p * 0.25f);
    pan_r_ = CosLookup(p * 0.25f - 0.25f);

    gain_ = params.gain;
    pre_delay_ = std::max(params.pre_delay, 0);
}

} // namespace particules_dsp
