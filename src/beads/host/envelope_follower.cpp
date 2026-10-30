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
// RobotBoy: see envelope_follower.h.
#include "host/envelope_follower.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

namespace robotboy_beads {

using namespace std;

namespace {

const float kIgnoreTime = 1.0f;  // second
const float kActiveTime = 5.0f;  // second
// A silent source can't be scaled up past 1 / kPeakFloor.
const float kPeakFloor = 0.01f;

}  // namespace

void EnvelopeFollower::Init(float sample_rate) {
  rise_s_ = FollowerTime(0.25f);
  fall_s_ = FollowerTime(0.5f);
  raw_ = 0.0f;
  peak_ = stable_peak_ = 1.0f;
  time_ = 0;
  auto_gain_ = true;
  sample_rate_ = 0.0f;
  SetSampleRate(sample_rate);
}

void EnvelopeFollower::SetSampleRate(float sample_rate) {
  // Keep the elapsed fraction of the learning phase across a rate change.
  const float ratio = sample_rate_ > 0.0f ? sample_rate / sample_rate_ : 1.0f;
  time_ = size_t(float(time_) * ratio);
  sample_rate_ = sample_rate;
  ignore_time_ = size_t(sample_rate * kIgnoreTime);
  active_time_ = size_t(sample_rate * (kIgnoreTime + kActiveTime));
  one_hertz_ = 1.0f / sample_rate;
  UpdateCoefficients();
}

void EnvelopeFollower::UpdateCoefficients() {
  rise_coef_ = 1.0f - expf(-1.0f / (max(rise_s_, 1e-4f) * sample_rate_));
  fall_coef_ = 1.0f - expf(-1.0f / (max(fall_s_, 1e-4f) * sample_rate_));
}

float EnvelopeFollower::Process(float rectified) {
  const float x = rectified > 0.0f ? rectified : 0.0f;
  raw_ += (x > raw_ ? rise_coef_ : fall_coef_) * (x - raw_);

  if (!auto_gain_) {
    return min(raw_, 1.0f);
  }

  if (time_ < ignore_time_) {
    peak_ = 1.0f;
  } else if (time_ < active_time_) {
    SLOPE(peak_, raw_, 10.0f * one_hertz_, 2.0f * one_hertz_);
    stable_peak_ = peak_;
  } else {
    SLOPE(
        peak_,
        raw_,
        5.0f * one_hertz_,
        (raw_ > stable_peak_ ? 0.5f : 0.01f) * one_hertz_);
  }
  // As in Beads, silence doesn't advance the learning phase.
  if (time_ > ignore_time_ || raw_ > 0.001f) {
    time_ = min(time_ + 1, active_time_);
  }

  const float scaled = raw_ / max(peak_, kPeakFloor);
  return min(max(scaled, 0.0f), 1.0f);
}

}  // namespace robotboy_beads
