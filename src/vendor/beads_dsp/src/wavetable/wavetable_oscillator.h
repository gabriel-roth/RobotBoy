#pragma once

#include "../../include/beads/types.h"

namespace beads {

class WavetableOscillator {
public:
    void Init(float sample_rate);

    void SetProvider(WavetableProvider* provider);

    // Process one block of audio output.
    // pitch_semitones: V/Oct pitch in semitones (0 = middle C)
    // bank: 0-1, selects which bank (FEEDBACK knob)
    // wave: 0-1, selects waveform within bank (TIME knob)
    void Process(float pitch_semitones, float bank, float wave,
                 StereoFrame* output, size_t num_frames);

private:
    float sample_rate_ = 48000.0f;
    WavetableProvider* provider_ = nullptr;

    float phase_ = 0.0f;
    float phase_increment_ = 0.0f;
};

} // namespace beads
