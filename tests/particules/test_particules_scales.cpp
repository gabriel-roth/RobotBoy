#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "../../src/particules/particules_scales.h"

int main() {
    using namespace particules;

    // Every scale mode: table exists, ratios ascending, 1/1-relative
    // (all > 1.0), terminated by the 2.0 octave period.
    for (int mode = kPitchChromatic; mode <= kPitchMinorPentatonic; ++mode) {
        uint32_t count = 0;
        const int* semis = ScaleSemitones(mode, &count);
        assert(semis != nullptr);
        assert(count >= 5 && count <= kMaxScaleNotes);

        double ratios[kMaxScaleNotes];
        BuildScaleRatios(semis, count, ratios);

        double prev = 1.0;
        for (uint32_t i = 0; i < count; ++i) {
            assert(ratios[i] > prev);
            prev = ratios[i];
        }
        assert(std::fabs(ratios[count - 1] - 2.0) < 1e-12);
    }

    // Chromatic has all 12 degrees; pentatonics have 5; diatonics 7.
    uint32_t count = 0;
    ScaleSemitones(kPitchChromatic, &count);        assert(count == 12);
    ScaleSemitones(kPitchMajor, &count);            assert(count == 7);
    ScaleSemitones(kPitchMinor, &count);            assert(count == 7);
    ScaleSemitones(kPitchMajorPentatonic, &count);  assert(count == 5);
    ScaleSemitones(kPitchMinorPentatonic, &count);  assert(count == 5);

    // Legacy pitch-lock modes have no table.
    for (int mode : {kPitchOff, kPitchOctaves, kPitchOctavesFifths}) {
        count = 99;
        assert(ScaleSemitones(mode, &count) == nullptr);
        assert(count == 0);
    }

    std::printf("test_particules_scales: all assertions passed\n");
    return 0;
}
