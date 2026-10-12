#include <catch2/catch_test_macros.hpp>
#include "RackWavetableProvider.hpp"
#include "WavetableData.hpp"

TEST_CASE("RackWavetableProvider: all bank groups enabled by default", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    REQUIRE(provider.NumBanksAvailable() == 24);
    for (int g = 0; g < RackWavetableProvider::kNumBankGroups; ++g)
        REQUIRE(provider.isGroupEnabled(g));
}

TEST_CASE("RackWavetableProvider: disabling a group removes its banks and shrinks the count", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(1, false);  // formants
    REQUIRE_FALSE(provider.isGroupEnabled(1));
    REQUIRE(provider.NumBanksAvailable() == 16);
}

TEST_CASE("RackWavetableProvider: remaining banks are spread contiguously across the logical range", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(1, false);  // sines(0) + Braids(2) remain, 8 banks each
    REQUIRE(provider.NumBanksAvailable() == 16);
    REQUIRE(provider.GetWaveform(0, 0) == WavetableData::kData[0][0]);   // first sines bank
    REQUIRE(provider.GetWaveform(7, 0) == WavetableData::kData[7][0]);   // last sines bank
    REQUIRE(provider.GetWaveform(8, 0) == WavetableData::kData[16][0]);  // first Braids bank
    REQUIRE(provider.GetWaveform(15, 0) == WavetableData::kData[23][0]); // last Braids bank
}

TEST_CASE("RackWavetableProvider: disabling two groups leaves the third spanning the full range", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(0, false);
    provider.setGroupEnabled(1, false);
    REQUIRE(provider.NumBanksAvailable() == 8);
    REQUIRE(provider.GetWaveform(0, 0) == WavetableData::kData[16][0]);
    REQUIRE(provider.GetWaveform(7, 0) == WavetableData::kData[23][0]);
}

TEST_CASE("RackWavetableProvider: cannot disable the only enabled group", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(0, false);
    provider.setGroupEnabled(1, false);
    REQUIRE(provider.NumBanksAvailable() == 8);
    REQUIRE_FALSE(provider.canDisableGroup(2));
    provider.setGroupEnabled(2, false);  // must be refused (no-op)
    REQUIRE(provider.isGroupEnabled(2));
    REQUIRE(provider.NumBanksAvailable() == 8);
}

TEST_CASE("RackWavetableProvider: re-enabling a group restores its banks", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(0, false);
    REQUIRE(provider.NumBanksAvailable() == 16);
    provider.setGroupEnabled(0, true);
    REQUIRE(provider.NumBanksAvailable() == 24);
    REQUIRE(provider.GetWaveform(0, 0) == WavetableData::kData[0][0]);
}
