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
// Cosine oscillator. Generates a cosine between 0.0 and 1.0 with minimal
// CPU use.

#ifndef STMLIB_DSP_COSINE_OSCILLATOR_H_
#define STMLIB_DSP_COSINE_OSCILLATOR_H_

#include "stmlib/stmlib.h"

#include <algorithm>
#include <cmath>

namespace stmlib {

enum CosineOscillatorMode {
  COSINE_OSCILLATOR_APPROXIMATE,
  COSINE_OSCILLATOR_EXACT
};

class CosineOscillator {
 public:
  CosineOscillator() { }
  ~CosineOscillator() { }

  template<CosineOscillatorMode mode>
  inline void Init(float frequency) {
    if (mode == COSINE_OSCILLATOR_APPROXIMATE) {
      InitApproximate(frequency);
    } else {
      iir_coefficient_ = 2.0f * cosf(2.0f * float(M_PI) * frequency);
      initial_amplitude_ = iir_coefficient_ * 0.25f;
    }
    Start();
  }
  
  inline void InitApproximate(float frequency) {
    float sign = 16.0f;
    frequency -= 0.25f;
    if (frequency < 0.0f) {
      frequency = -frequency;
    } else {
      if (frequency > 0.5f) {
        frequency -= 0.5f;
      } else {
        sign = -16.0f;
      }
    }
    iir_coefficient_ = sign * frequency * (1.0f - 2.0f * frequency);
    initial_amplitude_ = iir_coefficient_ * 0.25f;
  }
  
  // RobotBoy: changes the frequency without restarting the oscillator. The
  // phase is read from the current state and the oscillator is re-seeded at
  // that phase with the amplitude (0.5) a fresh Start() gives, so the output
  // stays within [0, 1] however often the frequency moves. Does nothing if
  // the coefficient does not change.
  inline void SetFrequencyKeepPhase(float frequency) {
    const float old_coefficient = iir_coefficient_;
    InitApproximate(frequency);
    if (iir_coefficient_ == old_coefficient) {
      return;
    }
    const double c = 0.5 * old_coefficient;
    const double s = std::sqrt(std::max(0.0, 1.0 - c * c));
    if (s < 1.0e-9) {
      Start();
      return;
    }
    const double phase = std::atan2((y1_ - y0_ * c) / s, y0_);
    const double c_new = std::min(1.0, std::max(-1.0, 0.5 * double(iir_coefficient_)));
    const double theta = std::acos(c_new);
    y0_ = float(0.5 * std::cos(phase));
    y1_ = float(0.5 * std::cos(phase - theta));
  }

  inline void Start() {
    y1_ = initial_amplitude_;
    y0_ = 0.5f;
  }
  
  inline float value() const {
    return y1_ + 0.5f;
  }

  inline float Next() {
    float temp = y0_;
    y0_ = iir_coefficient_ * y0_ - y1_;
    y1_ = temp;
    return temp + 0.5f;
  }
  
 private:
  float y1_;
  float y0_;
  float iir_coefficient_;
  float initial_amplitude_;

  DISALLOW_COPY_AND_ASSIGN(CosineOscillator);
};

}  // namespace stmlib

#endif  // STMLIB_DSP_COSINE_OSCILLATOR_H_
