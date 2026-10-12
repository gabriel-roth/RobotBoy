// Arbitrary-ratio streaming resampler (Kaiser-windowed sinc, table-driven).
//
// The Beads engine runs at the hardware's native rate for each quality
// setting (48, 32 or 24 kHz). This converts between that rate and whatever
// rate the host runs at, identically on VCV and MetaModule so both platforms
// produce the same numbers. When the two rates are equal it is a
// pass-through and adds no latency.
//
// Usage: Push() one input frame, then Pop() until it returns false.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace robotboy_beads {

constexpr double kPi = 3.14159265358979323846;

template <int num_channels>
class Resampler {
 public:
  // Zero crossings of the sinc on each side of the centre tap, measured at
  // the lower of the two rates. 16 keeps the passband flat to ~0.37 of the
  // lower rate (Beads' own 2x decimator in "Sunny tape" stops at ~0.26).
  static constexpr int kZeroCrossings = 16;
  static constexpr float kCutoff = 0.45f;     // fraction of the lower rate
  static constexpr float kKaiserBeta = 7.0f;
  // The kernel table is sampled per zero crossing, so its size doesn't
  // depend on the ratio.
  static constexpr int kTablePointsPerCrossing = 128;
  static constexpr int kTableSize = kZeroCrossings * kTablePointsPerCrossing + 2;
  // Largest kernel half-width, in input samples: covers downsampling by
  // up to 32:1 (a 768 kHz host into the 24 kHz engine).
  static constexpr int kMaxHalfWidth =
      int(kZeroCrossings * 32 / (2 * kCutoff)) + 2;
  static constexpr int kRingSize = 2048;  // power of two, > 2 * kMaxHalfWidth

  // Rebuilds the kernel only when the rates change; always clears the
  // stream (Init on the audio thread is cheap when the rates are the same).
  void Init(float in_rate, float out_rate) {
    if (in_rate != in_rate_ || out_rate != out_rate_) {
      in_rate_ = in_rate;
      out_rate_ = out_rate;
      bypass_ = in_rate == out_rate;
      step_ = double(in_rate) / double(out_rate);
      if (!bypass_) {
        // Cutoff in cycles per input sample.
        fc_ = kCutoff * std::min(1.0f, out_rate / in_rate);
        half_width_ = std::min(
            kMaxHalfWidth, int(std::ceil(kZeroCrossings / (2.0f * fc_))));
        // Kernel in units of zero crossings: z = 2 * fc * x.
        const double i0_beta = BesselI0(kKaiserBeta);
        for (int k = 0; k < kTableSize; ++k) {
          const double z = double(k) / kTablePointsPerCrossing;
          const double u = z / kZeroCrossings;
          double h = 0.0;
          if (u < 1.0) {
            const double sinc = z == 0.0
                ? 1.0
                : std::sin(kPi * z) / (kPi * z);
            const double window =
                BesselI0(kKaiserBeta * std::sqrt(1.0 - u * u)) / i0_beta;
            h = 2.0 * fc_ * sinc * window;
          }
          table_[k] = float(h);
        }
        index_scale_ = 2.0f * fc_ * float(kTablePointsPerCrossing);
      }
    }
    Reset();
  }

  void Reset() {
    for (auto& frame : ring_) {
      frame.fill(0.0f);
    }
    in_count_ = 0;
    t_integral_ = 0;
    t_fractional_ = 0.0;
    pending_ = false;
  }

  bool bypass() const { return bypass_; }

  // Group delay, in output samples.
  float delay_in_output_samples() const {
    return bypass_ ? 0.0f : float(half_width_ / step_);
  }

  void Push(const float* frame) {
    if (bypass_) {
      std::copy(frame, frame + num_channels, pending_frame_.begin());
      pending_ = true;
      return;
    }
    std::copy(frame, frame + num_channels, ring_[in_count_ & kMask].begin());
    ++in_count_;
  }

  bool Pop(float* frame) {
    if (bypass_) {
      if (!pending_) {
        return false;
      }
      std::copy(pending_frame_.begin(), pending_frame_.end(), frame);
      pending_ = false;
      return true;
    }

    // The output at input time t needs inputs up to floor(t) + half_width.
    if (t_integral_ + half_width_ > in_count_ - 1) {
      return false;
    }

    std::array<float, num_channels> acc;
    acc.fill(0.0f);
    const float fractional = float(t_fractional_);
    for (int64_t j = t_integral_ - half_width_ + 1;
         j <= t_integral_ + half_width_; ++j) {
      // Distance from the output instant to input sample j.
      const float d = std::fabs(fractional + float(t_integral_ - j));
      const float index = std::min(d * index_scale_, float(kTableSize - 2));
      const int index_integral = int(index);
      const float index_fractional = index - float(index_integral);
      const float h = table_[index_integral] + \
          (table_[index_integral + 1] - table_[index_integral]) * \
          index_fractional;
      const auto& x = ring_[j & kMask];
      for (int c = 0; c < num_channels; ++c) {
        acc[c] += h * x[c];
      }
    }
    std::copy(acc.begin(), acc.end(), frame);

    t_fractional_ += step_;
    const double whole = std::floor(t_fractional_);
    t_integral_ += int64_t(whole);
    t_fractional_ -= whole;
    return true;
  }

 private:
  static constexpr int64_t kMask = kRingSize - 1;
  static_assert((kRingSize & (kRingSize - 1)) == 0, "ring must be 2^n");
  static_assert(kRingSize > 2 * kMaxHalfWidth + 2, "ring too small");

  static double BesselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 64; ++k) {
      term *= (x / (2.0 * k)) * (x / (2.0 * k));
      sum += term;
      if (term < 1e-12 * sum) {
        break;
      }
    }
    return sum;
  }

  float in_rate_ = 0.0f;
  float out_rate_ = 0.0f;
  bool bypass_ = true;
  double step_ = 1.0;
  float fc_ = 0.45f;
  float index_scale_ = 1.0f;
  int half_width_ = 1;

  std::array<float, kTableSize> table_ = {};
  std::array<std::array<float, num_channels>, kRingSize> ring_ = {};
  int64_t in_count_ = 0;
  int64_t t_integral_ = 0;
  double t_fractional_ = 0.0;

  std::array<float, num_channels> pending_frame_ = {};
  bool pending_ = false;
};

}  // namespace robotboy_beads
