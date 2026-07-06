#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../src/particules/pitch_notch_map.hpp"

static bool approx(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

int main() {
    // Endpoint mapping
    assert(approx(pitchKnobToSemitones(0.0f), -24.0f));
    assert(approx(pitchKnobToSemitones(1.0f),  24.0f));

    // Center = unison
    assert(approx(pitchKnobToSemitones(0.5f), 0.0f));

    // Round-trips at notch positions
    for (float st : {-19.f, -12.f, -7.f, 0.f, 7.f, 12.f, 19.f}) {
        float roundtrip = pitchKnobToSemitones(semitonesToPitchKnob(st));
        assert(approx(roundtrip, st));
    }

    // Round-trips at non-notch positions
    for (float st : {-23.f, -15.f, -5.f, 3.f, 9.f, 17.f, 23.f}) {
        float roundtrip = pitchKnobToSemitones(semitonesToPitchKnob(st));
        assert(approx(roundtrip, st));
    }

    // Strictly monotonic (sample 200 points)
    float prev = pitchKnobToSemitones(0.0f);
    for (int i = 1; i <= 200; ++i) {
        float t = i / 200.0f;
        float cur = pitchKnobToSemitones(t);
        assert(cur > prev);
        prev = cur;
    }

    // Notch zones are wider than plain semitone intervals
    float notch_width  = semitonesToPitchKnob( 0.5f) - semitonesToPitchKnob(-0.5f);
    float normal_width = semitonesToPitchKnob( 2.5f) - semitonesToPitchKnob( 1.5f);
    assert(notch_width > normal_width);

    // Symmetry: mapping is symmetric around center
    assert(approx(pitchKnobToSemitones(0.5f + 0.1f), -pitchKnobToSemitones(0.5f - 0.1f)));

    printf("All tests passed.\n");
    return 0;
}
