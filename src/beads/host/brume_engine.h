// Brume's reverb engine, one host sample at a time. Rack-free.
//
// The input is split into dry and send at the host rate (wow and flutter on
// one or the other). The send goes through a resampler into the tank rate
// for the current Quality, where it is processed in blocks of rate / 4000
// frames: FILTER, SMEAR, the shimmer return, a DC blocker, Beads' pre-reverb
// limiter, the active tank (Beads or Clouds, fully wet), the Mode's makeup
// gain, Beads' output limiter and the tank's output gain. The active tank's
// output is pitch-shifted, scaled by SHIMMER and returned to the input of the
// next block. The wet comes back through a second resampler and an output
// FIFO primed as in EngineHost, then WIDTH and DRY/WET mix it with the dry,
// undelayed input.
//
// Mode switches spill over: the old tank gets silence and its output gain
// falls to 0 over 2 s, its output added to the wet; at 0 it is cleared and
// stops. The active tank's gain rises to 1 over 20 ms, so switching back to
// a tank that is still ringing picks it up where it is. Each tank has its own
// output limiter, which goes with it into the spillover.
//
// A Clear or Quality change fades the wet out over 10 ms at the tank rate,
// then clears every engine buffer, both tanks included; a Quality change
// waits one more block for the output FIFO to drain, then also reconfigures
// the streams and fades back in over 10 ms.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

// The vendored FxEngines' STATIC_ASSERTs expand to unused local typedefs.
// (Clang honors the GCC pragma and spelling.)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#include "beads/dsp/frame.h"
#include "beads/dsp/fx/reverb.h"
#include "beads/dsp/limiter.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/diffuser.h"
#include "clouds/dsp/fx/pitch_shifter.h"
#include "clouds/dsp/fx/reverb.h"
#include "stmlib/dsp/filter.h"
#pragma GCC diagnostic pop

#include "host/brume_controls.h"
#include "host/resampler.h"
#include "host/wow_flutter.h"

namespace robotboy_beads {

// Wet makeup gain per Mode, after the tank and before its output limiter, so
// that both Modes give the same wet RMS for pink noise at the defaults. The
// Clouds value is the Beads/Clouds wet RMS ratio from the "calibrate the
// Clouds makeup" test (test_brume_modes.cpp), 30 s of pink noise, Bright,
// rounded to 3 significant figures.
constexpr float kBeadsMakeup = 1.0f;
constexpr float kCloudsMakeup = 0.992f;  // measured 0.99179

// Knob values 0..1 after modulation, except interval (semitones).
struct BrumeControls {
  float send, filter, smear, wow, shimmer, interval_semitones,
        decay, damp, diffuse, speed, depth, width, dry_wet;
};

// The spec's defaults.
BrumeControls DefaultBrumeControls();

struct BrumeFrame { float l, r; };

class BrumeEngine {
 public:
  BrumeEngine();
  ~BrumeEngine();
  BrumeEngine(const BrumeEngine&) = delete;
  BrumeEngine& operator=(const BrumeEngine&) = delete;

  // Allocates the memory of both tanks, the diffuser and the pitch shifter.
  // Returns false if that fails; Process then outputs silence.
  bool Init(float host_rate);

  void SetHostRate(float host_rate);

  // Applied at the next block. The old tank spills over; the new one
  // starts empty unless it is still spilling over.
  void SetMode(BrumeMode mode) { requested_mode_ = mode; }
  // Fade out, clear, reconfigure, fade in.
  void SetQuality(BrumeQuality quality) { requested_quality_ = quality; }
  void SetWowScope(WowScope scope) { wow_scope_ = scope; }

  // Call every host sample. Non-finite values keep the previous value.
  void SetControls(const BrumeControls& controls);

  void RequestClear() { clear_requested_ = true; }

  // +-1 in; out clamped to +-2.
  BrumeFrame Process(float l, float r);

  bool ok() const { return beads_memory_ != nullptr; }
  BrumeMode active_mode() const { return mode_; }
  BrumeQuality active_quality() const { return quality_; }
  float tank_rate() const { return tank_rate_; }
  // Times the output FIFO ran dry after priming and had to re-prime.
  int underflows() const { return underflows_; }
  // A tank's output gain: 1 when active, falling while it spills over, 0
  // when stopped.
  float tank_gain(BrumeMode mode) const { return tank_gain_[int(mode)]; }

 private:
  enum {
    kMaxBlockSize = 12,
    kOutputFifoSize = 4096,
  };

  enum class Fade { kRunning, kClearOut, kQualityOut, kQualityDrain, kIn };

  void Reconfigure();
  void ResetStreams();
  void ClearEngine();
  void ClearTank(BrumeMode mode);
  void RunBlock();
  void ApplyTankControls(BrumeMode mode, const BrumeControls& c);
  void ProcessTank(BrumeMode mode, beads::FloatFrame* frames, size_t size);
  void PushOutputFrame(float l, float r);

  std::unique_ptr<uint16_t[]> beads_memory_;
  std::unique_ptr<uint16_t[]> clouds_memory_;
  std::unique_ptr<float[]> diffuser_memory_;
  std::unique_ptr<uint16_t[]> pitch_shifter_memory_;

  // Both tanks always exist; the active one runs, and the other while it
  // spills over. Indexed by BrumeMode.
  beads::Reverb beads_tank_;
  clouds::Reverb clouds_tank_;
  float applied_speed_[2];  // last set_lfo_rate multiplier per tank; < 0: stale
  beads::Limiter post_limiter_[2];
  float tank_gain_[2] = {1.0f, 0.0f};
  bool tank_running_[2] = {true, false};
  float gain_rise_ = 1.0f / 960.0f;    // per tank-rate sample, 20 ms
  float gain_fall_ = 1.0f / 96000.0f;  // 2 s

  clouds::Diffuser diffuser_;
  stmlib::Svf lp_[2];
  stmlib::Svf hp_[2];
  stmlib::DCBlocker dc_blocker_[2];
  beads::Limiter pre_limiter_;

  // Shimmer: the previous block's tank output, pitch-shifted and scaled.
  clouds::PitchShifter pitch_shifter_;
  std::array<beads::FloatFrame, kMaxBlockSize> shimmer_return_;
  bool shimmer_live_ = false;  // shimmer_return_ holds a nonzero gain's output

  WowFlutter wow_;
  Resampler<2> input_resampler_;
  Resampler<2> output_resampler_;

  float host_rate_ = 48000.0f;
  float tank_rate_ = 48000.0f;
  size_t block_size_ = 12;

  BrumeMode mode_ = BrumeMode::kBeads;
  BrumeMode requested_mode_ = BrumeMode::kBeads;
  BrumeQuality quality_ = BrumeQuality::kBright;
  BrumeQuality requested_quality_ = BrumeQuality::kBright;
  WowScope wow_scope_ = WowScope::kInput;
  bool clear_requested_ = false;

  // Latest controls; the engine latches them once per block.
  BrumeControls controls_;
  bool controls_seen_ = false;

  // Host-rate one-pole smoothing (about 2 ms).
  float smoothing_ = 0.01f;
  float send_ = 0.5f;
  float wow_amount_ = 0.0f;  // after WowAmount, 0..3
  float width_ = 0.5f;
  float dry_wet_ = 0.5f;

  // Wet fade at the tank rate, for Clear and Quality changes.
  Fade fade_state_ = Fade::kRunning;
  float fade_ = 1.0f;
  float fade_step_ = 1.0f / 480.0f;

  // Tank-rate block being assembled.
  std::array<beads::FloatFrame, kMaxBlockSize> block_;
  // The spilling-over tank's output for the current block.
  std::array<beads::FloatFrame, kMaxBlockSize> spill_;
  size_t block_fill_ = 0;

  // Host-rate wet FIFO.
  std::array<std::array<float, 2>, kOutputFifoSize> output_fifo_;
  size_t fifo_read_ = 0;
  size_t fifo_write_ = 0;
  bool primed_ = false;
  int prime_threshold_ = 12;
  int underflows_ = 0;
};

}  // namespace robotboy_beads
