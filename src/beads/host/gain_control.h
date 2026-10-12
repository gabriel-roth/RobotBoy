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
// RobotBoy: adapted from beads/gain_control.{h,cc}. The hardware version
// reads 32-bit codec words; this one takes float frames already scaled so
// that +-5 V = 1.0, and exposes the current gain for Particules' menu.
// Everything else is upstream.
//
// Converts raw 32-bit data from the codec into a FloatFrame, by applying
// the correct gain constant. Also responsible for filling in right
// channel data with left channel data (or the opposite) when either channel
// is left unpatched.

#ifndef ROBOTBOY_BEADS_GAIN_CONTROL_H_
#define ROBOTBOY_BEADS_GAIN_CONTROL_H_

#include "stmlib/stmlib.h"

#include "beads/dsp/frame.h"

namespace robotboy_beads {

using beads::FloatFrame;

class GainControl {
 public:
  // RobotBoy: the firmware's instance is a zero-initialized global, and
  // Init() reads time_ and Process() smooths manual_gain_ from that zero
  // state; do the same for instances that live on the heap or stack.
  GainControl()
      : peak_(0.0f), agc_peak_(0.0f), stable_agc_peak_(0.0f), gain_(0.0f),
        soft_clipping_amount_(0.0f),
        soft_clipping_mode_(SOFT_CLIPPING_MODE_OFF),
        auto_gain_(false), manual_gain_(0.0f), one_hertz_(0.0f),
        time_(0), ignore_time_(0), active_time_(0) { }
  ~GainControl() { }
  
  enum State {
    GAIN_CONTROL_STATE_IGNORE_FIRST_SAMPLES,
    GAIN_CONTROL_STATE_ENABLED,
    GAIN_CONTROL_STATE_DONE
  };
  
  enum SoftClippingMode {
    SOFT_CLIPPING_MODE_OFF,
    SOFT_CLIPPING_MODE_ON,
    SOFT_CLIPPING_MODE_DYNAMIC
  };
  
  void Init(float sample_rate, SoftClippingMode soft_clipping_mode, bool reset);
  
  void Restart() {
    time_ = ignore_time_;
  }
  
  void Process(
      bool active,
      uint8_t gain,
      bool left_normalization,
      bool right_normalization,
      const FloatFrame* in,
      FloatFrame* out,
      size_t size);

  inline uint8_t vu_meter() const {
    float intensity = peak_ * gain_;
    if (intensity > 1.0f) {
      return 255;
    } else {
      return uint8_t(192.0f * intensity * intensity);
    }
  }
  
  // RobotBoy: the gain currently applied (linear).
  inline float gain() const { return gain_; }
  
  inline bool adjusting() const {
    return time_ < active_time_ && time_ > 0 && auto_gain_;
  }

 private:
  float peak_;
  float agc_peak_;
  float stable_agc_peak_;
  float gain_;

  float soft_clipping_amount_;
  SoftClippingMode soft_clipping_mode_;
  
  bool auto_gain_;
  float manual_gain_;
  
  float one_hertz_;
  
  size_t time_;
  size_t ignore_time_;
  size_t active_time_;
  
  DISALLOW_COPY_AND_ASSIGN(GainControl);
};

}  // namespace robotboy_beads

#endif  // ROBOTBOY_BEADS_GAIN_CONTROL_H_