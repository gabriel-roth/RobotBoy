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

    float phase_scale_ = 0.0f;        // kWavetableSize / sample_rate_, set in Init()
    // Value-compare caches (self-invalidating): recompute only when the
    // corresponding input actually changes.
    float last_pitch_ = -1e9f;
    float last_bank_ = -1.0f, last_wave_ = -1.0f;
    int   num_banks_ = 0, waveforms_per_bank_ = 0;    // cached in SetProvider()
    const float *w_ll_ = nullptr, *w_lh_ = nullptr, *w_hl_ = nullptr, *w_hh_ = nullptr;
    float bank_frac_ = 0.0f, wave_frac_ = 0.0f;
};

} // namespace particules_dsp
