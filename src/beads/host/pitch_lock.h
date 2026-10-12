// Particules' "Lock pitch" quantizer, hooked into Beads through
// beads::PitchLock: it snaps each grain's pitch (in semitones, after the
// knob, CV and randomization are combined) to the nearest allowed interval.
//
// Modes follow particules_scales.h: Off, Octaves, Octaves + 5ths, then the
// five 12-tone scales, which are taken relative to a root (0 = C). A grain
// at 0 semitones plays the recording at its own pitch, which counts as C.

#pragma once

#include <cmath>

#include "beads/dsp/granular_player.h"
#include "particules_scales.h"

namespace robotboy_beads {

class ScalePitchLock : public beads::PitchLock {
 public:
  void Set(int mode, int root) {
    mode_ = mode;
    root_ = ((root % 12) + 12) % 12;
    for (bool& a : allowed_) a = false;
    allowed_[0] = true;
    if (mode == particules::kPitchOctavesFifths) {
      allowed_[7] = true;
    } else if (mode >= particules::kPitchChromatic) {
      uint32_t count = 0;
      const int* degrees = particules::ScaleSemitones(mode, &count);
      for (uint32_t i = 0; degrees && i < count; ++i) {
        allowed_[degrees[i] % 12] = true;
      }
    }
  }

  // Octaves and Octaves + 5ths always refer to the recording's own pitch;
  // only the scale modes use the root.
  bool active() const { return mode_ != particules::kPitchOff; }

  const beads::PitchLock* hook() const { return active() ? this : nullptr; }

  float Quantize(float semitones) const override {
    if (!std::isfinite(semitones)) return 0.0f;
    const int root = mode_ >= particules::kPitchChromatic ? root_ : 0;
    // The nearest allowed value is within 12 semitones; scan both ways from
    // the rounded pitch and keep the closest (ties go downward).
    const int center = int(std::lround(semitones));
    float best = float(center);
    float best_distance = 1e9f;
    for (int offset = -12; offset <= 12; ++offset) {
      const int candidate = center + offset;
      const int degree = (((candidate - root) % 12) + 12) % 12;
      if (!allowed_[degree]) continue;
      const float distance = std::fabs(float(candidate) - semitones);
      if (distance < best_distance) {
        best_distance = distance;
        best = float(candidate);
      }
    }
    return best;
  }

 private:
  int mode_ = particules::kPitchOff;
  int root_ = 0;
  bool allowed_[12] = {true};
};

}  // namespace robotboy_beads
