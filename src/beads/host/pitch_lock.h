// Particules' "Lock pitch" quantizer, hooked into Beads through
// beads::PitchLock: it snaps each grain's pitch (in semitones, after the
// knob, CV and randomization are combined) to the nearest allowed interval.
//
// Modes: Off, Octaves, Octaves + 5ths, then five 12-tone scales, which are
// taken relative to a root (0 = C). A grain at 0 semitones plays the
// recording at its own pitch, which counts as C.

#pragma once

#include <cmath>
#include <cstdint>

#include "beads/dsp/granular_player.h"

namespace robotboy_beads {

enum PitchLockMode {
  kPitchOff = 0,
  kPitchOctaves,
  kPitchOctavesFifths,
  kPitchChromatic,
  kPitchMajor,
  kPitchMinor,
  kPitchMajorPentatonic,
  kPitchMinorPentatonic,
};

// A scale mode's degrees in semitones above the root, without the unison
// and ending with the octave (12). nullptr (count 0) for the other modes.
inline const int* ScaleSemitones(int mode, uint32_t* count) {
  static const int kChromatic[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  static const int kMajor[] = {2, 4, 5, 7, 9, 11, 12};
  static const int kMinor[] = {2, 3, 5, 7, 8, 10, 12};
  static const int kMajorPentatonic[] = {2, 4, 7, 9, 12};
  static const int kMinorPentatonic[] = {3, 5, 7, 10, 12};
  switch (mode) {
    case kPitchChromatic: *count = 12; return kChromatic;
    case kPitchMajor: *count = 7; return kMajor;
    case kPitchMinor: *count = 7; return kMinor;
    case kPitchMajorPentatonic: *count = 5; return kMajorPentatonic;
    case kPitchMinorPentatonic: *count = 5; return kMinorPentatonic;
    default: *count = 0; return nullptr;
  }
}

class ScalePitchLock : public beads::PitchLock {
 public:
  void Set(int mode, int root) {
    mode_ = mode;
    root_ = ((root % 12) + 12) % 12;
    for (bool& a : allowed_) a = false;
    allowed_[0] = true;
    if (mode == kPitchOctavesFifths) {
      allowed_[7] = true;
    } else if (mode >= kPitchChromatic) {
      uint32_t count = 0;
      const int* degrees = ScaleSemitones(mode, &count);
      for (uint32_t i = 0; degrees && i < count; ++i) {
        allowed_[degrees[i] % 12] = true;
      }
    }
  }

  // Octaves and Octaves + 5ths always refer to the recording's own pitch;
  // only the scale modes use the root.
  bool active() const { return mode_ != kPitchOff; }

  const beads::PitchLock* hook() const { return active() ? this : nullptr; }

  float Quantize(float semitones) const override {
    if (!std::isfinite(semitones)) return 0.0f;
    const int root = mode_ >= kPitchChromatic ? root_ : 0;
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
  int mode_ = kPitchOff;
  int root_ = 0;
  bool allowed_[12] = {true};
};

}  // namespace robotboy_beads
