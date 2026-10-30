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
// RobotBoy: a one-pole envelope follower for Brume. The optional auto-gain is
// adapted from beads/gain_control.cc (see host/gain_control.cpp): the same
// ignore / learn / hold state machine and slopes, applied to the envelope
// instead of to the audio.

#ifndef ROBOTBOY_BEADS_ENVELOPE_FOLLOWER_H_
#define ROBOTBOY_BEADS_ENVELOPE_FOLLOWER_H_

#include <cmath>
#include <cstddef>

#include "stmlib/stmlib.h"

namespace robotboy_beads {

// Rise / fall time constant in seconds for a 0..1 knob (10 ms .. 5 s).
inline float FollowerTime(float knob) {
  return 0.01f * powf(500.0f, knob);
}

class EnvelopeFollower {
 public:
  EnvelopeFollower() { }
  ~EnvelopeFollower() { }

  void Init(float sample_rate);
  void SetSampleRate(float sample_rate);

  void set_times(float rise_s, float fall_s) {
    rise_s_ = rise_s;
    fall_s_ = fall_s;
    UpdateCoefficients();
  }

  // Switching auto-gain from off to on restarts learning.
  void set_auto_gain(bool on) {
    if (on && !auto_gain_) Restart();
    auto_gain_ = on;
  }

  void Restart() { time_ = 0; }

  // Takes a rectified sample (1.0 = full scale), returns the envelope, 0..1.
  float Process(float rectified);

  float raw() const { return raw_; }

 private:
  void UpdateCoefficients();

  float sample_rate_;
  float rise_s_;
  float fall_s_;
  float rise_coef_;
  float fall_coef_;
  float one_hertz_;
  size_t ignore_time_;
  size_t active_time_;

  float raw_;
  float peak_;
  float stable_peak_;
  size_t time_;
  bool auto_gain_;

  DISALLOW_COPY_AND_ASSIGN(EnvelopeFollower);
};

}  // namespace robotboy_beads

#endif  // ROBOTBOY_BEADS_ENVELOPE_FOLLOWER_H_
