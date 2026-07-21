#pragma once

#include "../../include/particules_dsp/types.h"
#include "../util/dsp_utils.h"
#include "../util/svf.h"
#include "../random/random.h"

namespace particules_dsp {

// Simulates the 4 quality mode characters via DSP. Bit-depth character
// (12-bit / mu-law) is NOT modeled here — it lives in the recording buffer's
// storage codec (see StorageFormat / QualityConfigFor in types.h). This
// class only applies the analog-domain coloring: anti-alias/tone filtering,
// tape hiss, and wow/flutter.
//
// Quality mode processing:
//   BrightDigital:    No degradation, pass-through.
//   ColdDigital:      Input LP at 10kHz (anti-alias for 2x decimation).
//                     Output pass-through (quantization is the storage codec's job).
//   SunnyTape:        Input LP at 10kHz (anti-alias) + output LP at 10kHz (tone).
//                     Half-depth wow/flutter.
//   ScorchedCassette: Input LP at 10kHz (anti-alias) + independent per-channel
//                     hiss (stereo is preserved — no mono sum; mono-vs-stereo
//                     is an input property) + output LP at 5kHz (dark tone).
//                     Full-depth wow/flutter.
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

    // Anti-alias input LPs must sit below each mode's decimated Nyquist
    // (all lo-fi modes are 2x -> Nyquist 12 kHz); output LPs are tonal
    // shaping at host rate. Bit-depth character lives in the recording
    // buffer's storage codec, not here.
    static constexpr float kColdDigitalInputLpHz = 10000.0f;
    static constexpr float kSunnyTapeInputLpHz   = 10000.0f;
    static constexpr float kSunnyTapeOutputLpHz  = 10000.0f;  // tone (bright tape)
    static constexpr float kScorchedInputLpHz    = 10000.0f;
    static constexpr float kScorchedOutputLpHz   = 5000.0f;   // cassette tone (dark)

private:

    // Tape hiss level (subtle)
    static constexpr float kTapeHissLevel = 0.00025f;

    // Quality mode transition: crossfade between old and new mode output
    QualityMode prev_input_mode_ = QualityMode::kBrightDigital;
    QualityMode prev_output_mode_ = QualityMode::kBrightDigital;
    static constexpr int kModeXfadeSamples = 64;
    int input_xfade_counter_ = 0;
    int output_xfade_counter_ = 0;

    // Crossfade from the unprocessed input to the new mode's output for
    // kModeXfadeSamples after a mode switch, avoiding the abrupt timbral
    // jump (especially into/out of tape mode's filtering + hiss).
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
