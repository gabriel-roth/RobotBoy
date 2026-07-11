#pragma once

#include <cmath>
#include <cstdint>

// Tables for the unified "Lock pitch" selector. Modes 0-2 are the legacy
// engine-side pitch_lock modes (particules_dsp::QuantizePitchLock); modes 3-7 load
// the engine's PitchQuantizer with a 12-TET ratio table instead. The two
// mechanisms are mutually exclusive by construction (the engine applies the
// scale quantizer before pitch_lock, so setting both would double-quantize).
namespace particules {

enum PitchScaleMode {
    kPitchOff = 0,
    kPitchOctaves = 1,
    kPitchOctavesFifths = 2,
    kPitchChromatic = 3,
    kPitchMajor = 4,
    kPitchMinor = 5,
    kPitchMajorPentatonic = 6,
    kPitchMinorPentatonic = 7,
    kPitchScaleModeCount = 8,
};

constexpr uint32_t kMaxScaleNotes = 12;

// Semitone degrees above the root, excluding the implicit 1/1 (degree 0)
// and ending with the period (12 = octave) — the exact shape
// PitchQuantizer::loadRatios expects after conversion to ratios.
// Returns nullptr (count 0) for the legacy non-scale modes.
inline const int* ScaleSemitones(int mode, uint32_t* count) {
    static const int kChromatic[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    static const int kMajor[]     = {2, 4, 5, 7, 9, 11, 12};
    static const int kMinor[]     = {2, 3, 5, 7, 8, 10, 12};
    static const int kMajorPent[] = {2, 4, 7, 9, 12};
    static const int kMinorPent[] = {3, 5, 7, 10, 12};
    switch (mode) {
        case kPitchChromatic:       *count = 12; return kChromatic;
        case kPitchMajor:           *count = 7;  return kMajor;
        case kPitchMinor:           *count = 7;  return kMinor;
        case kPitchMajorPentatonic: *count = 5;  return kMajorPent;
        case kPitchMinorPentatonic: *count = 5;  return kMinorPent;
        default:                    *count = 0;  return nullptr;
    }
}

// Fill `out` (capacity >= count) with 2^(semitone/12) ratios.
inline void BuildScaleRatios(const int* semitones, uint32_t count, double* out) {
    for (uint32_t i = 0; i < count; ++i)
        out[i] = std::exp2(static_cast<double>(semitones[i]) / 12.0);
}

} // namespace particules
