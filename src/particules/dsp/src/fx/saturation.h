#pragma once

#include "../../include/particules_dsp/types.h"
#include "../util/dsp_utils.h"

namespace particules_dsp {

// Soft-clip / tape saturation curves per quality mode.
//
// Quality mode saturation behavior:
//   HiFi:      Hard clip at +/-1.0 (brickwall feedback limiter)
//   Clouds:    Soft clip using tanh-like curve, medium drive
//   CleanLoFi: Medium tape saturation (asymmetric soft clip)
//   Tape:      Soft tanh limiting; deep write-path drive via SaturateWrite
class Saturation {
public:
    void Init();

    // Apply saturation curve based on quality mode
    float Process(float input, QualityMode mode);
    StereoFrame Process(StereoFrame input, QualityMode mode);

    // Feedback limiting per quality mode. Every curve has unity
    // small-signal slope: the limiter bounds the loop without adding or
    // shedding loop gain at low level (a hidden gain trim here skews the
    // mode's decay rate against the others at the same feedback knob).
    float LimitFeedback(float input, QualityMode mode);
    StereoFrame LimitFeedback(StereoFrame input, QualityMode mode);

    // Write-path tape saturation, applied to the input+feedback sum just
    // before it is recorded. Digital modes pass through. Tape modes use
    // SoftClip(drive*x)/drive: unity small-signal slope, ceiling 1/drive —
    // deliberately below the storage codec's +/-1 clamp so accumulated
    // feedback compresses onto a warm tanh ceiling instead of hard-clipping
    // in the codec. Re-recording through this every pass is what makes
    // tape-mode echoes progressively more saturated.
    float SaturateWrite(float input, QualityMode mode);
    StereoFrame SaturateWrite(StereoFrame input, QualityMode mode);

    // Tape write drives (ceiling = 1/drive; Sunny's positive branch gets a
    // further 1.1x for bias asymmetry).
    static constexpr float kSunnyWriteDrive = 1.4f;
    static constexpr float kScorchedWriteDrive = 2.2f;

private:
    // Asymmetric soft clip for tape character: positive peaks saturate
    // earlier (ceiling 1/1.1) than negative (ceiling 1); both branches
    // keep unity small-signal slope.
    static float AsymmetricSoftClip(float x);
    // SoftClip(drive*x)/drive — unity slope at 0, ceiling 1/drive.
    static float NormalizedSoftClip(float x, float drive);
};

} // namespace particules_dsp
