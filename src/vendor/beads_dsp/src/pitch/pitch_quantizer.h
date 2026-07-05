#ifndef BEADS_PITCH_QUANTIZER_H_
#define BEADS_PITCH_QUANTIZER_H_

#include <cstdint>

namespace beads {

// Platform-agnostic pitch quantizer that snaps V/oct values to the nearest
// degree of a loaded scale.  Works with pre-computed double ratios so the
// DSP library has no dependency on the NT API's _NT_sclNote type.
class PitchQuantizer {
 public:
  PitchQuantizer() noexcept;

  // Load a scale from an array of ratios (1/1-relative, ascending).
  // The last ratio is the period (e.g. 2.0 for an octave-repeating scale).
  void loadRatios(const double* ratios, uint32_t num_notes) noexcept;

  // Remove the loaded scale.  quantize() becomes a pass-through.
  void clear() noexcept;

  // True when a scale has been loaded.
  bool isLoaded() const noexcept { return num_notes_ > 0; }

  // Set the root as a MIDI note number (0-127, default 60 = C4).
  // Internally stored as a V/oct offset from middle C.
  void set_root(int midi_note) noexcept;

  // Snap a V/oct value to the nearest scale degree.
  // If no scale is loaded, returns the input unchanged.
  float quantize(float v_oct) const noexcept;

 private:
  double log2_ratios_[128];   // log2 of each degree ratio (degree 0 implicit = 0.0)
  double log2_period_;        // log2 of the period ratio (last entry)
  uint32_t num_notes_;        // number of scale degrees (including period)
  double root_v_oct_;         // root offset in V/oct from C4
};

}  // namespace beads

#endif  // BEADS_PITCH_QUANTIZER_H_
