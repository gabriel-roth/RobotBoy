#pragma once
/**
 * drive.hpp — Onbetap Drive-knob → gain mapping (input drive + output makeup).
 *
 * The Drive knob maps to input gain into the nonlinear core. The output makeup
 * gain is a CONSTANT buffer gain (the ×11 clone output-buffer analog): it is
 * deliberately independent of Drive. The core's integrator-state rail clamping
 * already provides level compression — the authentic "natural compression
 * between signal and self-osc" — so a Drive-dependent makeup double-compensates,
 * dropping level AND stripping grit as Drive rises. See
 * docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md.
 */

#include <cmath>

namespace onbetap {

// volts → core units (1 / 2.4 V window) and Task-5 calibration constants.
constexpr float kVoltsToCore = 1.f / 2.4f;
constexpr float kBaseTrim    = 0.4f;   // drive=0 → mild warmth at ±5 V
constexpr float kOutScale    = 20.5f;  // constant output buffer gain (Task 5)

struct DriveGains {
    float driveScale;  // input gain into the core
    float makeup;      // output buffer gain (constant, Drive-independent)
};

// drive:    knob [0,1] (+ CV, already applied/clamped by the caller)
// driveDb:  drive span in dB (menu "Drive span", default 36)
// headroom: input-scale trim (menu "Core headroom", default 1)
// outDb:    output trim in dB (menu "Output trim", default 0)
inline DriveGains driveGains(float drive, float driveDb,
                             float headroom, float outDb) {
    float spanOct    = driveDb / 6.0206f;                 // dB → octaves
    float driveGain  = std::exp2(-2.f + spanOct * drive); // 0.25 → …
    float driveScale = driveGain * kBaseTrim * kVoltsToCore * headroom;
    float makeup     = kOutScale * std::exp2(outDb / 6.0206f);
    return { driveScale, makeup };
}

} // namespace onbetap
