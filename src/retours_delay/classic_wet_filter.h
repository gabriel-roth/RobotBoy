#pragma once
#include "retours_delay_dsp/retours_dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/units.h"
#include <algorithm>
#include <cmath>

// Classic Retours' Filter: Beads' delay-mode bandwidth filter
// (beads/dsp/multitap_delay.h) on the v2.1 engine's wet signal. Beads states
// its cutoff relative to its 48 kHz engine rate; this converts it to absolute
// Hz so the filter sounds the same at any host rate.
namespace retours_classic {

class BandwidthFilter : public retours_delay_dsp::WetFilter {
public:
    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        for (auto& f : filter_) f.Init();
        SetBandwidth(0.5f);
    }

    // 0..1, noon (0.5) off; low-pass below, high-pass above. Once per block.
    void SetBandwidth(float bandwidth) {
        bandwidth_ = std::clamp(bandwidth, 0.f, 1.f);
        const float amount = std::fabs(2.f * bandwidth_ - 1.f);
        const float wet_sqrt = amount * (2.f - amount);
        wet_amount_ = wet_sqrt * (2.f - wet_sqrt);
        if (wet_amount_ <= 0.f) return;
        // LP: -7 to -2 octaves, HP: -8 to -3 octaves below 0.49 x 48 kHz.
        const float octave = bandwidth_ < 0.5f ? -7.f + bandwidth_ * 10.f
                                               : -8.f + (bandwidth_ - 0.5f) * 10.f;
        const float f48 = 0.49f * std::exp2(octave);
        const float q = 1.f + 16.f * amount * f48;
        const float f = std::min(f48 * 48000.f / sample_rate_, 0.49f);
        filter_[0].set_f_q<stmlib::FREQUENCY_ACCURATE>(f, q);
        filter_[1].set(filter_[0]);
    }

    bool active() const { return wet_amount_ > 0.f; }

    void Process(retours_delay_dsp::StereoFrame& s) override {
        if (!active()) return;
        float l, r;
        if (bandwidth_ < 0.5f) {
            l = filter_[0].Process<stmlib::FILTER_MODE_LOW_PASS>(s.l);
            r = filter_[1].Process<stmlib::FILTER_MODE_LOW_PASS>(s.r);
        } else {
            l = filter_[0].Process<stmlib::FILTER_MODE_HIGH_PASS>(s.l);
            r = filter_[1].Process<stmlib::FILTER_MODE_HIGH_PASS>(s.r);
        }
        s.l += (l - s.l) * wet_amount_;
        s.r += (r - s.r) * wet_amount_;
    }

private:
    stmlib::Svf filter_[2];
    float sample_rate_ = 48000.f;
    float bandwidth_ = 0.5f;
    float wet_amount_ = 0.f;
};

}  // namespace retours_classic
