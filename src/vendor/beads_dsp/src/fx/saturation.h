#pragma once

#include "../../include/beads/types.h"
#include "../util/dsp_utils.h"

namespace beads {

// Soft-clip / tape saturation curves per quality mode.
//
// Quality mode saturation behavior:
//   HiFi:      Hard clip at +/-1.0 (brickwall feedback limiter)
//   Clouds:    Soft clip using tanh-like curve, medium drive
//   CleanLoFi: Medium tape saturation (asymmetric soft clip)
//   Tape:      Mu-law transfer curve, asymmetric saturation, warm character
class Saturation {
public:
    void Init();

    // Apply saturation curve based on quality mode
    float Process(float input, QualityMode mode);
    StereoFrame Process(StereoFrame input, QualityMode mode);

    // Feedback limiting per quality mode
    float LimitFeedback(float input, QualityMode mode);
    StereoFrame LimitFeedback(StereoFrame input, QualityMode mode);

private:
    // Asymmetric soft clip for tape character.
    // Positive peaks saturate slightly harder than negative peaks.
    static float AsymmetricSoftClip(float x);
};

} // namespace beads
