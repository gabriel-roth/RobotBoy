// Arbitrary-ratio streaming resampler (Kaiser-windowed sinc).
//
// The Beads engine runs at the hardware's native rate for each quality
// setting (48, 32 or 24 kHz). This converts between that rate and whatever
// rate the host runs at, identically on VCV and MetaModule so both platforms
// produce the same numbers. When the two rates are equal it is a
// pass-through and adds no latency.
//
// When the ratio reduces to a fraction with a small denominator (every
// common pair: 48k <-> 32k is 3:2, 44.1k <-> 32k is 441:320), the output
// instants fall on only that many positions between input samples, so the
// kernel is sampled once per position at Init, and each output is a plain
// multiply-accumulate. Other ratios read the kernel table at every tap.
//
// The last num_held channels aren't filtered: each output takes the
// nearest input frame's value (for gate streams, where a sinc would only
// smear an edge that a threshold then puts back at the same place).
//
// Usage: Push() one input frame, then Pop() until it returns false.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace robotboy_beads {

constexpr double kPi = 3.14159265358979323846;

template <int num_channels, int num_held = 0>
class Resampler {
 public:
  static constexpr int kFrameSize = num_channels + num_held;
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
  // Room for the per-position kernels (positions x taps): 44.1k -> 32k,
  // the largest common case, needs 320 x 50.
  static constexpr int kMaxPolyphaseCoefficients = 16384;

  // Allocates the polyphase storage and computes the kernel shape here,
  // so Init never allocates and only scales the shape (Init runs on the
  // audio thread at every Quality change; the Bessel series in double
  // precision took milliseconds on MetaModule).
  Resampler() {
    coefficients_.reserve(kMaxPolyphaseCoefficients);
    KernelShape();
  }

  // Rebuilds the kernel only when the rates change; always clears the
  // stream (Init on the audio thread is cheap when the rates are the same).
  void Init(float in_rate, float out_rate) {
    if (in_rate != in_rate_ || out_rate != out_rate_) {
      in_rate_ = in_rate;
      out_rate_ = out_rate;
      bypass_ = in_rate == out_rate;
      step_ = double(in_rate) / double(out_rate);
      polyphase_ = false;
      if (!bypass_) {
        // Cutoff in cycles per input sample.
        fc_ = kCutoff * std::min(1.0f, out_rate / in_rate);
        half_width_ = std::min(
            kMaxHalfWidth, int(std::ceil(kZeroCrossings / (2.0f * fc_))));
        // The kernel's shape doesn't depend on the rates; only its height
        // (2 * fc) does.
        const std::array<double, kTableSize>& shape = KernelShape();
        for (int k = 0; k < kTableSize; ++k) {
          table_[k] = float(2.0 * fc_ * shape[k]);
        }
        index_scale_ = 2.0f * fc_ * float(kTablePointsPerCrossing);
        InitPolyphase();
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
    phase_ = 0;
    pending_ = false;
  }

  bool bypass() const { return bypass_; }
  bool polyphase() const { return polyphase_; }

  // Group delay, in output samples.
  float delay_in_output_samples() const {
    return bypass_ ? 0.0f : float(half_width_ / step_);
  }

  void Push(const float* frame) {
    if (bypass_) {
      std::copy(frame, frame + kFrameSize, pending_frame_.begin());
      pending_ = true;
      return;
    }
    std::copy(frame, frame + kFrameSize, ring_[in_count_ & kMask].begin());
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
    const int64_t first = t_integral_ - half_width_ + 1;
    const int taps = 2 * half_width_;
    bool nearest_is_next;
    if (polyphase_) {
      const float* h = &coefficients_[size_t(phase_) * size_t(taps)];
      for (int k = 0; k < taps; ++k) {
        const auto& x = ring_[(first + k) & kMask];
        for (int c = 0; c < num_channels; ++c) {
          acc[c] += h[k] * x[c];
        }
      }
      nearest_is_next = 2 * phase_ > phase_count_;
    } else {
      const float fractional = float(t_fractional_);
      for (int k = 0; k < taps; ++k) {
        const float h = Kernel(fractional, half_width_ - 1 - k);
        const auto& x = ring_[(first + k) & kMask];
        for (int c = 0; c < num_channels; ++c) {
          acc[c] += h * x[c];
        }
      }
      nearest_is_next = t_fractional_ > 0.5;
    }
    std::copy(acc.begin(), acc.end(), frame);
    const auto& nearest = ring_[(t_integral_ + (nearest_is_next ? 1 : 0)) & kMask];
    for (int c = num_channels; c < kFrameSize; ++c) {
      frame[c] = nearest[c];
    }

    if (polyphase_) {
      phase_ += phase_step_;
      t_integral_ += phase_ / phase_count_;
      phase_ %= phase_count_;
    } else {
      t_fractional_ += step_;
      const double whole = std::floor(t_fractional_);
      t_integral_ += int64_t(whole);
      t_fractional_ -= whole;
    }
    return true;
  }

 private:
  static constexpr int64_t kMask = kRingSize - 1;
  static_assert((kRingSize & (kRingSize - 1)) == 0, "ring must be 2^n");
  static_assert(kRingSize > 2 * kMaxHalfWidth + 2, "ring too small");

  // The kernel at fractional + offset input samples from the output
  // instant, linearly interpolated from the table.
  float Kernel(float fractional, int offset) const {
    const float d = std::fabs(fractional + float(offset));
    const float index = std::min(d * index_scale_, float(kTableSize - 2));
    const int index_integral = int(index);
    const float index_fractional = index - float(index_integral);
    return table_[index_integral] +
        (table_[index_integral + 1] - table_[index_integral]) *
        index_fractional;
  }

  // in_rate / out_rate = phase_step_ / phase_count_ in lowest terms; output
  // n sits n * phase_step_ / phase_count_ input samples in, so its position
  // between inputs is one of phase_count_ values. The kernels are read from
  // the same table, at the same points, as the per-tap path.
  void InitPolyphase() {
    if (in_rate_ != std::floor(in_rate_) || out_rate_ != std::floor(out_rate_) ||
        in_rate_ < 1.0f || out_rate_ < 1.0f) {
      return;
    }
    const int64_t in = int64_t(in_rate_);
    const int64_t out = int64_t(out_rate_);
    const int64_t g = std::gcd(in, out);
    const int64_t taps = 2 * half_width_;
    if ((out / g) * taps > kMaxPolyphaseCoefficients) {
      return;
    }
    phase_step_ = int(in / g);
    phase_count_ = int(out / g);
    coefficients_.resize(size_t(phase_count_ * taps));
    for (int p = 0; p < phase_count_; ++p) {
      const float fractional = float(double(p) / double(phase_count_));
      for (int k = 0; k < taps; ++k) {
        coefficients_[size_t(p * taps + k)] =
            Kernel(fractional, half_width_ - 1 - k);
      }
    }
    polyphase_ = true;
  }

  // The windowed sinc in units of zero crossings (z = 2 * fc * x), at unit
  // height, sampled kTablePointsPerCrossing times per crossing.
  static const std::array<double, kTableSize>& KernelShape() {
    static const std::array<double, kTableSize> shape = [] {
      std::array<double, kTableSize> t{};
      const double i0_beta = BesselI0(kKaiserBeta);
      for (int k = 0; k < kTableSize; ++k) {
        const double z = double(k) / kTablePointsPerCrossing;
        const double u = z / kZeroCrossings;
        if (u < 1.0) {
          const double sinc = z == 0.0
              ? 1.0
              : std::sin(kPi * z) / (kPi * z);
          t[k] = sinc * BesselI0(kKaiserBeta * std::sqrt(1.0 - u * u)) / i0_beta;
        }
      }
      return t;
    }();
    return shape;
  }

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
  std::array<std::array<float, kFrameSize>, kRingSize> ring_ = {};
  int64_t in_count_ = 0;
  int64_t t_integral_ = 0;
  double t_fractional_ = 0.0;  // per-tap path

  bool polyphase_ = false;
  std::vector<float> coefficients_;  // phase_count_ rows of 2 * half_width_ taps
  int phase_step_ = 1;
  int phase_count_ = 1;
  int phase_ = 0;  // current output's position, in 1 / phase_count_ samples

  std::array<float, kFrameSize> pending_frame_ = {};
  bool pending_ = false;
};

}  // namespace robotboy_beads
