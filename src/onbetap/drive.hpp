#pragma once
// Onbetap's Drive knob → gain mapping (input drive, output makeup, VCA push).
//
// The Drive knob sets the input gain into the nonlinear core. The output
// makeup is a constant buffer gain (the ×11 clone output buffer), deliberately
// independent of Drive: the core's state limiting already compresses level,
// the authentic "natural compression between signal and self-oscillation", so
// a Drive-dependent makeup would double-compensate, dropping level and
// stripping grit as Drive rises.
//
// The one Drive-dependent output element is vcaPush: a bounded boost (never a
// cut) into the fixed 9 V output-VCA ceiling, quadratic in drive, so the top
// of the knob keeps gaining grit while the authentic resonance choke removes
// the resonance-derived grit. gritDb = 0 disables it.
#include <cmath>

namespace onbetap {

constexpr float kVoltsToCore = 1.f / 2.4f;  // volts → core units (2.4 V window)
constexpr float kBaseTrim    = 0.4f;        // drive=0 → mild warmth at ±5 V
constexpr float kOutScale    = 20.5f;       // constant output buffer gain

constexpr float kDriveSpanDb   = 36.f;   // knob spans −12…+24 dB of input gain
constexpr float kDefaultGritDb = 3.5f;   // VCA push at full Drive (dB)

struct DriveGains {
	float driveScale;  // input gain into the core
	float makeup;      // output buffer gain (constant, Drive-independent)
	float vcaPush;     // Drive-grit push into the output VCA (always >= 1)
};

// drive: knob [0,1] plus CV, already clamped by the caller.
inline DriveGains driveGains(float drive, float driveDb = kDriveSpanDb,
                             float gritDb = kDefaultGritDb) {
	float spanOct    = driveDb / 6.0206f;                   // dB → octaves
	float driveGain  = std::exp2(-2.f + spanOct * drive);   // 0.25 → …
	float driveScale = driveGain * kBaseTrim * kVoltsToCore;
	float vcaPush    = std::exp2(gritDb / 6.0206f * drive * drive);
	return { driveScale, kOutScale, vcaPush };
}

} // namespace onbetap
