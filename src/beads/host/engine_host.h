// Runs a beads::Processor inside a Rack-style host, one host sample at a
// time.
//
// The Beads hardware runs its codec at a different sample rate for each
// quality setting (processor.h), and every constant in the DSP assumes that
// rate. So the processor runs at the hardware's own rate, behind a pair of
// resamplers (bypassed when the host already runs at that rate), in blocks
// of rate / 4000 frames, exactly as beads.cc drives it: auto gain on the
// way in, the Seed input delayed by 10 blocks, and 0.25 s of silence after
// every reconfiguration.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "beads/dsp/processor.h"
#include "stmlib/utils/gate_flags.h"

#include "host/gain_control.h"
#include "host/grain_capture.h"
#include "host/resampler.h"

namespace robotboy_beads {

// RobotBoy's Quality order (front-panel colors: white, cyan, amber,
// magenta), mapped onto Beads' media in MediaForQuality().
enum class Quality { kBright = 0, kCold, kSunny, kScorched };

beads::Processor::Media MediaForQuality(Quality quality);
float EngineRateForQuality(Quality quality);

// One host sample in: audio scaled so that +-5 V = 1.0, plus the gate
// levels of the Freeze and Seed inputs.
struct HostFrameIn {
  float l;
  float r;
  bool freeze;
  bool seed;
};

struct HostFrameOut {
  float l;
  float r;
  bool gate;  // Beads' grain/tap gate (what its gate-output mode sends)
};

// Everything beads.cc reads from the panel once per block.
struct BlockControls {
  beads::Parameters parameters;
  beads::ModulatedParameter density;
  beads::Processor::MixParameters mix;
  beads::SeedButtonState seed_button;
  bool seed_patched;     // false: the processor sees no Seed input at all
  uint8_t gain_setting;  // 0 = auto gain, 1..255 = Beads' manual gain byte
};

// The wrapper fills in the controls at the start of each engine block.
class ControlSource {
 public:
  virtual void ReadControls(BlockControls* controls) = 0;

 protected:
  ~ControlSource() = default;
};

class EngineHost {
 public:
  EngineHost();
  ~EngineHost();
  EngineHost(const EngineHost&) = delete;
  EngineHost& operator=(const EngineHost&) = delete;

  // Allocates the delay and reverb memory (\~900 KB). Returns false if that
  // fails; the host then outputs silence.
  bool Init(beads::Processor::ModeLock mode_lock, bool reverb_enabled,
            float host_rate);

  void SetHostRate(float host_rate);

  // Takes effect at the next block boundary. A quality change waits while
  // the buffer is frozen, so a frozen recording is never wiped.
  void SetQuality(Quality quality);

  // Jack state of the two audio inputs. Mono = not both patched; a change
  // reformats the buffer (as Beads does when a cable goes in or out).
  void SetInputs(bool left_patched, bool right_patched);

  // Synthetic tape hiss in Scorched cassette (on by default). On Beads the
  // hiss is the codec's own noise at 24 kHz, which the DSP code doesn't
  // contain, so RobotBoy adds it here, at the converter's place in the chain.
  void SetHissEnabled(bool enabled) { hiss_enabled_ = enabled; }

  void RequestClear() { clear_requested_ = true; }
  void RecalibrateGain() { recalibrate_requested_ = true; }

  // Lucarne's grain display: capture into feed after each block while
  // feed->attached is set. nullptr (the default) turns it off.
  void SetGrainFeed(GrainFeed* feed) { capture_.Attach(feed); }

  HostFrameOut Process(const HostFrameIn& in, ControlSource* controls);

  // Status, for menus and lights. Plain members: a read from the UI thread
  // is a benign race, at worst one block stale.
  bool ok() const { return processor_ != nullptr; }
  const beads::Processor& processor() const { return *processor_; }
  float gain_db() const;
  bool buffer_empty() const { return buffer_empty_; }
  // True during the 0.25 s of silence after a reformat, when the processor
  // isn't running.
  bool muted() const { return transition_blocks_ > 0; }
  int active_grains() const { return active_grains_; }
  Quality active_quality() const { return quality_; }
  float engine_rate() const { return engine_rate_; }
  // When host rate = engine rate (no resampling), an input sample comes
  // out this many host samples later.
  int bypass_latency() const { return int(block_size_) - 1; }

 private:
  enum {
    kMaxBlockSize = 12,
    kSeedDelayBlocks = 10,
    kSeedDelaySize = 256,
    kTransitionBlocks = 1000,  // 0.25 s at Beads' 4 kHz block rate
    kOutputFifoSize = 4096,
    // Delay memory cleared per muted block. The largest medium has about
    // 1.2 million units (8-bit mono and stereo lines), so the clearing
    // ends well inside the 1000 muted blocks.
    kClearUnitsPerBlock = 2048,
  };

  void Reconfigure(bool reset_gain, bool apply_quality);
  void InitGainControl(bool reset);
  void ResetStreams();
  void RunBlock(ControlSource* controls);
  void ClearMemoryStep(bool finish);
  void PushOutputFrame(float l, float r, float gate);

  static constexpr float kHissLevel = 0.00025f;
  // xorshift32, mapped to [-1, 1).
  float NextHiss() {
    hiss_state_ ^= hiss_state_ << 13;
    hiss_state_ ^= hiss_state_ >> 17;
    hiss_state_ ^= hiss_state_ << 5;
    return float(int32_t(hiss_state_)) * (1.0f / 2147483648.0f);
  }

  std::unique_ptr<beads::Processor> processor_;
  std::unique_ptr<uint8_t[]> delay_memory_;
  std::unique_ptr<uint16_t[]> reverb_memory_;

  GainControl gain_control_;
  Resampler<2> input_resampler_;
  Resampler<2, 1> output_resampler_;  // L, R filtered; the gate held

  float host_rate_ = 48000.0f;
  float engine_rate_ = 48000.0f;
  size_t block_size_ = 12;

  Quality quality_ = Quality::kBright;
  Quality requested_quality_ = Quality::kBright;
  bool left_patched_ = false;
  bool right_patched_ = false;
  bool mono_ = true;
  bool frozen_ = false;  // any frame of the last block was frozen

  // Engine-rate block being assembled.
  std::array<beads::FloatFrame, kMaxBlockSize> block_in_;
  std::array<bool, kMaxBlockSize> block_freeze_;
  std::array<bool, kMaxBlockSize> block_seed_;
  size_t block_fill_ = 0;
  bool freeze_accumulator_ = false;
  bool seed_accumulator_ = false;

  // Seed delay line (beads.cc delays the Seed events by 10 blocks).
  std::array<bool, kSeedDelaySize> seed_delay_;
  size_t seed_delay_write_ = 0;
  stmlib::GateFlags previous_seed_ = stmlib::GATE_FLAG_LOW;

  int transition_blocks_ = 0;
  size_t clear_position_ = 0;  // delay memory cleared so far (units)
  bool hiss_enabled_ = true;
  uint32_t hiss_state_ = 0x2545f491u;  // separate from Beads' own RNG
  bool clear_requested_ = false;
  bool recalibrate_requested_ = false;
  bool buffer_empty_ = true;
  int active_grains_ = 0;
  GrainCapture capture_;

  // Host-rate output FIFO (L, R, gate).
  std::array<std::array<float, 3>, kOutputFifoSize> output_fifo_;
  size_t fifo_read_ = 0;
  size_t fifo_write_ = 0;
  bool primed_ = false;
  int prime_threshold_ = 12;
};

}  // namespace robotboy_beads
