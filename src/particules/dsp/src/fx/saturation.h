#pragma once

#include "../../include/particules_dsp/types.h"
#include "../util/dsp_utils.h"

namespace particules_dsp {

// Soft-clip / tape saturation curves per quality mode.
//
// Quality mode saturation behavior:
//   BrightDigital:    Hard clip at +/-1.0 (brickwall feedback limiter)
//   ColdDigital:      Soft clip using tanh-like curve, medium drive
//   SunnyTape:        Medium tape saturation (asymmetric soft clip)
//   ScorchedCassette: Soft tanh limiting; deep write-path drive via SaturateWrite
class Saturation {
public:
    // Positive-branch extra drive shared by AsymmetricSoftClip and Sunny's
    // SaturateWrite: positive peaks saturate 1.1x earlier (magnetic bias).
    static constexpr float kTapeBiasAsymmetry = 1.1f;
    // Reciprocal, precomputed: NormalizedSoftClip's saturated branch needs
    // 1/drive and every drive here is a compile-time constant (see task 14,
    // findings §4 M1/M7 -- folds the two divides in SoftClip(x*drive)/drive
    // down to one).
    static constexpr float kTapeBiasAsymmetryRecip = 1.0f / kTapeBiasAsymmetry;
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
    // before it is recorded. Bright passes through. Cold applies the
    // Clouds cubic write limiter (see kColdWriteDrive). Tape modes use
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
    static constexpr float kSunnyWriteDriveRecip = 1.0f / kSunnyWriteDrive;
    // Sunny's positive-branch combined drive (write drive * bias asymmetry)
    // and its reciprocal, precomputed once so the per-sample call is a
    // constant lookup rather than a runtime multiply-then-reciprocal.
    static constexpr float kSunnyWriteDrivePos = kSunnyWriteDrive * kTapeBiasAsymmetry;
    static constexpr float kSunnyWriteDrivePosRecip = 1.0f / kSunnyWriteDrivePos;
    static constexpr float kScorchedWriteDrive = 2.2f;
    static constexpr float kScorchedWriteDriveRecip = 1.0f / kScorchedWriteDrive;

    // Cold digital write drive — a Clouds emulation: Clouds ran its
    // feedback sum through stmlib SoftLimit (the same polynomial as our
    // FastTanh) with a 1.4x drive (granular_processor.cc:197-203), so its
    // overload was a warm cubic smudge, never a hard clip. Normalized here
    // (unity small-signal slope, ceiling 1/1.4) to keep the uniform
    // loop-gain law. See docs/superpowers/plans/
    // 2026-07-21-cold-digital-clouds-voicing-notes.md.
    static constexpr float kColdWriteDrive = 1.4f;
    static constexpr float kColdWriteDriveRecip = 1.0f / kColdWriteDrive;

private:
    // Asymmetric soft clip for tape character: positive peaks saturate
    // earlier (ceiling 1/1.1) than negative (ceiling 1); both branches
    // keep unity small-signal slope.
    static float AsymmetricSoftClip(float x);
    // SoftClip(drive*x)/drive — unity slope at 0, ceiling 1/drive. Folded to
    // a single divide (task 14): for |x*drive| <= 3 the FastTanh polynomial
    // and the /drive share one denominator; above that the clip has already
    // saturated to +/-1, so the caller's precomputed driveRecip (1/drive) is
    // used directly instead of dividing again.
    static float NormalizedSoftClip(float x, float drive, float driveRecip);
};

} // namespace particules_dsp
