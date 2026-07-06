#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "beads/types.h"
#include "pitch/pitch_quantizer.h"

using namespace beads;
using Catch::Approx;

// Load a 12-note equal-temperament (chromatic) scale.
// Degree 0 is implicit at 0.0 V/oct (1/1). The 12 entries are
// degrees 1–11 plus the period (2.0 = 1 octave).
static void load_chromatic(PitchQuantizer& q) {
    double ratios[12];
    for (int i = 0; i < 11; ++i)
        ratios[i] = std::pow(2.0, (i + 1) / 12.0);
    ratios[11] = 2.0;  // period
    q.loadRatios(ratios, 12);
}

// C major pentatonic: degrees at 0, 2, 4, 7, 9 semitones.
// Entries: 2, 4, 7, 9 semitones above root, then period 12.
static void load_pentatonic(PitchQuantizer& q) {
    double ratios[5] = {
        std::pow(2.0, 2.0 / 12),  // D
        std::pow(2.0, 4.0 / 12),  // E
        std::pow(2.0, 7.0 / 12),  // G
        std::pow(2.0, 9.0 / 12),  // A
        2.0                        // C' (period)
    };
    q.loadRatios(ratios, 5);
}

TEST_CASE("PitchQuantizer: unloaded is a pass-through", "[pitch_quantizer]") {
    PitchQuantizer q;
    REQUIRE(q.isLoaded() == false);
    REQUIRE(q.quantize(0.0f)  == Approx(0.0f));
    REQUIRE(q.quantize(0.5f)  == Approx(0.5f));
    REQUIRE(q.quantize(-0.5f) == Approx(-0.5f));
    REQUIRE(q.quantize(2.3f)  == Approx(2.3f));
}

TEST_CASE("PitchQuantizer: loadRatios with null or zero count clears", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_chromatic(q);
    REQUIRE(q.isLoaded() == true);

    q.loadRatios(nullptr, 5);
    REQUIRE(q.isLoaded() == false);
    REQUIRE(q.quantize(0.33f) == Approx(0.33f));
}

TEST_CASE("PitchQuantizer: clear() makes it a pass-through", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_chromatic(q);
    REQUIRE(q.isLoaded() == true);

    q.clear();
    REQUIRE(q.isLoaded() == false);
    REQUIRE(q.quantize(0.5f) == Approx(0.5f));
}

TEST_CASE("PitchQuantizer: chromatic scale — exact degrees unchanged", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_chromatic(q);

    // Degree 0: unison
    REQUIRE(q.quantize(0.0f) == Approx(0.0f).margin(1e-4f));
    // Perfect fourth: 5 semitones
    REQUIRE(q.quantize(5.0f / 12.0f) == Approx(5.0f / 12.0f).margin(1e-4f));
    // Perfect fifth: 7 semitones
    REQUIRE(q.quantize(7.0f / 12.0f) == Approx(7.0f / 12.0f).margin(1e-4f));
    // Octave
    REQUIRE(q.quantize(1.0f) == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("PitchQuantizer: chromatic scale — off-degree snaps to nearest", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_chromatic(q);

    // 1.1 semitones: closer to 1 than to 0
    REQUIRE(q.quantize(1.1f / 12.0f) == Approx(1.0f / 12.0f).margin(1e-4f));
    // 6.6 semitones: closer to 7 than to 6
    REQUIRE(q.quantize(6.6f / 12.0f) == Approx(7.0f / 12.0f).margin(1e-4f));
    // 11.9 semitones: closer to 12 (= 0 of next octave) than to 11
    REQUIRE(q.quantize(11.9f / 12.0f) == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("PitchQuantizer: octave folding — negative input", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_chromatic(q);

    // −0.1 semitones: closer to 0 than to −1
    REQUIRE(q.quantize(-0.1f / 12.0f) == Approx(0.0f).margin(1e-4f));
    // Exactly −1 semitone: snaps to −1/12 V/oct
    REQUIRE(q.quantize(-1.0f / 12.0f) == Approx(-1.0f / 12.0f).margin(1e-4f));
    // −1 octave exactly
    REQUIRE(q.quantize(-1.0f) == Approx(-1.0f).margin(1e-4f));
    // −1 octave + 7 semitones
    REQUIRE(q.quantize(-1.0f + 7.0f / 12.0f) == Approx(-1.0f + 7.0f / 12.0f).margin(1e-4f));
}

TEST_CASE("PitchQuantizer: octave folding — multi-octave input", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_chromatic(q);

    // 1 octave + 7 semitones
    REQUIRE(q.quantize(1.0f + 7.0f / 12.0f) == Approx(1.0f + 7.0f / 12.0f).margin(1e-4f));
    // 2 octaves
    REQUIRE(q.quantize(2.0f) == Approx(2.0f).margin(1e-4f));
    // 3 octaves
    REQUIRE(q.quantize(3.0f) == Approx(3.0f).margin(1e-4f));
}

TEST_CASE("PitchQuantizer: set_root shifts quantization", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_chromatic(q);

    // Root = C5 (MIDI 72), root_v_oct = (72-60)/12 = +1.0
    q.set_root(72);

    // At root (1.0 V/oct): unchanged
    REQUIRE(q.quantize(1.0f) == Approx(1.0f).margin(1e-4f));
    // Root + 1.1 semitones → snaps to root + 1 semitone
    REQUIRE(q.quantize(1.0f + 1.1f / 12.0f) == Approx(1.0f + 1.0f / 12.0f).margin(1e-4f));
    // Root + 7 semitones (fifth above C5) → unchanged
    REQUIRE(q.quantize(1.0f + 7.0f / 12.0f) == Approx(1.0f + 7.0f / 12.0f).margin(1e-4f));

    // Root = C3 (MIDI 48), root_v_oct = (48-60)/12 = -1.0
    q.set_root(48);
    REQUIRE(q.quantize(-1.0f) == Approx(-1.0f).margin(1e-4f));
    REQUIRE(q.quantize(-1.0f + 1.1f / 12.0f) == Approx(-1.0f + 1.0f / 12.0f).margin(1e-4f));
}

TEST_CASE("PitchQuantizer: set_root changes degree selection on sparse scale", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_pentatonic(q);
    // Pentatonic degrees: 0, 2, 4, 7, 9 semitones

    // With default root (C4, MIDI 60), input at 5 st → snaps to E (4 st)
    q.set_root(60);
    REQUIRE(q.quantize(5.0f / 12.0f) == Approx(4.0f / 12.0f).margin(1e-4f));

    // Shift root to D (MIDI 62, +2 st). Now the scale degrees are at 2, 4, 6, 9, 11 st.
    // Input at 5 st: 1 away from 4 (E), 1 away from 6 (F#). Tie broken by nearest in log2 space;
    // for an equal chromatic-step tie, the lower degree wins (first found in scan).
    // To avoid a tie, use 5.5 st: closer to 6 (F#, 0.5 away) than to 4 (E, 1.5 away).
    q.set_root(62);
    // With root at D (62), scale grid starts at 2 st. F# is at 6 st.
    // 5.5 st → 0.5 below F# (6 st) vs 1.5 above E (4 st) → snaps to F# = 6 st
    REQUIRE(q.quantize(5.5f / 12.0f) == Approx(6.0f / 12.0f).margin(1e-4f));

    // Restore root to C (MIDI 60). 5.5 st → closer to E (4 st, 1.5 away) than G (7 st, 1.5 away).
    // Tie: in chromatic semitone space 5.5 is equidistant, but in log2 space E is slightly closer.
    // Use 5.3 st instead: clearly closer to E (4 st, 1.3 away) than G (7 st, 1.7 away).
    q.set_root(60);
    REQUIRE(q.quantize(5.3f / 12.0f) == Approx(4.0f / 12.0f).margin(1e-4f));
}

TEST_CASE("PitchQuantizer: pentatonic scale — off-scale snaps to nearest", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_pentatonic(q);
    // Scale degrees at 0, 2, 4, 7, 9 semitones

    // F (5 semitones): 1 away from E (4), 2 from G (7) → snaps to E
    REQUIRE(q.quantize(5.0f / 12.0f) == Approx(4.0f / 12.0f).margin(1e-4f));
    // A# (10 semitones): 1 away from A (9), 2 from C' (12) → snaps to A
    REQUIRE(q.quantize(10.0f / 12.0f) == Approx(9.0f / 12.0f).margin(1e-4f));
    // B (11 semitones): 1 away from C' (12=0 of next octave), 2 from A (9) → snaps to C'
    REQUIRE(q.quantize(11.0f / 12.0f) == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("PitchQuantizer: set_root clamps to MIDI range 0-127", "[pitch_quantizer]") {
    PitchQuantizer q;
    load_pentatonic(q);

    // Eb (3 st = 0.25 V/oct): root=0 snaps to D (2 st), root=127 snaps to E (4 st),
    // so this input is sensitive to root placement.
    const float input = 3.0f / 12.0f;

    // set_root(-5) should behave identically to set_root(0): clamped to 0
    q.set_root(-5);
    float result_clamped_low = q.quantize(input);
    q.set_root(0);
    float result_0 = q.quantize(input);
    REQUIRE(result_clamped_low == Approx(result_0).margin(1e-4f));

    // set_root(200) should behave identically to set_root(127): clamped to 127
    q.set_root(200);
    float result_clamped_high = q.quantize(input);
    q.set_root(127);
    float result_127 = q.quantize(input);
    REQUIRE(result_clamped_high == Approx(result_127).margin(1e-4f));

    // Sanity: root(0) and root(127) actually produce different outputs
    // (confirms the scale is sensitive to root, so the clamp test is meaningful)
    REQUIRE(result_0 != Approx(result_127).margin(1e-4f));
}
