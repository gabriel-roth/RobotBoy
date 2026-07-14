#pragma once

#include "WavetableData.hpp"
#include "particules_dsp/types.h"

// Implements particules_dsp::WavetableProvider using the Plaits-derived data in
// WavetableData.hpp: 24 banks × 8 waveforms × 256 samples.
struct RackWavetableProvider : particules_dsp::WavetableProvider {
    const float* GetWaveform(int bank, int index) const override {
        return WavetableData::kData[bank][index];
    }
    int NumBanksAvailable() const override {
        return WavetableData::kNumWavetableBanks;
    }
    int WaveformsPerBank() const override {
        return WavetableData::kWaveformsPerBank;
    }
};
