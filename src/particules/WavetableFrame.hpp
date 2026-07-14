#pragma once

#include "particules_dsp/types.h"
#include <algorithm>

namespace robotboy {

// Bilinearly blend the four neighbouring stored waveforms at normalized
// bank01/wave01 in [0,1] and return the sample at sampleIndex. Mirrors the
// bank x wave crossfade in WavetableOscillator::Process (no phase interp).
inline float wavetableFrameSample(const particules_dsp::WavetableProvider& provider,
                                  float bank01, float wave01, int sampleIndex) {
    const int numBanks = provider.NumBanksAvailable();
    const int perBank  = provider.WaveformsPerBank();
    if (numBanks <= 0 || perBank <= 0) return 0.f;

    const float bankPos = std::clamp(bank01, 0.f, 1.f) * float(numBanks - 1);
    int bankLo = int(bankPos);
    bankLo = std::clamp(bankLo, 0, numBanks - 1);
    int bankHi = std::min(bankLo + 1, numBanks - 1);
    const float bankFrac = bankPos - float(bankLo);

    const float wavePos = std::clamp(wave01, 0.f, 1.f) * float(perBank - 1);
    int waveLo = int(wavePos);
    waveLo = std::clamp(waveLo, 0, perBank - 1);
    int waveHi = std::min(waveLo + 1, perBank - 1);
    const float waveFrac = wavePos - float(waveLo);

    const float* ll = provider.GetWaveform(bankLo, waveLo);
    const float* lh = provider.GetWaveform(bankLo, waveHi);
    const float* hl = provider.GetWaveform(bankHi, waveLo);
    const float* hh = provider.GetWaveform(bankHi, waveHi);
    if (!ll || !lh || !hl || !hh) return 0.f;

    const float sLo = ll[sampleIndex] + (lh[sampleIndex] - ll[sampleIndex]) * waveFrac;
    const float sHi = hl[sampleIndex] + (hh[sampleIndex] - hl[sampleIndex]) * waveFrac;
    return sLo + (sHi - sLo) * bankFrac;
}

}  // namespace robotboy
