#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../src/particules/ondes_pitch_map.hpp"

static bool approx(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

int main() {
    // Endpoint mapping, same range as the notched map it replaces.
    assert(approx(ondesKnobToSemitones(0.0f), -24.0f));
    assert(approx(ondesKnobToSemitones(1.0f),  24.0f));
    assert(approx(ondesKnobToSemitones(0.5f),   0.0f));

    // Round-trip through the inverse at several points, including the old
    // notch landmarks (they're unremarkable now, but should still round-trip).
    for (float st : {-24.f, -19.f, -12.f, -7.f, 0.f, 7.f, 12.f, 19.f, 24.f}) {
        float roundtrip = ondesKnobToSemitones(ondesSemitonesToKnob(st));
        assert(approx(roundtrip, st));
    }

    // Linear: equal knob steps produce equal semitone steps everywhere,
    // including across where the old notch zones used to be widened.
    float stepAtOldNotch = ondesKnobToSemitones(0.55f) - ondesKnobToSemitones(0.45f);
    float stepElsewhere   = ondesKnobToSemitones(0.90f) - ondesKnobToSemitones(0.80f);
    assert(approx(stepAtOldNotch, stepElsewhere));

    // Strictly monotonic.
    float prev = ondesKnobToSemitones(0.0f);
    for (int i = 1; i <= 200; ++i) {
        float cur = ondesKnobToSemitones(i / 200.0f);
        assert(cur > prev);
        prev = cur;
    }

    // Out-of-range knob positions clamp instead of extrapolating.
    assert(approx(ondesKnobToSemitones(-1.f), -24.0f));
    assert(approx(ondesKnobToSemitones(2.f),   24.0f));

    printf("All tests passed.\n");
    return 0;
}
