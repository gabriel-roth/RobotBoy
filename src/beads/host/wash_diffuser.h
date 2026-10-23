// Copyright 2014 Emilie Gillet.
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
// RobotBoy: Brume's SMEAR wash, a longer relative of Clouds' diffuser
// (clouds/dsp/fx/diffuser.h): eight allpass stages per channel in the same
// form, with coefficient 0.7 and lengths in milliseconds, so it sounds the
// same at every tank rate. The three longest stages drift by 0.4 ms on slow,
// unrelated sine LFOs (R at the opposite sign to L), read through first-order
// allpass interpolation so the loop loses no treble. It runs after the stock
// diffuser and is crossfaded in by amount. At amount 0 the frames are left
// untouched, and the chain runs on silence for 2 s to drain, then sleeps
// until amount rises again, so it wakes up nearly empty (about -85 dB).

#ifndef ROBOTBOY_BEADS_WASH_DIFFUSER_H_
#define ROBOTBOY_BEADS_WASH_DIFFUSER_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "clouds/dsp/frame.h"

namespace robotboy_beads {

class WashDiffuser {
 public:
  enum {
    kNumStages = 8,
    kNumModulated = 3,  // the last (longest) stages
    kLineSize = 4096,   // 53.7 ms + 0.4 ms at 48 kHz, with room to spare
  };
  static constexpr size_t kMemorySize = 2 * kNumStages * kLineSize;

  // memory holds kMemorySize floats. rate is the tank rate.
  void Init(float* memory, float rate) {
    static const float kLengthMs[2][kNumStages] = {
        {7.3f, 11.1f, 15.7f, 21.9f, 29.3f, 37.1f, 44.9f, 53.7f},
        {8.1f, 12.3f, 14.6f, 23.8f, 27.4f, 39.6f, 42.2f, 51.3f},
    };
    memory_ = memory;
    for (int ch = 0; ch < 2; ++ch) {
      for (int i = 0; i < kNumStages; ++i) {
        length_[ch][i] = std::round(kLengthMs[ch][i] * rate / 1000.0f);
      }
    }
    depth_ = kDepthMs * rate / 1000.0f;
    for (int i = 0; i < kNumModulated; ++i) {
      const int stage = kNumStages - kNumModulated + i;
      const float hz = 0.4f * (0.17f + 0.0667f * float(stage));
      const float w = 2.0f * 3.14159265f * hz / rate;
      rotate_cos_[i] = std::cos(w);
      rotate_sin_[i] = std::sin(w);
      lfo_cos_[i] = std::cos(1.3f * float(stage));
      lfo_sin_[i] = std::sin(1.3f * float(stage));
    }
    drain_samples_ = int32_t(kDrainSeconds * rate);
    Clear();
  }

  void Clear() {
    std::fill(memory_, memory_ + kMemorySize, 0.0f);
    std::fill(&interpolator_[0][0], &interpolator_[0][0] + 2 * kNumModulated,
              0.0f);
    idle_ = drain_samples_;  // asleep, and silent
  }

  void Process(clouds::FloatFrame* frames, size_t size, float amount) {
    if (amount > 0.0f) {
      idle_ = 0;
    } else if (idle_ >= drain_samples_) {
      return;
    } else {
      idle_ += int32_t(size);
    }
    for (int i = 0; i < kNumModulated; ++i) {
      const float n = 1.0f / std::sqrt(
          lfo_cos_[i] * lfo_cos_[i] + lfo_sin_[i] * lfo_sin_[i]);
      lfo_cos_[i] *= n;
      lfo_sin_[i] *= n;
    }
    for (size_t s = 0; s < size; ++s) {
      for (int i = 0; i < kNumModulated; ++i) {
        const float c = lfo_cos_[i] * rotate_cos_[i] -
            lfo_sin_[i] * rotate_sin_[i];
        lfo_sin_[i] = lfo_sin_[i] * rotate_cos_[i] +
            lfo_cos_[i] * rotate_sin_[i];
        lfo_cos_[i] = c;
      }
      if (amount > 0.0f) {
        frames[s].l += amount * (Chain(0, frames[s].l) - frames[s].l);
        frames[s].r += amount * (Chain(1, frames[s].r) - frames[s].r);
      } else {
        // Draining: unheard, and fed silence so it empties.
        Chain(0, 0.0f);
        Chain(1, 0.0f);
      }
      ++write_;
    }
  }

 private:
  static constexpr float kCoefficient = 0.7f;
  static constexpr float kDepthMs = 0.4f;
  static constexpr float kDrainSeconds = 2.0f;

  float Chain(int ch, float x) {
    const int first_modulated = kNumStages - kNumModulated;
    for (int i = 0; i < kNumStages; ++i) {
      float* line = &memory_[(ch * kNumStages + i) * kLineSize];
      float d;
      if (i < first_modulated) {
        d = line[(write_ - uint32_t(length_[ch][i])) & (kLineSize - 1)];
      } else {
        // First-order allpass interpolation, with the fraction kept in
        // 0.5 .. 1.5 where it behaves.
        const int m = i - first_modulated;
        const float lfo = ch ? -lfo_sin_[m] : lfo_sin_[m];
        const float delay = length_[ch][i] + depth_ * lfo;
        const uint32_t whole = uint32_t(delay - 0.5f);
        const float fraction = delay - float(whole);
        const float eta = (1.0f - fraction) / (1.0f + fraction);
        float& y = interpolator_[ch][m];
        y = eta * line[(write_ - whole) & (kLineSize - 1)] +
            line[(write_ - whole - 1) & (kLineSize - 1)] - eta * y;
        d = y;
      }
      const float v = x + kCoefficient * d;
      line[write_ & (kLineSize - 1)] = v;
      x = d - kCoefficient * v;
    }
    return x;
  }

  float* memory_ = nullptr;
  uint32_t write_ = 0;
  float length_[2][kNumStages] = {};
  float depth_ = 0.0f;
  float rotate_cos_[kNumModulated] = {};
  float rotate_sin_[kNumModulated] = {};
  float lfo_cos_[kNumModulated] = {};
  float lfo_sin_[kNumModulated] = {};
  float interpolator_[2][kNumModulated] = {};
  int32_t drain_samples_ = 0;
  int32_t idle_ = 0;
};

}  // namespace robotboy_beads

#endif  // ROBOTBOY_BEADS_WASH_DIFFUSER_H_
