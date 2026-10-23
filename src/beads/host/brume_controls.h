// Copyright 2019 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// RobotBoy: Brume's knob laws and CV / envelope modulation math. Rack-free.
// Laws and defaults follow the Controls table of the Brume design spec.

#ifndef ROBOTBOY_BEADS_BRUME_CONTROLS_H_
#define ROBOTBOY_BEADS_BRUME_CONTROLS_H_

#include <cmath>

#include "stmlib/dsp/units.h"

namespace robotboy_beads {

enum class BrumeMode { kBeads = 0, kClouds };
enum class BrumeQuality { kBright = 0, kCold, kScorched };
enum class WowScope { kInput = 0, kReverbOnly };

// Tank sample rate for each Quality.
inline float BrumeTankRate(BrumeQuality quality) {
  switch (quality) {
    case BrumeQuality::kCold: return 32000.0f;
    case BrumeQuality::kScorched: return 24000.0f;
    default: return 48000.0f;
  }
}

// Shimmer gain at which the loop just sustains (g1 in the spec). Measured
// by the "[.calibrate]" case in tests/beads_engine/test_brume_shimmer.cpp:
// a 0.2 s noise burst into Beads at Bright, DECAY 0.5, INTERVAL +12, other
// controls at default; the gain at which the tail is as loud at 18..20 s as
// at 2..4 s, bisected to 1.4221 and rounded to 3 significant figures.
constexpr float kShimmerSustainGain = 1.42f;

struct BrumeInput {
  float l, r;
};

// One patched input feeds both sides; none is silence.
inline BrumeInput NormalizeInputs(
    bool l_patched, bool r_patched, float l, float r) {
  if (l_patched && r_patched) return {l, r};
  if (l_patched) return {l, l};
  if (r_patched) return {r, r};
  return {0.0f, 0.0f};
}

inline float ClampUnit(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Noon = unity.
inline float SendGain(float v) { return 2.0f * v; }

// 0..0.95 maps linearly over the Mode's range; the last 5% holds at 1.0.
inline float DecayTime(float v, BrumeMode mode) {
  if (v > 0.95f) return 1.0f;
  const float lo = mode == BrumeMode::kBeads ? 0.30f : 0.35f;
  const float hi = mode == BrumeMode::kBeads ? 0.95f : 0.98f;
  return lo + (hi - lo) * v / 0.95f;
}

// Knob positions that reproduce the original Beads reverb: all at noon. Beads
// is the default Mode; Clouds' own originals differ slightly (diffusion 0.7),
// but the defaults are Beads'.
constexpr float kSpeedDefault = 0.5f;    // 1x
constexpr float kDiffuseDefault = 0.5f;  // coefficient 0.625
constexpr float kDampDefault = 0.5f;     // lp 0.6
constexpr float kDepthDefault = 0.5f;    // 4 * 0.5^2 = 1x

// Tank lowpass coefficient: 1 (no damping) through 0.6 at noon to 0.15.
inline float DampLp(float v) {
  return v <= 0.5f ? 1.0f - 0.8f * v : 0.6f - 0.9f * (v - 0.5f);
}

// Allpass coefficient: 0.3 through 0.625 at noon to 0.9.
inline float DiffuseCoefficient(float v) {
  return v <= 0.5f ? 0.3f + 0.65f * v : 0.625f + 0.55f * (v - 0.5f);
}

// LFO rate multiplier: 0.1x through 1x at noon to 16x.
inline float SpeedMultiplier(float v) {
  return v <= 0.5f ? 0.1f * powf(10.0f, 2.0f * v) : powf(16.0f, 2.0f * v - 1.0f);
}

// Modulation excursion multiplier, 0 .. 4x. The tanks' delay reserves hold
// 4x (see the RobotBoy notes in the vendored reverbs).
inline float DepthMultiplier(float v) { return 4.0f * v * v; }

// Delay-length scale, 0.5x through 1x at noon to 2x: 0.5 * 4^v, written as
// 2^(2v - 1) so that noon is exactly 2^0 = 1 (whole offsets in the tanks).
constexpr float kSizeDefault = 0.5f;
inline float SizeScale(float v) { return exp2f(2.0f * v - 1.0f); }

// Pre-delay, 0 .. 500 ms, quadratic for resolution at short times.
constexpr float kPredelayDefault = 0.0f;
constexpr float kMaxPredelaySeconds = 0.5f;
inline float PredelaySeconds(float v) { return kMaxPredelaySeconds * v * v; }

struct SmearSettings {
  float amount, coefficient;
};

// The Clouds diffuser fades in over the first 70% at its own coefficient
// (0.625), then the coefficient rises to 0.8.
inline SmearSettings SmearForKnob(float v) {
  SmearSettings s;
  if (v <= 0.7f) {
    s.amount = v / 0.7f;
    s.coefficient = 0.625f;
  } else {
    s.amount = 1.0f;
    s.coefficient = 0.625f + 0.175f * (v - 0.7f) / 0.3f;
  }
  return s;
}

// Wow and flutter amount, 0 .. 3; 1 is Beads' depth at Scorched.
inline float WowAmount(float v) { return 3.0f * v * v; }

// Rises to g1 at 0.95, then plateaus at 1.15 g1 (self-oscillation).
inline float ShimmerGain(float v) {
  if (v >= 0.95f) return 1.15f * kShimmerSustainGain;
  const float x = v / 0.95f;
  return kShimmerSustainGain * x * x;
}

// Noon = original stereo.
inline float WidthGain(float v) { return 2.0f * v; }

// Beads' dry/wet laws.
inline float DryGain(float x) { return (1.0f - x) * (1.0f + x); }
inline float WetGain(float x) { return x * (2.0f - x); }

inline float IntervalSemitones(float v) { return -24.0f + 48.0f * v; }

struct FilterSettings {
  float lp, hp, lpq;
};

// Clouds' texture law with feedback 0: lowpass below noon, highpass above.
inline FilterSettings FilterForKnob(float v) {
  FilterSettings s;
  s.lp = 0.5f * stmlib::SemitonesToRatio((v < 0.5f ? v - 0.5f : 0.0f) * 216.0f);
  s.hp = 0.25f * stmlib::SemitonesToRatio((v < 0.5f ? -0.5f : v - 1.0f) * 216.0f);
  if (s.lp > 0.499f) s.lp = 0.499f;
  if (s.hp > 0.499f) s.hp = 0.499f;
  s.lpq = 1.0f + 3.0f * (0.5f - s.lp);
  return s;
}

// Knob value after CV or envelope, clamped 0..1. A patched jack replaces the
// envelope; non-finite volts read as 0.
inline float ModulatedKnob(
    float knob, bool patched, float volts, float trim, float envelope) {
  if (patched) {
    if (!std::isfinite(volts)) volts = 0.0f;
    return ClampUnit(knob + volts / 5.0f * trim);
  }
  return ClampUnit(knob + envelope * trim);
}

// INTERVAL in semitones: 1 V/oct at full trim from CV, 24 st at full trim
// from the envelope. Non-finite volts return the knob; clamped to +-48.
inline float ModulatedInterval(
    float knob_semitones, bool patched, float volts, float trim,
    float envelope) {
  float st = knob_semitones;
  if (patched) {
    if (std::isfinite(volts)) st += volts * 12.0f * trim;
  } else {
    st += envelope * trim * 24.0f;
  }
  return st < -48.0f ? -48.0f : (st > 48.0f ? 48.0f : st);
}

}  // namespace robotboy_beads

#endif  // ROBOTBOY_BEADS_BRUME_CONTROLS_H_
