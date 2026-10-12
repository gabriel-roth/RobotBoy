#pragma once
#include "../../include/retours_delay_dsp/types.h"

namespace retours_delay_dsp {

// Classic dual-head "rotary" pitch shifter (Clouds/Beads style).
// Own small ring buffer; memory provided by Impl (member array, 4096
// stereo frames = 32 KB, fine inside the Impl allocation).
class RotaryShifter {
public:
    void Init(float sample_rate);
    void SetRatio(float ratio);        // block rate; 1.0 = bypass request
    StereoFrame Process(StereoFrame in);   // per sample

private:
    float buf_l_[kShifterSize];
    float buf_r_[kShifterSize];
    size_t write_ = 0;
    float phase_ = 0.f;      // 0..1 head sweep
    float phase_inc_ = 0.f;  // (1 - ratio) / kShifterSize, per sample
    bool  bypass_ = true;
    float bypass_xfade_ = 1.f;  // declick on engage/disengage, 256-sample ramp
};

} // namespace retours_delay_dsp
