#include "pitch_quantizer.h"

#include <cmath>
#include <cstring>

namespace particules_dsp {
namespace {

// log2(x) via natural log -- avoids std::log2 which may not be available
// on all embedded targets.
static inline double Log2(double x) {
  return std::log(x) * 1.4426950408889634;  // 1/ln(2)
}

}  // namespace

PitchQuantizer::PitchQuantizer() noexcept
    : log2_period_(1.0),
      inv_log2_period_(1.0),
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
  inv_log2_period_ = 1.0 / log2_period_;
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

  // Guard non-finite input (e.g. a transient NaN/Inf CV glitch surviving
  // from upstream): the old pow/log round trip happened to degrade NaN
  // comparisons to a harmless finite fallback, but static_cast<int> below
  // is UB on a non-finite double, so bail out explicitly instead. Return
  // 0.f (log2-domain unity, i.e. the root pitch) rather than echoing the
  // non-finite v_oct back out -- the quantizer must not launder a NaN/Inf
  // through untouched, since callers (e.g. GrainEngine) may otherwise
  // assume "ran through quantize()" implies "finite in, finite-or-same
  // out".
  if (!std::isfinite(pitch)) return 0.0f;

  // v_oct is already log2(freq), so the scale table (log2_ratios_,
  // log2_period_) can be scanned directly -- no pow/log round trip needed.
  // Reduce pitch into [0, log2_period_) in one step (replaces the old
  // iterative multiply/divide-by-period_ratio loop).
  double k = std::floor(pitch * inv_log2_period_);
  double log2_ratio = pitch - k * log2_period_;
  int octave_offset = static_cast<int>(k);

  // Linear scan: find the nearest degree in log2 space.
  // Degree 0 is implicit 1/1 (log2 = 0.0).
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

}  // namespace particules_dsp
