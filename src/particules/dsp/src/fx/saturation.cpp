#include "saturation.h"
#include <cmath>

namespace particules_dsp {

void Saturation::Init() {
    // No persistent state needed
}

// ---------------------------------------------------------------------------
// Asymmetric soft clip for tape character.
// Positive peaks are clipped a little harder (drive * 1.1) to mimic
// magnetic bias asymmetry.
// ---------------------------------------------------------------------------
float Saturation::AsymmetricSoftClip(float x) {
    const float shaped = x * (x >= 0.0f ? 1.1f : 0.9f);
    return SoftClip(shaped);
}

// ---------------------------------------------------------------------------
// Process: apply saturation curve matching the current quality mode.
// ---------------------------------------------------------------------------
float Saturation::Process(float input, QualityMode mode) {
    switch (mode) {
        case QualityMode::kBrightDigital:
            // Clean hard clip — transparent brickwall
            return HardClip(input, 1.0f);

        case QualityMode::kColdDigital:
            // Medium-drive soft clip (tanh-like)
            return SoftClip(input * 1.5f);

        case QualityMode::kSunnyTape:
            // Asymmetric tape-style soft clip, moderate drive
            return AsymmetricSoftClip(input);

        case QualityMode::kScorchedCassette:
            // Mu-law compression for warm tape character
            return MuLawCompress(input, 64.0f);
    }
    return input;
}

StereoFrame Saturation::Process(StereoFrame input, QualityMode mode) {
    return {
        Process(input.l, mode),
        Process(input.r, mode)
    };
}

// ---------------------------------------------------------------------------
// LimitFeedback: keep feedback gain under control per quality mode.
//
// HiFi is a hard wall; the others allow softer, more musical limiting
// so that high feedback sounds characterful rather than brittle.
// ---------------------------------------------------------------------------
float Saturation::LimitFeedback(float input, QualityMode mode) {
    switch (mode) {
        case QualityMode::kBrightDigital:
            // Brickwall at +/-1
            return HardClip(input, 1.0f);

        case QualityMode::kColdDigital:
            // Soft clip with moderate headroom
            return SoftClip(input);

        case QualityMode::kSunnyTape:
            // Slightly compressed feedback
            return AsymmetricSoftClip(input * 0.9f);

        case QualityMode::kScorchedCassette:
            // Hard clip at +/-1: the storage codec clamps on write anyway,
            // so clipping here just bounds the feedback sum pre-encoder.
            return HardClip(input, 1.0f);
    }
    return input;
}

StereoFrame Saturation::LimitFeedback(StereoFrame input, QualityMode mode) {
    return {
        LimitFeedback(input.l, mode),
        LimitFeedback(input.r, mode)
    };
}

} // namespace particules_dsp
