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

// Full = unity (the originals' level); turning down reduces what enters the
// reverb.
constexpr float kSendDefault = 1.0f;
inline float SendGain(float v) { return v; }

// 0..0.95 maps linearly over the Mode's range; the last 5% holds at 1.0.
inline float DecayTime(float v, BrumeMode mode) {
  if (v > 0.95f) return 1.0f;
  const float lo = mode == BrumeMode::kBeads ? 0.30f : 0.35f;
  const float hi = mode == BrumeMode::kBeads ? 0.95f : 0.98f;
  return lo + (hi - lo) * v / 0.95f;
}

// Knob positions that reproduce the original reverb of either Mode: noon,
// except DIFFUSE, whose top is the original (it maps per Mode).
constexpr float kSpeedDefault = 0.5f;    // 1x
constexpr float kDiffuseDefault = 1.0f;  // coefficient 0.625 / 0.7
constexpr float kDampDefault = 0.5f;     // lp 0.6
constexpr float kDepthDefault = 0.5f;    // 4 * 0.5^2 = 1x

// Tank lowpass coefficient: 1 (no damping) through 0.6 at noon to 0.15.
inline float DampLp(float v) {
  return v <= 0.5f ? 1.0f - 0.8f * v : 0.6f - 0.9f * (v - 0.5f);
}

// Allpass coefficient: 0.3 up to the Mode's original at full (Beads 0.625,
// Clouds 0.7). Above the originals the allpasses ring more than they
// diffuse, so the knob stops there. Written so that full is exactly the
// original.
inline float DiffuseCoefficient(float v, BrumeMode mode) {
  const float top = mode == BrumeMode::kBeads ? 0.625f : 0.7f;
  return top - (top - 0.3f) * (1.0f - v);
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

// amount: the stock Clouds diffuser's mix; wash: WashDiffuser's mix.
struct SmearSettings {
  float amount, wash;
};

// The Clouds diffuser fades in over the first quarter and is exactly stock
// there; the wash then fades in over the rest.
inline SmearSettings SmearForKnob(float v) {
  SmearSettings s;
  if (v <= 0.25f) {
    s.amount = v / 0.25f;
    s.wash = 0.0f;
  } else {
    s.amount = 1.0f;
    s.wash = (v - 0.25f) / 0.75f;
  }
  return s;
}

// Wow and flutter amount, 0 .. 3; 1 is Beads' depth at Scorched.
inline float WowAmount(float v) { return 3.0f * v * v; }

// Noon = original stereo.
inline float WidthGain(float v) { return 2.0f * v; }

// Beads' dry/wet laws.
inline float DryGain(float x) { return (1.0f - x) * (1.0f + x); }
inline float WetGain(float x) { return x * (2.0f - x); }

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

}  // namespace robotboy_beads

#endif  // ROBOTBOY_BEADS_BRUME_CONTROLS_H_
