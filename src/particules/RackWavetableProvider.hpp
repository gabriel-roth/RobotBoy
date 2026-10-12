#pragma once

#include "WavetableData.hpp"
#include "particules_dsp/types.h"

// Implements particules_dsp::WavetableProvider using the Plaits-derived data in
// WavetableData.hpp: 24 banks x 8 waveforms x 256 samples, grouped into three
// user-facing sets of 8 banks each (WavetableData.hpp's own grouping comment):
// group 0 = sines (banks 0-7), group 1 = formants (banks 8-15), group 2 =
// Braids imports (banks 16-23). Any group can be disabled from Ondes' context
// menu; the logical bank range GetWaveform()/NumBanksAvailable() expose then
// covers only the enabled groups, so the knob's full [0,1] range always spans
// whatever remains -- no dead zone at a disabled group's old position.
struct RackWavetableProvider : particules_dsp::WavetableProvider {
    static constexpr int kNumBankGroups = 3;
    static constexpr int kBanksPerGroup =
        WavetableData::kNumWavetableBanks / kNumBankGroups;
    static_assert(WavetableData::kNumWavetableBanks % kNumBankGroups == 0,
                  "bank groups must divide the bank count evenly");

    const float* GetWaveform(int bank, int index) const override {
        int physical = physicalBankIndex(bank);
        return physical >= 0 ? WavetableData::kData[physical][index] : nullptr;
    }
    int NumBanksAvailable() const override {
        int n = 0;
        for (int g = 0; g < kNumBankGroups; ++g)
            if (groupEnabled_[g]) n += kBanksPerGroup;
        return n;
    }
    int WaveformsPerBank() const override {
        return WavetableData::kWaveformsPerBank;
    }

    bool isGroupEnabled(int group) const { return groupEnabled_[group]; }

    // False only when `group` is currently enabled and is the last one --
    // disabling it would leave zero banks reachable.
    bool canDisableGroup(int group) const {
        if (!groupEnabled_[group]) return true;
        int enabledCount = 0;
        for (int g = 0; g < kNumBankGroups; ++g)
            if (groupEnabled_[g]) enabledCount++;
        return enabledCount > 1;
    }

    // No-op if disabling `group` would leave none enabled.
    void setGroupEnabled(int group, bool enabled) {
        if (!enabled && !canDisableGroup(group)) return;
        groupEnabled_[group] = enabled;
    }

private:
    // Maps a logical bank index (as seen by GetWaveform/NumBanksAvailable) to
    // its physical WavetableData bank, skipping disabled groups in
    // sines -> formants -> Braids order. Returns -1 if out of range.
    int physicalBankIndex(int logicalBank) const {
        int remaining = logicalBank;
        for (int g = 0; g < kNumBankGroups; ++g) {
            if (!groupEnabled_[g]) continue;
            if (remaining < kBanksPerGroup) return g * kBanksPerGroup + remaining;
            remaining -= kBanksPerGroup;
        }
        return -1;
    }

    bool groupEnabled_[kNumBankGroups] = {true, true, true};
};
