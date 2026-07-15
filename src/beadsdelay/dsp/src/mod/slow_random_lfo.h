#pragma once
#include "random/random.h"
#include "util/dsp_utils.h"

namespace beadsdelay_dsp {

// Two-point cosine-interpolated random, advanced at block rate.
class SlowRandomLfo {
public:
    void Init(particules_dsp::Random* rng, uint32_t salt) {
        rng_ = rng; salt_ = salt; from_ = 0.f; to_ = 0.f; phase_ = 1.f;
    }
    void SetRate(float hz, float sample_rate) { rate_hz_ = hz; sr_ = sample_rate; }
    // advance by n samples, return value in [-1, 1]
    float Next(size_t n) {
        phase_ += rate_hz_ / sr_ * n;
        if (phase_ >= 1.f) { phase_ -= (int)phase_; from_ = to_; to_ = rng_->NextBipolar(); }
        float t = 0.5f - 0.5f * std::cos(phase_ * particules_dsp::kPi); // block rate: std::cos OK
        return from_ + (to_ - from_) * t;
    }
private:
    particules_dsp::Random* rng_ = nullptr;
    uint32_t salt_ = 0;
    float from_ = 0.f, to_ = 0.f, phase_ = 1.f, rate_hz_ = 0.15f, sr_ = 48000.f;
};

} // namespace beadsdelay_dsp
