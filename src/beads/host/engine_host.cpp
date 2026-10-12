#include "host/engine_host.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace robotboy_beads {

namespace {

// Beads' delay memory: three 256 KB banks (beads.cc), shared by all four
// quality settings, plus the 32768-word reverb buffer.
constexpr size_t kBankSize = 262144;
constexpr size_t kNumBanks = 3;
constexpr size_t kReverbSize = 32768;

inline float Finite(float x) { return std::isfinite(x) ? x : 0.0f; }

}  // namespace

beads::Processor::Media MediaForQuality(Quality quality) {
  switch (quality) {
    case Quality::kCold:
      return beads::Processor::MEDIA_VINTAGE_DIGITAL;
    case Quality::kSunny:
      return beads::Processor::MEDIA_CLEAN_TAPE;
    case Quality::kScorched:
      return beads::Processor::MEDIA_DIRTY_TAPE;
    case Quality::kBright:
    default:
      return beads::Processor::MEDIA_DIGITAL;
  }
}

// processor.h, sample_rate() with ENABLE_HARDWARE_24K (the shipped build).
float EngineRateForQuality(Quality quality) {
  switch (quality) {
    case Quality::kCold:
      return 32000.0f;
    case Quality::kScorched:
      return 24000.0f;
    case Quality::kSunny:    // codec at 48 kHz, decimated x2 inside
    case Quality::kBright:
    default:
      return 48000.0f;
  }
}

EngineHost::EngineHost() {
  block_in_.fill(beads::FloatFrame{0.0f, 0.0f});
  block_freeze_.fill(false);
  block_seed_.fill(false);
  seed_delay_.fill(false);
  for (auto& f : output_fifo_) f.fill(0.0f);
}

EngineHost::~EngineHost() = default;

bool EngineHost::Init(beads::Processor::ModeLock mode_lock,
                      bool reverb_enabled, float host_rate) {
  processor_.reset(new (std::nothrow) beads::Processor);
  delay_memory_.reset(new (std::nothrow) uint8_t[kBankSize * kNumBanks]);
  reverb_memory_.reset(new (std::nothrow) uint16_t[kReverbSize]);
  if (!processor_ || !delay_memory_ || !reverb_memory_) {
    processor_.reset();
    delay_memory_.reset();
    reverb_memory_.reset();
    return false;
  }
  std::fill(&reverb_memory_[0], &reverb_memory_[kReverbSize], 0);

  uint8_t* banks[kNumBanks];
  for (size_t i = 0; i < kNumBanks; ++i) {
    banks[i] = &delay_memory_[i * kBankSize];
  }
  processor_->Init(banks, reverb_memory_.get());
  processor_->set_mode_lock(mode_lock);
  processor_->set_reverb_enabled(reverb_enabled);

  host_rate_ = host_rate;
  configured_ = false;
  Reconfigure(true, true);
  return true;
}

void EngineHost::SetHostRate(float host_rate) {
  if (host_rate == host_rate_) {
    return;
  }
  host_rate_ = host_rate;
  if (processor_) {
    ResetStreams();
  }
}

void EngineHost::SetQuality(Quality quality) {
  requested_quality_ = quality;
}

void EngineHost::SetInputs(bool left_patched, bool right_patched) {
  left_patched_ = left_patched;
  right_patched_ = right_patched;
}

float EngineHost::gain_db() const {
  return 20.0f * std::log10(std::max(gain_control_.gain(), 1e-6f));
}

void EngineHost::Reconfigure(bool reset_gain, bool apply_quality) {
  if (apply_quality) {
    quality_ = requested_quality_;
  }
  mono_ = !(left_patched_ && right_patched_);
  const beads::Processor::Media media = MediaForQuality(quality_);
  processor_->Configure(media, mono_);
  engine_rate_ = processor_->sample_rate();
  block_size_ = size_t(engine_rate_ / 4000.0f);

  // beads.cc ConfigureAndStartCodec().
  gain_control_.Init(
      engine_rate_,
      media == beads::Processor::MEDIA_DIRTY_TAPE
          ? GainControl::SOFT_CLIPPING_MODE_ON
          : (media == beads::Processor::MEDIA_CLEAN_TAPE
                ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                : GainControl::SOFT_CLIPPING_MODE_OFF),
      reset_gain);
  transition_blocks_ = kTransitionBlocks;
  buffer_empty_ = true;
  configured_ = true;
  ResetStreams();
}

void EngineHost::ResetStreams() {
  input_resampler_.Init(host_rate_, engine_rate_);
  output_resampler_.Init(engine_rate_, host_rate_);
  block_fill_ = 0;
  seed_delay_.fill(false);
  seed_delay_write_ = 0;
  previous_seed_ = stmlib::GATE_FLAG_LOW;
  fifo_read_ = fifo_write_ = 0;
  primed_ = false;
  // Enough to ride out the gap between two engine blocks' worth of output.
  prime_threshold_ = int(std::ceil(
      float(block_size_) * host_rate_ / engine_rate_));
  if (!output_resampler_.bypass() || !input_resampler_.bypass()) {
    prime_threshold_ += 2;
  }
}

void EngineHost::PushOutputFrame(float l, float r, float gate) {
  const float frame[3] = {l, r, gate};
  output_resampler_.Push(frame);
  float out[3];
  while (output_resampler_.Pop(out)) {
    const size_t next = (fifo_write_ + 1) % kOutputFifoSize;
    if (next == fifo_read_) {
      return;  // can't happen with a sane prime threshold; drop rather than wrap
    }
    std::copy(out, out + 3, output_fifo_[fifo_write_].begin());
    fifo_write_ = next;
  }
}

void EngineHost::RunBlock(ControlSource* controls) {
  const size_t size = block_size_;

  bool frozen = false;
  for (size_t i = 0; i < size; ++i) {
    frozen = frozen || block_freeze_[i];
  }
  frozen_ = frozen;

  // Configuration changes happen between blocks, as beads.cc restarts its
  // codec between blocks. A quality change waits for Freeze to release.
  const bool mono = !(left_patched_ && right_patched_);
  const bool mono_changed = mono != mono_;
  const bool quality_changed = requested_quality_ != quality_;
  if (mono_changed || (quality_changed && !frozen)) {
    // A cable change while frozen reformats (as on Beads), but keeps the
    // current quality until Freeze releases.
    Reconfigure(mono_changed, !frozen);
    return;  // streams were reset; this block's input is discarded
  }

  // Controls are read every block, as beads.cc reads its pots even while
  // the codec restarts; a source with one-shot controls should hold them
  // while muted() (the processor doesn't run then).
  BlockControls c{};
  controls->ReadControls(&c);

  if (clear_requested_) {
    clear_requested_ = false;
    processor_->ClearBuffer();
    buffer_empty_ = true;
  }
  if (recalibrate_requested_) {
    recalibrate_requested_ = false;
    gain_control_.Init(
        engine_rate_,
        MediaForQuality(quality_) == beads::Processor::MEDIA_DIRTY_TAPE
            ? GainControl::SOFT_CLIPPING_MODE_ON
            : (MediaForQuality(quality_) == beads::Processor::MEDIA_CLEAN_TAPE
                  ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                  : GainControl::SOFT_CLIPPING_MODE_OFF),
        true);
  }

  // Seed: delayed by 10 blocks so that a CV sent with the trigger has
  // settled (beads.cc), then turned into gate flags.
  stmlib::GateFlags seed[kMaxBlockSize];
  const stmlib::GateFlags* seed_input = nullptr;
  if (c.seed_patched) {
    const size_t delay = size * kSeedDelayBlocks;
    for (size_t i = 0; i < size; ++i) {
      seed_delay_[seed_delay_write_] = block_seed_[i];
      const bool delayed = seed_delay_[
          (seed_delay_write_ + kSeedDelaySize - delay) % kSeedDelaySize];
      seed_delay_write_ = (seed_delay_write_ + 1) % kSeedDelaySize;
      previous_seed_ = seed[i] = stmlib::ExtractGateFlags(
          previous_seed_, delayed);
    }
    seed_input = seed;
  } else {
    previous_seed_ = stmlib::GATE_FLAG_LOW;
  }

  beads::FloatFrame gained[kMaxBlockSize];
  beads::FloatFrame out[kMaxBlockSize];
  gain_control_.Process(
      transition_blocks_ == 0,
      c.gain_setting,
      !left_patched_,
      !right_patched_,
      block_in_.data(),
      gained,
      size);

  if (transition_blocks_) {
    --transition_blocks_;
    for (size_t i = 0; i < size; ++i) {
      PushOutputFrame(0.0f, 0.0f, 0.0f);
    }
    return;
  }

  c.mix.output_gate = false;  // the host builds the gate output itself
  processor_->Process(
      c.seed_button,
      seed_input,
      block_freeze_.data(),
      c.density,
      c.parameters,
      c.mix,
      gained,
      out,
      size);
  active_grains_ = processor_->num_active_grains();

  if (!frozen) {
    for (size_t i = 0; i < size && buffer_empty_; ++i) {
      if (gained[i].l != 0.0f || gained[i].r != 0.0f) {
        buffer_empty_ = false;
      }
    }
  }

  // beads.cc clips to the codec's full scale before conversion; the
  // limiters inside the processor are peak followers and let fast
  // transients through.
  for (size_t i = 0; i < size; ++i) {
    PushOutputFrame(
        std::min(std::max(Finite(out[i].l), -1.0f), 0.999999f),
        std::min(std::max(Finite(out[i].r), -1.0f), 0.999999f),
        processor_->gate_out(i) ? 1.0f : 0.0f);
  }
}

HostFrameOut EngineHost::Process(const HostFrameIn& in,
                                 ControlSource* controls) {
  HostFrameOut result = {0.0f, 0.0f, false};
  if (!processor_) {
    return result;
  }

  // Gates: each engine frame sees every host sample since the previous
  // one, so a one-sample trigger survives downsampling, and starts from the
  // current level, so a held gate stays high while upsampling.
  freeze_accumulator_ = freeze_accumulator_ || in.freeze;
  seed_accumulator_ = seed_accumulator_ || in.seed;

  const float frame[2] = {Finite(in.l), Finite(in.r)};
  input_resampler_.Push(frame);
  float engine_frame[2];
  while (input_resampler_.Pop(engine_frame)) {
    block_in_[block_fill_].l = engine_frame[0];
    block_in_[block_fill_].r = engine_frame[1];
    block_freeze_[block_fill_] = freeze_accumulator_;
    block_seed_[block_fill_] = seed_accumulator_;
    freeze_accumulator_ = in.freeze;
    seed_accumulator_ = in.seed;
    if (++block_fill_ == block_size_) {
      block_fill_ = 0;
      RunBlock(controls);
    }
  }

  const size_t available =
      (fifo_write_ + kOutputFifoSize - fifo_read_) % kOutputFifoSize;
  if (!primed_ && available >= size_t(prime_threshold_)) {
    primed_ = true;
  }
  if (primed_) {
    if (available == 0) {
      primed_ = false;  // underflow: re-prime rather than stutter
    } else {
      const auto& f = output_fifo_[fifo_read_];
      fifo_read_ = (fifo_read_ + 1) % kOutputFifoSize;
      // Clip again at the host rate: resampling clipped material can
      // overshoot full scale by a few percent between samples.
      result.l = std::min(std::max(f[0], -1.0f), 1.0f);
      result.r = std::min(std::max(f[1], -1.0f), 1.0f);
      result.gate = f[2] > 0.5f;
    }
  }
  return result;
}

}  // namespace robotboy_beads
