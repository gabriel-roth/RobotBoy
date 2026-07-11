#include "quality_processor.h"
#include <cmath>

namespace particules_dsp {

void QualityProcessor::Init(float sample_rate) {
    sample_rate_ = sample_rate;

    input_lp_l_.Init();
    input_lp_r_.Init();
    output_lp_l_.Init();
    output_lp_r_.Init();

    // Set default cutoff frequencies — they'll be overridden per-mode in
    // Process*, but a sane default avoids uninitialized filter state.
    input_lp_l_.SetFrequencyHz(kCloudsInputLpHz, sample_rate_);
    input_lp_r_.SetFrequencyHz(kCloudsInputLpHz, sample_rate_);
    output_lp_l_.SetFrequencyHz(kCleanLoFiLpHz, sample_rate_);
    output_lp_r_.SetFrequencyHz(kCleanLoFiLpHz, sample_rate_);

    // Gentle resonance (Butterworth-ish)
    input_lp_l_.SetQ(0.707f);
    input_lp_r_.SetQ(0.707f);
    output_lp_l_.SetQ(0.707f);
    output_lp_r_.SetQ(0.707f);

    // LFO state
    wow_phase_ = 0.0f;
    flutter_phase_ = 0.0f;
    wow_increment_ = kWowHz / sample_rate_;
    flutter_increment_ = kFlutterHz / sample_rate_;

    noise_gen_.Init(0xBEAD5EED);

    // Force coefficient recomputation on first call
    prev_input_mode_ = QualityMode::kHiFi;
    prev_output_mode_ = QualityMode::kHiFi;
    current_input_cutoff_hz_ = kCloudsInputLpHz;
    current_output_cutoff_hz_ = kCleanLoFiLpHz;
}

// ---------------------------------------------------------------------------
// Helper: get input LP cutoff for a given mode
// ---------------------------------------------------------------------------
static float InputCutoffForMode(QualityMode mode) {
    switch (mode) {
        case QualityMode::kClouds:    return QualityProcessor::kCloudsInputLpHz;
        case QualityMode::kCleanLoFi: return QualityProcessor::kCleanLoFiInputLpHz;
        case QualityMode::kTape:      return QualityProcessor::kTapeLpHz;
        default:                      return QualityProcessor::kCloudsInputLpHz;
    }
}

static float OutputCutoffForMode(QualityMode mode) {
    switch (mode) {
        case QualityMode::kCleanLoFi: return QualityProcessor::kCleanLoFiLpHz;
        case QualityMode::kTape:      return QualityProcessor::kTapeLpHz;
        default:                      return QualityProcessor::kCleanLoFiLpHz;
    }
}

// ---------------------------------------------------------------------------
// ProcessInput: quality-mode coloring applied *before* recording into the
// grain buffer.
// ---------------------------------------------------------------------------
StereoFrame QualityProcessor::ProcessInput(StereoFrame input, QualityMode mode) {
    // Detect mode change and start crossfade
    if (mode != prev_input_mode_) {
        input_xfade_counter_ = kModeXfadeSamples;
        prev_input_mode_ = mode;

        // Recompute SVF coefficients only on mode change
        float cutoff_hz = InputCutoffForMode(mode);
        if (cutoff_hz != current_input_cutoff_hz_) {
            current_input_cutoff_hz_ = cutoff_hz;
            input_lp_l_.SetFrequencyHz(cutoff_hz, sample_rate_);
            input_lp_r_.SetFrequencyHz(cutoff_hz, sample_rate_);
        }
    }

    float filtered_l = input_lp_l_.ProcessLP(input.l);
    float filtered_r = input_lp_r_.ProcessLP(input.r);

    StereoFrame result;
    switch (mode) {
        case QualityMode::kHiFi:
            // No input degradation — use unfiltered signal
            result = input;
            break;

        case QualityMode::kCleanLoFi:
            // Anti-aliasing LP for 8x decimation (effective Nyquist = 3 kHz)
            result = { filtered_l, filtered_r };
            break;

        case QualityMode::kClouds:
            result = { filtered_l, filtered_r };
            break;

        case QualityMode::kTape: {
            // Mono sum
            float mono = (filtered_l + filtered_r) * 0.5f;
            // Add subtle tape hiss
            float hiss = noise_gen_.NextBipolar() * kTapeHissLevel;
            mono += hiss;
            // Mu-law compression
            mono = MuLawCompress(mono, 64.0f);
            result = { mono, mono };
            break;
        }
    }

    // Crossfade from the unprocessed input to the new mode's output
    // when a mode switch just happened.  This avoids the abrupt timbral
    // jump (especially into/out of tape mode's mono sum + mu-law).
    ApplyModeXfade(input_xfade_counter_, input, result);

    return result;
}

// ---------------------------------------------------------------------------
// ProcessOutput: quality-mode coloring applied *after* grain / delay readout.
// ---------------------------------------------------------------------------
StereoFrame QualityProcessor::ProcessOutput(StereoFrame input, QualityMode mode) {
    // Detect mode change and start crossfade
    if (mode != prev_output_mode_) {
        output_xfade_counter_ = kModeXfadeSamples;
        prev_output_mode_ = mode;

        // Recompute SVF coefficients only on mode change
        float cutoff_hz = OutputCutoffForMode(mode);
        if (cutoff_hz != current_output_cutoff_hz_) {
            current_output_cutoff_hz_ = cutoff_hz;
            output_lp_l_.SetFrequencyHz(cutoff_hz, sample_rate_);
            output_lp_r_.SetFrequencyHz(cutoff_hz, sample_rate_);
        }
    }

    // Feed-through: always tick the filters so their state tracks the signal.
    float lp_l = output_lp_l_.ProcessLP(input.l);
    float lp_r = output_lp_r_.ProcessLP(input.r);

    StereoFrame result;
    switch (mode) {
        case QualityMode::kHiFi:
            result = input;
            break;

        case QualityMode::kClouds: {
            // 12-bit quantization
            float l = std::round(input.l * kQuantScale) / kQuantScale;
            float r = std::round(input.r * kQuantScale) / kQuantScale;
            result = { l, r };
            break;
        }

        case QualityMode::kCleanLoFi:
            result = { lp_l, lp_r };
            break;

        case QualityMode::kTape: {
            // Mu-law expansion.  The output LP was already ticked above
            // with the raw (compressed) input, which keeps the filter
            // state tracking the signal.  The LP result (lp_l/lp_r)
            // applied to the compressed signal is a reasonable
            // approximation; applying MuLawExpand *before* LP and
            // re-filtering would double-tick the SVF.  Instead, expand
            // the already-filtered result.
            result = {
                MuLawExpand(lp_l, 64.0f),
                MuLawExpand(lp_r, 64.0f)
            };
            break;
        }
    }

    // Crossfade from unprocessed input to new mode output on mode change
    ApplyModeXfade(output_xfade_counter_, input, result);

    return result;
}

// ---------------------------------------------------------------------------
// GetPitchModulation: returns a pitch *ratio* multiplier for the current
// sample.  Only tape mode produces modulation; all others return 1.0.
//
// Wow  = slow (~0.5 Hz), +/- 0.02 semitones
// Flutter = fast (~6 Hz), +/- 0.003 semitones
// ---------------------------------------------------------------------------
float QualityProcessor::GetPitchModulation(QualityMode mode, size_t num_samples) {
    if (mode != QualityMode::kTape) {
        return 1.0f;
    }

    // Advance LFO phases by the number of samples in this block.
    // The LFO increments are per-sample, so multiply by block size.
    float advance = static_cast<float>(num_samples);
    wow_phase_ += wow_increment_ * advance;
    while (wow_phase_ >= 1.0f) wow_phase_ -= 1.0f;

    flutter_phase_ += flutter_increment_ * advance;
    while (flutter_phase_ >= 1.0f) flutter_phase_ -= 1.0f;

    // Combined pitch deviation in semitones
    float wow_st     = kWowSemitones     * std::sin(wow_phase_     * kTwoPi);
    float flutter_st = kFlutterSemitones * std::sin(flutter_phase_ * kTwoPi);

    // Convert semitones offset to ratio
    return SemitonesToRatio(wow_st + flutter_st);
}

} // namespace particules_dsp
