#pragma once

#include "../../include/particules_dsp/types.h"
#include "../util/dsp_utils.h"
#include "../util/svf.h"
#include "../random/random.h"

namespace particules_dsp {

// Simulates the 4 quality mode characters via DSP.
//
// Quality mode processing:
//   HiFi:      No degradation, pass-through.
//   Clouds:    LP at ~10kHz input.  12-bit quantization on output.
//   CleanLoFi: LP at ~2.5kHz input + ~10kHz output.  No quantization.
//   Tape:      LP at ~5kHz.  Mono sum.  Gentle mu-law transfer curve (mu=64).
//              Wow LFO (~0.5 Hz, +/-0.02 semitones) +
//              flutter LFO (~6 Hz, +/-0.003 semitones).
class QualityProcessor {
public:
    void Init(float sample_rate);

    // Process input through quality mode character (called before recording)
    StereoFrame ProcessInput(StereoFrame input, QualityMode mode);

    // Process output through quality mode character (called after grain/delay).
    StereoFrame ProcessOutput(StereoFrame input, QualityMode mode);

    // Get wow/flutter pitch modulation (for tape mode grain read position).
    // Returns a pitch ratio multiplier (1.0 = no modulation).
    // num_samples: number of samples to advance the LFO by (typically block size).
    float GetPitchModulation(QualityMode mode, size_t num_samples = 1);

private:
    float sample_rate_ = 48000.0f;

    // LP filters for frequency band limiting per mode
    StateVariableFilter input_lp_l_;
    StateVariableFilter input_lp_r_;
    StateVariableFilter output_lp_l_;
    StateVariableFilter output_lp_r_;

    // Wow/flutter LFOs (tape mode)
    float wow_phase_ = 0.0f;
    float flutter_phase_ = 0.0f;

    // Noise generator for tape hiss
    Random noise_gen_;

    // Pre-computed per-sample LFO increments
    float wow_increment_ = 0.0f;
    float flutter_increment_ = 0.0f;

    // Cached cutoff Hz to avoid recomputing tan() every sample
    float current_input_cutoff_hz_ = 0.0f;
    float current_output_cutoff_hz_ = 0.0f;

public:
    // -- Constants (public for helper access) --
    static constexpr float kWowHz = 0.5f;
    static constexpr float kFlutterHz = 6.0f;
    static constexpr float kWowSemitones = 0.02f;
    static constexpr float kFlutterSemitones = 0.003f;

    static constexpr float kCloudsInputLpHz    = 10000.0f;
    static constexpr float kCleanLoFiLpHz      = 10000.0f;
    static constexpr float kCleanLoFiInputLpHz = 2500.0f;
    static constexpr float kTapeLpHz           = 5000.0f;

private:

    // Quantization scale: 12-bit for Clouds mode
    static constexpr float kQuantScale     = 2048.0f;  // 2^11

    // Tape hiss level (subtle)
    static constexpr float kTapeHissLevel = 0.00025f;

    // Quality mode transition: crossfade between old and new mode output
    QualityMode prev_input_mode_ = QualityMode::kHiFi;
    QualityMode prev_output_mode_ = QualityMode::kHiFi;
    static constexpr int kModeXfadeSamples = 64;
    int input_xfade_counter_ = 0;
    int output_xfade_counter_ = 0;

    // Crossfade from the unprocessed input to the new mode's output for
    // kModeXfadeSamples after a mode switch, avoiding the abrupt timbral
    // jump (especially into/out of tape mode's mono sum + mu-law).
    static void ApplyModeXfade(int& counter, const StereoFrame& input, StereoFrame& result) {
        if (counter > 0) {
            float mix = static_cast<float>(counter) / static_cast<float>(kModeXfadeSamples);
            // mix goes from 1 (all old = raw input) to 0 (all new mode)
            result.l = input.l * mix + result.l * (1.0f - mix);
            result.r = input.r * mix + result.r * (1.0f - mix);
            counter--;
        }
    }
};

} // namespace particules_dsp
