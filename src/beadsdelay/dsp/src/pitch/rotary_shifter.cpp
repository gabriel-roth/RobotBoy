#include "rotary_shifter.h"
#include <algorithm>
#include <cmath>

namespace beadsdelay_dsp {

namespace {
// Matches the processor's |semitones| < kShifterBypassSemitones bypass
// window, expressed as a ratio distance from 1.0 (2^(-0.25/12) - 1).
constexpr float kRatioEpsilon = 0.01434f;
constexpr float kBypassRampStep = 1.f / 256.f;
constexpr size_t kShifterMask = kShifterSize - 1;

// Fractional read at ring position (write - d), d in [0, kShifterSize).
// write_ + kShifterSize - d is always positive (d < kShifterSize), so the
// mask is applied only after truncating to a (positive) integer index.
inline float ReadInterp(const float* buf, size_t write, float d) {
    float base = static_cast<float>(write) + static_cast<float>(kShifterSize) - d;
    size_t idx0 = static_cast<size_t>(base);
    float frac = base - static_cast<float>(idx0);
    size_t i0 = idx0 & kShifterMask;
    size_t i1 = (i0 + 1) & kShifterMask;
    return buf[i0] * (1.f - frac) + buf[i1] * frac;
}
}  // namespace

void RotaryShifter::Init(float sample_rate) {
    (void)sample_rate;  // phase_inc_ is ratio-derived, independent of rate.
    for (size_t i = 0; i < kShifterSize; ++i) {
        buf_l_[i] = 0.f;
        buf_r_[i] = 0.f;
    }
    write_ = 0;
    phase_ = 0.f;
    phase_inc_ = 0.f;
    bypass_ = true;
    bypass_xfade_ = 1.f;
}

void RotaryShifter::SetRatio(float ratio) {
    phase_inc_ = (1.f - ratio) / static_cast<float>(kShifterSize);
    bypass_ = std::fabs(1.f - ratio) < kRatioEpsilon;
}

bool RotaryShifter::Bypassed() const { return bypass_; }

StereoFrame RotaryShifter::Process(StereoFrame in) {
    // Ramp the dry/shifted crossfade toward the current bypass target.
    float target = bypass_ ? 1.f : 0.f;
    if (bypass_xfade_ < target) {
        bypass_xfade_ = std::min(target, bypass_xfade_ + kBypassRampStep);
    } else if (bypass_xfade_ > target) {
        bypass_xfade_ = std::max(target, bypass_xfade_ - kBypassRampStep);
    }

    // Fully bypassed and ramp settled: pass through untouched, skip writes.
    if (bypass_ && bypass_xfade_ >= 1.f) {
        return in;
    }

    // Triangular windows, two heads a half-period apart.
    float p1 = phase_;
    float p2 = phase_ + 0.5f;
    if (p2 >= 1.f) p2 -= 1.f;
    float w1 = 2.f * (p1 < 0.5f ? p1 : 1.f - p1);
    float w2 = 2.f * (p2 < 0.5f ? p2 : 1.f - p2);
    float d1 = p1 * static_cast<float>(kShifterSize - 4);
    float d2 = p2 * static_cast<float>(kShifterSize - 4);

    StereoFrame shifted;
    shifted.l = ReadInterp(buf_l_, write_, d1) * w1 + ReadInterp(buf_l_, write_, d2) * w2;
    shifted.r = ReadInterp(buf_r_, write_, d1) * w1 + ReadInterp(buf_r_, write_, d2) * w2;

    // Keep writing the ring whenever not fully bypassed (needed through the
    // disengage ramp too).
    buf_l_[write_] = in.l;
    buf_r_[write_] = in.r;
    write_ = (write_ + 1) & kShifterMask;

    phase_ += phase_inc_;
    if (phase_ >= 1.f) phase_ -= 1.f;
    else if (phase_ < 0.f) phase_ += 1.f;

    StereoFrame out;
    out.l = in.l * bypass_xfade_ + shifted.l * (1.f - bypass_xfade_);
    out.r = in.r * bypass_xfade_ + shifted.r * (1.f - bypass_xfade_);
    return out;
}

} // namespace beadsdelay_dsp
