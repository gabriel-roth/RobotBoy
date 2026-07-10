#include "pitch_quantizer.h"

#include <cmath>
#include <cstring>

namespace beads {
namespace {

// log2(x) via natural log -- avoids std::log2 which may not be available
// on all embedded targets.
static inline double Log2(double x) {
  return std::log(x) * 1.4426950408889634;  // 1/ln(2)
}

}  // namespace

PitchQuantizer::PitchQuantizer() noexcept
    : log2_period_(1.0),
      num_notes_(0),
      root_v_oct_(0.0) {
  std::memset(log2_ratios_, 0, sizeof(log2_ratios_));
}

void PitchQuantizer::loadRatios(const double* ratios, uint32_t num_notes) noexcept {
  if (!ratios || num_notes == 0) {
    clear();
    return;
  }
  if (num_notes > 128) num_notes = 128;

  num_notes_ = num_notes;
  for (uint32_t i = 0; i < num_notes; ++i) {
    // Guard against zero/negative ratios from malformed SCL files
    double r = ratios[i];
    if (r < 1e-9) r = 1e-9;
    log2_ratios_[i] = Log2(r);
  }
  log2_period_ = log2_ratios_[num_notes - 1];
  if (log2_period_ <= 0.0) log2_period_ = 1.0;  // safety
}

void PitchQuantizer::clear() noexcept {
  num_notes_ = 0;
}

void PitchQuantizer::set_root(int midi_note) noexcept {
  if (midi_note < 0) midi_note = 0;
  if (midi_note > 127) midi_note = 127;
  root_v_oct_ = (midi_note - 60) / 12.0;
}

float PitchQuantizer::quantize(float v_oct) const noexcept {
  if (num_notes_ == 0) return v_oct;

  // Remove root offset so we quantize relative to the scale root
  double pitch = static_cast<double>(v_oct) - root_v_oct_;

  // Convert V/oct to ratio
  double ratio = std::pow(2.0, pitch);

  // Normalize ratio into [1.0, period_ratio), tracking octave offset.
  // Iteration bound prevents runaway loops with degenerate scales
  // (e.g. period_ratio very close to 1.0).  128 iterations covers
  // +-128 octaves which is far beyond any musically useful range.
  const double period_ratio = std::pow(2.0, log2_period_);
  int octave_offset = 0;
  static constexpr int kMaxIter = 128;

  if (ratio < 1.0) {
    for (int iter = 0; iter < kMaxIter && ratio < 1.0; ++iter) {
      ratio *= period_ratio;
      octave_offset--;
    }
  } else if (ratio >= period_ratio) {
    for (int iter = 0; iter < kMaxIter && ratio >= period_ratio; ++iter) {
      ratio /= period_ratio;
      octave_offset++;
    }
  }

  // Linear scan: find the nearest degree in log2 space.
  // Degree 0 is implicit 1/1 (log2 = 0.0).
  double log2_ratio = Log2(ratio);
  int best_degree = 0;
  double best_dist = std::fabs(log2_ratio);  // distance to degree 0

  for (uint32_t d = 0; d < num_notes_; ++d) {
    double dist = std::fabs(log2_ratio - log2_ratios_[d]);
    if (dist < best_dist) {
      best_dist = dist;
      best_degree = static_cast<int>(d) + 1;  // +1 because degree 0 is 1/1
    }
  }

  // Check period boundary (degree 0 of next octave)
  double dist_to_period = std::fabs(log2_ratio - log2_period_);
  if (dist_to_period < best_dist) {
    best_degree = 0;
    octave_offset++;
  }

  // Reconstruct quantized V/oct
  double quantized;
  if (best_degree == 0) {
    quantized = static_cast<double>(octave_offset) * log2_period_;
  } else {
    quantized = log2_ratios_[best_degree - 1] +
                static_cast<double>(octave_offset) * log2_period_;
  }

  // Add root offset back
  return static_cast<float>(quantized + root_v_oct_);
}

}  // namespace beads
