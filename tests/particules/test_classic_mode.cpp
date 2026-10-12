// tests/particules/test_classic_mode.cpp
#include "classic_mode.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using robotboy::ClassicFingerprint;
using robotboy::DetectClassic;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    const auto hiss = ClassicFingerprint::kAbsentTapeHiss;
    const auto banks = ClassicFingerprint::kPresentBankGroups;
    // An explicit key wins over any fingerprint.
    for (bool fp : {false, true}) {
        CHECK(DetectClassic(true, true, fp, hiss));
        CHECK(!DetectClassic(true, false, fp, hiss));
        CHECK(DetectClassic(true, true, fp, banks));
        CHECK(!DetectClassic(true, false, fp, banks));
    }
    // Particules/Retours: v2.1 never wrote tapeHiss; beta.1 always did.
    CHECK(DetectClassic(false, false, /*tapeHiss present*/ false, hiss));
    CHECK(!DetectClassic(false, false, /*tapeHiss present*/ true, hiss));
    // Ondes: v2.1 always wrote bankGroupsEnabled; beta.1 wrote nothing.
    CHECK(DetectClassic(false, false, /*bankGroups present*/ true, banks));
    CHECK(!DetectClassic(false, false, /*bankGroups present*/ false, banks));
    std::printf(failures ? "classic_mode: %d failures\n" : "classic_mode: ok\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
