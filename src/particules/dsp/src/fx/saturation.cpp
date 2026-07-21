#include "saturation.h"
#include <cmath>

namespace particules_dsp {

void Saturation::Init() {
    // No persistent state needed
}

// ---------------------------------------------------------------------------
// NormalizedSoftClip: SoftClip(drive*x)/drive. Unity small-signal slope,
// output ceiling 1/drive.
// ---------------------------------------------------------------------------
float Saturation::NormalizedSoftClip(float x, float drive) {
    return SoftClip(x * drive) / drive;
}

// ---------------------------------------------------------------------------
// Asymmetric soft clip for tape character.
// Positive peaks saturate earlier (mimicking magnetic bias asymmetry) but
// both branches keep unity small-signal slope — the old form multiplied the
// negative branch by 0.9 *inside* the tanh with no normalization, which
// acted as a hidden level trim on everything passing through.
// ---------------------------------------------------------------------------
float Saturation::AsymmetricSoftClip(float x) {
    return x >= 0.0f ? NormalizedSoftClip(x, 1.1f) : SoftClip(x);
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
            // Asymmetric tape limiting, unity small-signal gain (the old
            // *0.9f trim made Sunny decay ~0.9 dB/repeat faster than the
            // other modes at the same feedback knob).
            return AsymmetricSoftClip(input);

        case QualityMode::kScorchedCassette:
            // Soft tanh bound — "grungy tape saturation" per the spec, not
            // a hard wall. The write path adds the deep per-pass drive
            // (SaturateWrite); this just bounds the feedback sum warmly.
            return SoftClip(input);
    }
    return input;
}

StereoFrame Saturation::LimitFeedback(StereoFrame input, QualityMode mode) {
    return {
        LimitFeedback(input.l, mode),
        LimitFeedback(input.r, mode)
    };
}

// ---------------------------------------------------------------------------
// SaturateWrite: write-path tape drive (see header). Digital modes pass
// through; tape ceilings (1/1.54 and 1/1.4 for Sunny +/-, 1/2.2 Scorched)
// stay below the storage codec's +/-1 clamp on purpose.
// ---------------------------------------------------------------------------
float Saturation::SaturateWrite(float input, QualityMode mode) {
    switch (mode) {
        case QualityMode::kBrightDigital:
        case QualityMode::kColdDigital:
            return input;
        case QualityMode::kSunnyTape:
            return input >= 0.0f
                       ? NormalizedSoftClip(input, kSunnyWriteDrive * 1.1f)
                       : NormalizedSoftClip(input, kSunnyWriteDrive);
        case QualityMode::kScorchedCassette:
            return NormalizedSoftClip(input, kScorchedWriteDrive);
    }
    return input;
}

StereoFrame Saturation::SaturateWrite(StereoFrame input, QualityMode mode) {
    return {
        SaturateWrite(input.l, mode),
        SaturateWrite(input.r, mode)
    };
}

} // namespace particules_dsp
