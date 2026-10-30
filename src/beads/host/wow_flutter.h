// Copyright 2018 Emilie Gillet.
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
// RobotBoy: a host-rate-independent port of beads/dsp/fx/wow_and_flutter.h.
// Same LFOs, random FM and re-roll as upstream, updated 4000 times a second;
// the delay is Beads' (at 24 kHz) converted to host samples, and the output
// lowpass is recomputed for the host rate. Upstream's 128-sample delay line is
// replaced by a ring that fits 3x Beads' depth at 768 kHz, read with linear
// interpolation, and the stage crossfades to a bit-exact bypass over 5 ms
// when amount is 0.

#ifndef ROBOTBOY_BEADS_WOW_FLUTTER_H_
#define ROBOTBOY_BEADS_WOW_FLUTTER_H_

#include <cmath>
#include <cstddef>

#include "stmlib/stmlib.h"
#include "stmlib/utils/random.h"

#include "beads/dsp/sine_oscillator.h"

namespace robotboy_beads {

class WowFlutter {
 public:
  WowFlutter() { }
  ~WowFlutter() { }

  void Init(float host_rate) {
    host_rate_ = host_rate;
    block_size_ = static_cast<size_t>(host_rate / kControlRate + 0.5f);
    if (block_size_ < 1) block_size_ = 1;
    one_hertz_ = 1.0f / kControlRate;
    delay_scale_ = host_rate / kBeadsRate;
    lp_ = 1.0f - std::exp(-2.0f * 3.14159265358979f * kLpHertz / host_rate);
    fade_step_ = 1.0f / (kFadeSeconds * host_rate);
    wow_.Init();
    flutter_.Init();
    random_modulation_.Init();
    random_frequency_ = 0.125f * one_hertz_;
    Reset();
  }

  void Reset() {
    for (size_t i = 0; i < kRingSize; ++i) {
      ring_[0][i] = ring_[1][i] = 0.0f;
    }
    write_ = 0;
    lp_l_ = lp_r_ = 0.0f;
    mix_ = 0.0f;
    gain_ = 1.0f;
    gain_target_ = 1.0f;
    gain_step_ = 0.0f;
    counter_ = 0;
  }

  // amount in 0..3 (1 is Beads' depth), smoothed by the caller. Per sample,
  // in place.
  void Process(float* l, float* r, float amount) {
    if (counter_ == 0) {
      UpdateModulation();
      counter_ = block_size_;
    }
    --counter_;
    gain_ += gain_step_;

    ring_[0][write_] = *l;
    ring_[1][write_] = *r;

    // Total delay in host samples; the ring has room for amount 3 at 768 kHz.
    const float delay = gain_ * amount * delay_scale_;
    const float read = static_cast<float>(write_) + kRingSize - delay;
    const size_t i0 = static_cast<size_t>(read);
    const float frac = read - static_cast<float>(i0);
    const size_t a = i0 & kRingMask;
    const size_t b = (i0 + 1) & kRingMask;
    // a is the older sample of the pair for a positive delay.
    const float wet_l = ring_[0][a] + (ring_[0][b] - ring_[0][a]) * frac;
    const float wet_r = ring_[1][a] + (ring_[1][b] - ring_[1][a]) * frac;
    write_ = (write_ + 1) & kRingMask;

    lp_l_ += lp_ * (wet_l - lp_l_);
    lp_r_ += lp_ * (wet_r - lp_r_);

    // The processed path always runs, so crossing zero fades between two
    // live signals.
    const float target = amount > 0.0f ? 1.0f : 0.0f;
    if (mix_ < target) {
      mix_ = mix_ + fade_step_ > target ? target : mix_ + fade_step_;
    } else if (mix_ > target) {
      mix_ = mix_ - fade_step_ < target ? target : mix_ - fade_step_;
    }
    if (mix_ > 0.0f) {
      *l += (lp_l_ - *l) * mix_;
      *r += (lp_r_ - *r) * mix_;
    }
  }

 private:
  static constexpr float kControlRate = 4000.0f;
  static constexpr float kBeadsRate = 24000.0f;
  static constexpr float kLpHertz = 11443.0f;  // Beads' 0.95 coefficient at 24 kHz.
  static constexpr float kFadeSeconds = 0.005f;
  // 768 kHz: 3 x 5.13 ms is 11808 frames.
  static constexpr size_t kRingSize = 16384;
  static constexpr size_t kRingMask = kRingSize - 1;

  // One upstream Process() call's LFO work, for a block of block_size_
  // samples. Sets up a linear ramp of the delay factor across the next block.
  void UpdateModulation() {
    const float size = static_cast<float>(block_size_);
    const float previous_phase = random_modulation_.phase();
    const float random_modulation = random_modulation_.Render(
        random_frequency_ * size);
    if (random_modulation_.phase() >= 0.25f && previous_phase < 0.25f) {
      random_frequency_ = one_hertz_ * \
          (0.05f + stmlib::Random::GetFloat() * 0.2f);
    }
    const float fm = 0.5f + 1.5f * random_modulation * random_modulation;
    const float depth = 1.0f - 0.4f * random_modulation;
    const float wow = wow_.Render(one_hertz_ * 0.23f * size * fm);
    const float flutter = flutter_.Render(one_hertz_ * 1.99f * size);
    gain_target_ = (1.0f + 105.0f * wow + 17.0f * flutter) * depth;
    gain_step_ = (gain_target_ - gain_) / size;
  }

  float host_rate_;
  float one_hertz_;
  float delay_scale_;
  float lp_;
  float fade_step_;
  size_t block_size_;
  size_t counter_;

  float random_frequency_;
  float gain_;
  float gain_target_;
  float gain_step_;
  float mix_;
  float lp_l_, lp_r_;

  beads::SineOscillator random_modulation_;
  beads::SineOscillator wow_;
  beads::SineOscillator flutter_;

  float ring_[2][kRingSize];
  size_t write_;

  DISALLOW_COPY_AND_ASSIGN(WowFlutter);
};

}  // namespace robotboy_beads

#endif  // ROBOTBOY_BEADS_WOW_FLUTTER_H_
