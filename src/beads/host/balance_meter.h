// Brume's dry/wet balance light: compares the levels of the dry and wet
// parts of the output and colours the light by which is louder. White when
// they are within kBalanceEven dB of each other, blending to cyan (wet
// louder) or amber (dry louder) by kBalanceFull dB. The louder part sets the
// brightness, so silence goes dark.
//
// Levels are in engine units (1 = 5 V), as mean squares smoothed over
// about 100 ms so the light holds steady.

#pragma once

#include <algorithm>
#include <cmath>

namespace robotboy_beads {

inline float BalanceClamp01(float x) {
  return std::min(std::max(x, 0.0f), 1.0f);
}

constexpr float kBalanceEven = 1.5f;     // dB either side of equal: white
constexpr float kBalanceFull = 12.0f;    // dB: fully cyan or amber
constexpr float kBalanceDark = -48.0f;   // dB re 5 V: off
constexpr float kBalanceBright = -24.0f; // dB re 5 V: full brightness
constexpr float kBalanceTime = 0.1f;     // s

constexpr float kBalanceWet[3] = {0.0f, 0.8f, 1.0f};   // cyan
constexpr float kBalanceDry[3] = {1.0f, 0.55f, 0.0f};  // amber

// RGB for mean-square levels `dry` and `wet`.
inline void BalanceColor(float dry, float wet, float rgb[3]) {
  const float floor = 1e-12f;
  const float dry_db = 10.0f * std::log10(std::max(dry, floor));
  const float wet_db = 10.0f * std::log10(std::max(wet, floor));
  const float loud_db = std::max(dry_db, wet_db);
  const float brightness = BalanceClamp01(
      (loud_db - kBalanceDark) / (kBalanceBright - kBalanceDark));
  const float diff = wet_db - dry_db;
  const float blend = BalanceClamp01(
      (std::fabs(diff) - kBalanceEven) / (kBalanceFull - kBalanceEven));
  const float* tint = diff > 0.0f ? kBalanceWet : kBalanceDry;
  for (int c = 0; c < 3; ++c) {
    rgb[c] = brightness * (1.0f + blend * (tint[c] - 1.0f));
  }
}

class BalanceMeter {
 public:
  void Init(float sample_rate) {
    coefficient_ = 1.0f - std::exp(-1.0f / (kBalanceTime * sample_rate));
    dry_ = 0.0f;
    wet_ = 0.0f;
  }

  // Once per sample, with the dry and wet parts of the output.
  void Process(float dry_l, float dry_r, float wet_l, float wet_r) {
    const float dry = 0.5f * (dry_l * dry_l + dry_r * dry_r);
    const float wet = 0.5f * (wet_l * wet_l + wet_r * wet_r);
    dry_ += coefficient_ * (dry - dry_);
    wet_ += coefficient_ * (wet - wet_);
  }

  void Color(float rgb[3]) const { BalanceColor(dry_, wet_, rgb); }

  float dry() const { return dry_; }
  float wet() const { return wet_; }

 private:
  float coefficient_ = 1.0f;
  float dry_ = 0.0f;
  float wet_ = 0.0f;
};

}  // namespace robotboy_beads
