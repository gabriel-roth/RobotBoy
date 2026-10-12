// Knob laws and CV conditioning, as Beads applies them to its pots and CV
// inputs (beads/cv_reader.h). The firmware runs these once per audio block,
// on raw ADC readings; here the input is already a 0..1 knob position or a
// scaled CV, so only the laws, smoothing and clamping remain.

#pragma once

#include <algorithm>
#include <cmath>

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/hysteresis_filter.h"

namespace robotboy_beads {

// Attenurandomizer knob law: a dead band around noon and a curved response
// either side, reaching -1 / +1 at the end stops (cv_reader.h LAW_ATTENUVERTER).
inline float AttenuverterLaw(float raw) {
  float v = raw - 0.5f;
  return (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
}

// Pitch knob law: +-24 semitones with flat notches at 0, 1, 3, 4, 7, 12 and
// 24 semitones each side of noon (cv_reader.h LAW_DETUNE).
inline float DetuneLaw(float raw) {
  raw = std::min(std::max(raw, 0.0f), 1.0f);
  float detune = 2.0f * raw - 1.0f;
  const float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 6.0f;
  MAKE_INTEGRAL_FRACTIONAL(detune);

  const float intervals[8] = {
    0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f
  };

  detune_fractional = std::max(
      (detune_fractional - 0.33333333f) * 1.5f, 0.0f);

  const float a = intervals[detune_integral];
  const float b = intervals[detune_integral + 1];
  return stmlib::Crossfade(a, b, stmlib::SmoothStep(detune_fractional)) * sign;
}

// Knob position for a pitch in semitones. DetuneLaw is monotonic, so this
// bisects; a value inside a notch returns the middle of that notch.
inline float InverseDetuneLaw(float semitones) {
  semitones = std::min(std::max(semitones, -24.0f), 24.0f);
  float lo = 0.0f, hi = 1.0f;  // smallest raw with DetuneLaw(raw) >= target
  for (int i = 0; i < 40; ++i) {
    const float mid = 0.5f * (lo + hi);
    if (DetuneLaw(mid) >= semitones) hi = mid; else lo = mid;
  }
  const float first = hi;
  lo = 0.0f; hi = 1.0f;          // largest raw with DetuneLaw(raw) <= target
  for (int i = 0; i < 40; ++i) {
    const float mid = 0.5f * (lo + hi);
    if (DetuneLaw(mid) <= semitones) lo = mid; else hi = mid;
  }
  return first <= lo ? 0.5f * (first + lo) : first;
}

enum class PotLaw { kNormal, kAttenuverter, kDetune };

// One Beads pot: law, one-pole smoothing and optional hysteresis, run once
// per engine block. The first reading initializes the filter so a patch
// loads without a glide from zero.
class PotChannel {
 public:
  void Init(PotLaw law, float lp_coefficient, float hysteresis) {
    law_ = law;
    lp_coefficient_ = lp_coefficient;
    hysteresis_filter_.Init(hysteresis);
    primed_ = false;
  }

  float Process(float raw) {
    if (!std::isfinite(raw)) raw = 0.5f;
    float v = raw;
    if (law_ == PotLaw::kAttenuverter) {
      v = AttenuverterLaw(raw);
    } else if (law_ == PotLaw::kDetune) {
      v = DetuneLaw(raw);
    }
    if (!primed_) {
      lp_ = v;
      hysteresis_filter_.Process(v, 0.0f);
      primed_ = true;
    }
    ONE_POLE(lp_, v, lp_coefficient_);
    return hysteresis_filter_.Process(lp_);
  }

  float value() const { return hysteresis_filter_.value(); }

 private:
  PotLaw law_ = PotLaw::kNormal;
  float lp_coefficient_ = 1.0f;
  float lp_ = 0.0f;
  bool primed_ = false;
  stmlib::HysteresisFilter hysteresis_filter_;
};

// One Beads CV input: one-pole smoothing and clamping of an already-scaled
// CV (V/5 for most inputs, semitones for Pitch). Non-finite input reads 0.
class CvChannel {
 public:
  void Init(float lp_coefficient, float min, float max) {
    lp_coefficient_ = lp_coefficient;
    min_ = min;
    max_ = max;
    lp_ = 0.0f;
    value_ = 0.0f;
  }

  float Process(float scaled) {
    if (!std::isfinite(scaled)) scaled = 0.0f;
    ONE_POLE(lp_, scaled, lp_coefficient_);
    float v = lp_;
    CONSTRAIN(v, min_, max_);
    value_ = v;
    return v;
  }

  float value() const { return value_; }

 private:
  float lp_coefficient_ = 1.0f;
  float min_ = -1.0f;
  float max_ = 1.0f;
  float lp_ = 0.0f;
  float value_ = 0.0f;
};

// Beads' CV channel settings (cv_reader.cc): smoothing and clamp ranges.
// Scaling: V/5 for Time/Size/Shape/Density, V*12 (semitones) for Pitch.
inline void InitTimeSizeShapeCv(CvChannel* c) { c->Init(0.5f, -1.6f, 1.6f); }
inline void InitDensityCv(CvChannel* c) { c->Init(1.0f, -1.0f, 1.0f); }
inline void InitPitchCv(CvChannel* c) { c->Init(1.0f, -60.0f, 60.0f); }
// Beads' assignable CV (it can drive Feedback, Dry/wet or Reverb): V/5,
// smoothed a little more, +-1.
inline void InitMixCv(CvChannel* c) { c->Init(0.1f, -1.0f, 1.0f); }

// Beads' pot settings (cv_reader.cc): all pots smooth at 0.05 per block,
// Time also has 0.001 of hysteresis.
constexpr float kPotLp = 0.05f;
constexpr float kTimeHysteresis = 0.001f;

// Feedback / Dry-wet / Reverb: Beads scales the pot by 1.005 so the end stop
// reaches 1.0, then adds its assignable CV (conditioned by a CvChannel set up
// with InitMixCv). RobotBoy has one CV input per parameter, each with its own
// attenuverter.
inline float MixParameter(float knob, float cv, float amount) {
  float v = knob * 1.005f + cv * amount;
  CONSTRAIN(v, 0.0f, 1.0f);
  return v;
}

}  // namespace robotboy_beads
