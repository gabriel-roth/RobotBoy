#pragma once
/**
 * drive.hpp — Onbetap Drive-knob → gain mapping (input drive + output makeup).
 *
 * The Drive knob maps to input gain into the nonlinear core. The output makeup
 * gain is a CONSTANT buffer gain (the ×11 clone output-buffer analog): it is
 * deliberately independent of Drive. The core's integrator-state rail clamping
 * already provides level compression — the authentic "natural compression
 * between signal and self-osc" — so a Drive-dependent makeup double-compensates,
 * dropping level AND stripping grit as Drive rises.
 *
 * The one deliberate Drive-dependent output element is vcaPush: a bounded
 * BOOST (never a cut) into the fixed 9 V output-VCA ceiling, quadratic in
 * drive, so the top of the knob keeps gaining grit while the (authentic)
 * resonance choke removes the resonance-derived grit. gritDb = 0 disables it.
 */

#include <cmath>

namespace onbetap {

// volts → core units (1 / 2.4 V window) and Task-5 calibration constants.
constexpr float kVoltsToCore = 1.f / 2.4f;
constexpr float kBaseTrim    = 0.4f;   // drive=0 → mild warmth at ±5 V
constexpr float kOutScale    = 20.5f;  // constant output buffer gain

// Baked voicing (2026-07-18, by-ear final): the Tuning menu was removed and
// its sliders fixed at these values. Span 36 dB (knob = −12…+24 dB) is viable
// again — the resonance-choke reversal that motivated the earlier 30 dB trim
// is handled by the grit push and the deep-overdrive guards. Headroom 1×,
// onset trim 0, and output trim 0 dB are baked as literals at the call site.
constexpr float kDriveSpanDb   = 36.f;
constexpr float kDefaultGritDb = 3.5f; // Drive-grit VCA push at full Drive (dB)

struct DriveGains {
    float driveScale;  // input gain into the core
    float makeup;      // output buffer gain (constant, Drive-independent)
    float vcaPush;     // Drive-grit push into the output VCA (always >= 1)
};

// drive:    knob [0,1] (+ CV, already applied/clamped by the caller)
// driveDb:  drive span in dB (shipped: kDriveSpanDb)
// headroom: input-scale trim (shipped: 1)
// outDb:    output trim in dB (shipped: 0)
// gritDb:   VCA push at full Drive in dB (shipped: kDefaultGritDb)
inline DriveGains driveGains(float drive, float driveDb,
                             float headroom, float outDb, float gritDb) {
    float spanOct    = driveDb / 6.0206f;                 // dB → octaves
    float driveGain  = std::exp2(-2.f + spanOct * drive); // 0.25 → …
    float driveScale = driveGain * kBaseTrim * kVoltsToCore * headroom;
    float makeup     = kOutScale * std::exp2(outDb / 6.0206f);
    float vcaPush    = std::exp2(gritDb / 6.0206f * drive * drive);
    return { driveScale, makeup, vcaPush };
}

} // namespace onbetap
