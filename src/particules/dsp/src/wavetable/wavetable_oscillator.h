#pragma once

#include "particules_dsp/types.h"

namespace particules_dsp {

// Morphing wavetable oscillator. Bank×wave select a 2D position in the
// wavetable set; the four neighbouring waveforms are bilinearly interpolated,
// phase is linearly interpolated. Mono output written to both StereoFrame channels.
class WavetableOscillator {
public:
    void Init(float sample_rate);
    void SetProvider(WavetableProvider* provider);

    // pitch_semitones: V/oct pitch in semitones (0 = middle C)
    // bank: 0-1 across banks; wave: 0-1 across waveforms within a bank
    void Process(float pitch_semitones, float bank, float wave,
                 StereoFrame* output, size_t num_frames);

private:
    float sample_rate_ = 48000.0f;
    WavetableProvider* provider_ = nullptr;
    float phase_ = 0.0f;
    float phase_increment_ = 0.0f;
};

} // namespace particules_dsp
