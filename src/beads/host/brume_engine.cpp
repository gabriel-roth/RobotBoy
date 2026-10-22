#include "host/brume_engine.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace robotboy_beads {

namespace {

constexpr size_t kBeadsReverbSize = 32768;
constexpr size_t kCloudsReverbSize = 16384;
constexpr size_t kDiffuserSize = 2048;
constexpr size_t kPitchShifterSize = 4096;

constexpr float kSmoothingSeconds = 0.002f;
constexpr float kFadeSeconds = 0.010f;
constexpr float kDcBlockerHertz = 10.0f;
// The send is held to +-8 (+-40 V) and made finite before the engine sees
// it; RunBlock's finite check catches anything that still diverges.
constexpr float kMaxSend = 8.0f;
constexpr float kMaxOutput = 2.0f;
constexpr float kCloudsInputGain = 0.2f;

inline float Finite(float x) { return std::isfinite(x) ? x : 0.0f; }

inline float Clamp(float x, float limit) {
  return std::min(std::max(x, -limit), limit);
}

// A non-finite value keeps the previous one.
inline float Sanitize(float value, float previous, float lo, float hi) {
  if (!std::isfinite(value)) return previous;
  return std::min(std::max(value, lo), hi);
}

}  // namespace

BrumeControls DefaultBrumeControls() {
  BrumeControls c;
  c.send = 0.5f;
  c.filter = 0.5f;
  c.smear = 0.0f;
  c.wow = 0.0f;
  c.shimmer = 0.0f;
  c.interval_semitones = 12.0f;
  c.decay = 0.5f;
  c.damp = 4.0f / 7.0f;
  c.diffuse = 0.65f;
  c.speed = 0.5f;
  c.depth = 1.0f;
  c.width = 0.5f;
  c.dry_wet = 0.5f;
  return c;
}

BrumeEngine::BrumeEngine() {
  controls_ = DefaultBrumeControls();
  applied_speed_[0] = applied_speed_[1] = -1.0f;
  block_.fill(beads::FloatFrame{0.0f, 0.0f});
  shimmer_return_.fill(beads::FloatFrame{0.0f, 0.0f});
  for (auto& f : output_fifo_) f.fill(0.0f);
}

BrumeEngine::~BrumeEngine() = default;

bool BrumeEngine::Init(float host_rate) {
  beads_memory_.reset(new (std::nothrow) uint16_t[kBeadsReverbSize]);
  clouds_memory_.reset(new (std::nothrow) uint16_t[kCloudsReverbSize]);
  diffuser_memory_.reset(new (std::nothrow) float[kDiffuserSize]);
  pitch_shifter_memory_.reset(new (std::nothrow) uint16_t[kPitchShifterSize]);
  if (!beads_memory_ || !clouds_memory_ || !diffuser_memory_ ||
      !pitch_shifter_memory_) {
    beads_memory_.reset();
    clouds_memory_.reset();
    diffuser_memory_.reset();
    pitch_shifter_memory_.reset();
    return false;
  }

  host_rate_ = host_rate;
  quality_ = requested_quality_;
  mode_ = requested_mode_;
  tank_rate_ = BrumeTankRate(quality_);

  beads_tank_.Init(beads_memory_.get(), tank_rate_);
  clouds_tank_.Init(clouds_memory_.get());
  diffuser_.Init(diffuser_memory_.get());
  diffuser_.set_amount(0.0f);
  // Init already sets the full 2047-sample window, which set_size(1.0)
  // glides toward.
  pitch_shifter_.Init(pitch_shifter_memory_.get());
  pitch_shifter_.set_size(1.0f);
  pitch_shifter_.set_ratio(1.0f);
  for (int i = 0; i < 2; ++i) {
    lp_[i].Init();
    hp_[i].Init();
  }

  smoothing_ = 1.0f - std::exp(-1.0f / (kSmoothingSeconds * host_rate_));
  wow_.Init(host_rate_);
  Reconfigure();
  fade_state_ = Fade::kRunning;
  fade_ = 1.0f;
  return true;
}

void BrumeEngine::SetHostRate(float host_rate) {
  if (host_rate == host_rate_) {
    return;
  }
  host_rate_ = host_rate;
  smoothing_ = 1.0f - std::exp(-1.0f / (kSmoothingSeconds * host_rate_));
  if (ok()) {
    wow_.Init(host_rate_);
    ResetStreams();
  }
}

void BrumeEngine::SetControls(const BrumeControls& in) {
  BrumeControls& c = controls_;
  c.send = Sanitize(in.send, c.send, 0.0f, 1.0f);
  c.filter = Sanitize(in.filter, c.filter, 0.0f, 1.0f);
  c.smear = Sanitize(in.smear, c.smear, 0.0f, 1.0f);
  c.wow = Sanitize(in.wow, c.wow, 0.0f, 1.0f);
  c.shimmer = Sanitize(in.shimmer, c.shimmer, 0.0f, 1.0f);
  c.interval_semitones = Sanitize(
      in.interval_semitones, c.interval_semitones, -48.0f, 48.0f);
  c.decay = Sanitize(in.decay, c.decay, 0.0f, 1.0f);
  c.damp = Sanitize(in.damp, c.damp, 0.0f, 1.0f);
  c.diffuse = Sanitize(in.diffuse, c.diffuse, 0.0f, 1.0f);
  c.speed = Sanitize(in.speed, c.speed, 0.0f, 1.0f);
  c.depth = Sanitize(in.depth, c.depth, 0.0f, 1.0f);
  c.width = Sanitize(in.width, c.width, 0.0f, 1.0f);
  c.dry_wet = Sanitize(in.dry_wet, c.dry_wet, 0.0f, 1.0f);
  if (!controls_seen_) {
    // The smoothers start where the panel is, rather than gliding there.
    controls_seen_ = true;
    send_ = c.send;
    wow_amount_ = c.wow;
    width_ = c.width;
    dry_wet_ = c.dry_wet;
  }
}

// Quality (tank rate and block size) takes effect, and everything restarts
// empty.
void BrumeEngine::Reconfigure() {
  quality_ = requested_quality_;
  tank_rate_ = BrumeTankRate(quality_);
  block_size_ = size_t(tank_rate_ / 4000.0f);
  fade_step_ = 1.0f / (kFadeSeconds * tank_rate_);
  ClearEngine();
  ResetStreams();
}

void BrumeEngine::ResetStreams() {
  input_resampler_.Init(host_rate_, tank_rate_);
  output_resampler_.Init(tank_rate_, host_rate_);
  block_fill_ = 0;
  fifo_read_ = fifo_write_ = 0;
  primed_ = false;
  // Enough to ride out the gap between two engine blocks' worth of output.
  prime_threshold_ = int(std::ceil(
      float(block_size_) * host_rate_ / tank_rate_));
  if (!output_resampler_.bypass() || !input_resampler_.bypass()) {
    prime_threshold_ += 2;
  }
}

void BrumeEngine::ClearTank(BrumeMode mode) {
  if (mode == BrumeMode::kBeads) {
    beads_tank_.Clear();
    // Reset zeroes the loop filters, which Clear leaves alone, and restarts
    // the LFOs at multiplier 1.
    beads_tank_.Reset(tank_rate_);
  } else {
    clouds_tank_.Clear();
  }
  applied_speed_[int(mode)] = -1.0f;
}

void BrumeEngine::ClearEngine() {
  ClearTank(BrumeMode::kBeads);
  ClearTank(BrumeMode::kClouds);
  diffuser_.Clear();
  pitch_shifter_.Clear();
  shimmer_return_.fill(beads::FloatFrame{0.0f, 0.0f});
  shimmer_live_ = false;
  const float pole = 1.0f - 2.0f * 3.14159265f * kDcBlockerHertz / tank_rate_;
  for (int i = 0; i < 2; ++i) {
    lp_[i].Reset();
    hp_[i].Reset();
    dc_blocker_[i].Init(pole);
  }
  // As Beads' pre_reverb_limiter_ and output_limiter_ (processor.cc).
  pre_limiter_.Init(1.0f / (tank_rate_ * 4.0f), 0.9f);
  post_limiter_.Init(1.0f / (tank_rate_ * 6.0f), 0.9f);
}

void BrumeEngine::ApplyTankControls(const BrumeControls& c) {
  const float time = DecayTime(c.decay, mode_);
  const float lp = DampLp(c.damp);
  const float diffusion = DiffuseCoefficient(c.diffuse);
  const float speed = SpeedMultiplier(c.speed);
  float& applied_speed = applied_speed_[int(mode_)];
  if (mode_ == BrumeMode::kBeads) {
    beads_tank_.set_mix(1.0f);
    beads_tank_.set_time(time);
    beads_tank_.set_lp(lp);
    beads_tank_.set_diffusion(diffusion);
    beads_tank_.set_mod_depth(c.depth);
    if (speed != applied_speed) {
      beads_tank_.set_lfo_rate(speed, tank_rate_);
      applied_speed = speed;
    }
  } else {
    clouds_tank_.set_amount(1.0f);
    clouds_tank_.set_input_gain(kCloudsInputGain);
    clouds_tank_.set_time(time);
    clouds_tank_.set_lp(lp);
    clouds_tank_.set_diffusion(diffusion);
    clouds_tank_.set_mod_depth(c.depth);
    if (speed != applied_speed) {
      clouds_tank_.set_lfo_rate(speed, tank_rate_);
      applied_speed = speed;
    }
  }
}

void BrumeEngine::PushOutputFrame(float l, float r) {
  const float frame[2] = {l, r};
  output_resampler_.Push(frame);
  float out[2];
  while (output_resampler_.Pop(out)) {
    const size_t next = (fifo_write_ + 1) % kOutputFifoSize;
    if (next == fifo_read_) {
      return;  // can't happen with a sane prime threshold; drop rather than wrap
    }
    output_fifo_[fifo_write_][0] = out[0];
    output_fifo_[fifo_write_][1] = out[1];
    fifo_write_ = next;
  }
}

void BrumeEngine::RunBlock() {
  const size_t size = block_size_;

  // Configuration changes happen between blocks.
  if (requested_mode_ != mode_) {
    mode_ = requested_mode_;
    ClearTank(mode_);
  }
  if (requested_quality_ != quality_ && fade_state_ != Fade::kQualityOut) {
    fade_state_ = Fade::kQualityOut;
  }
  if (clear_requested_) {
    clear_requested_ = false;
    if (fade_state_ != Fade::kQualityOut) {
      fade_state_ = Fade::kClearOut;  // a Quality change clears anyway
    }
  }

  const BrumeControls c = controls_;
  ApplyTankControls(c);

  beads::FloatFrame* frames = block_.data();

  // FILTER: Clouds' texture filter with feedback 0 (granular_processor.cc).
  const FilterSettings filter = FilterForKnob(c.filter);
  for (int ch = 0; ch < 2; ++ch) {
    lp_[ch].set_f_q<stmlib::FREQUENCY_FAST>(filter.lp, filter.lpq);
    hp_[ch].set_f_q<stmlib::FREQUENCY_FAST>(filter.hp, 1.0f);
  }
  for (size_t i = 0; i < size; ++i) {
    frames[i].l = hp_[0].Process<stmlib::FILTER_MODE_HIGH_PASS>(
        lp_[0].Process<stmlib::FILTER_MODE_LOW_PASS>(frames[i].l));
    frames[i].r = hp_[1].Process<stmlib::FILTER_MODE_HIGH_PASS>(
        lp_[1].Process<stmlib::FILTER_MODE_LOW_PASS>(frames[i].r));
  }

  // SMEAR. The Clouds classes take clouds::FloatFrame, which has the same
  // two floats; the block is copied across rather than reinterpreted.
  clouds::FloatFrame clouds_frames[kMaxBlockSize];
  for (size_t i = 0; i < size; ++i) {
    clouds_frames[i].l = frames[i].l;
    clouds_frames[i].r = frames[i].r;
  }
  diffuser_.set_amount(c.smear);
  diffuser_.Process(clouds_frames, size);

  // The shimmer return from the previous block. Skipped at gain 0, so that
  // SHIMMER at 0 leaves the signal bit-identical.
  if (shimmer_live_) {
    for (size_t i = 0; i < size; ++i) {
      clouds_frames[i].l += shimmer_return_[i].l;
      clouds_frames[i].r += shimmer_return_[i].r;
    }
  }

  for (size_t i = 0; i < size; ++i) {
    frames[i].l = clouds_frames[i].l;
    frames[i].r = clouds_frames[i].r;
    dc_blocker_[0].Process(&frames[i].l, 1);
    dc_blocker_[1].Process(&frames[i].r, 1);
  }
  pre_limiter_.Process(frames, size);

  // The active tank, fully wet. Both sum L + R at their input.
  if (mode_ == BrumeMode::kBeads) {
    beads_tank_.Process(frames, size);
  } else {
    for (size_t i = 0; i < size; ++i) {
      clouds_frames[i].l = frames[i].l;
      clouds_frames[i].r = frames[i].r;
    }
    clouds_tank_.Process(clouds_frames, size);
    for (size_t i = 0; i < size; ++i) {
      frames[i].l = clouds_frames[i].l;
      frames[i].r = clouds_frames[i].r;
    }
  }

  // (Task 7: per-tank output gain and spillover here.)

  post_limiter_.Process(frames, size);

  bool finite = true;
  for (size_t i = 0; i < size; ++i) {
    finite = finite && std::isfinite(frames[i].l) && std::isfinite(frames[i].r);
  }
  if (!finite) {
    // Can't happen with finite, clamped input; if it does, start over
    // rather than carry a NaN in a filter state.
    ClearEngine();
  } else {
    // Shimmer: the limited tank output, shifted by INTERVAL, feeds the next
    // block. The shifter always runs, so raising SHIMMER from 0 picks up
    // the current tail.
    const float gain = ShimmerGain(c.shimmer);
    pitch_shifter_.set_ratio(stmlib::SemitonesToRatio(c.interval_semitones));
    for (size_t i = 0; i < size; ++i) {
      clouds_frames[i].l = frames[i].l;
      clouds_frames[i].r = frames[i].r;
    }
    pitch_shifter_.Process(clouds_frames, size);
    for (size_t i = 0; i < size; ++i) {
      shimmer_return_[i].l = clouds_frames[i].l * gain;
      shimmer_return_[i].r = clouds_frames[i].r * gain;
    }
    shimmer_live_ = gain > 0.0f;
  }

  for (size_t i = 0; i < size; ++i) {
    if (fade_state_ == Fade::kIn) {
      fade_ = std::min(fade_ + fade_step_, 1.0f);
      if (fade_ == 1.0f) fade_state_ = Fade::kRunning;
    } else if (fade_state_ != Fade::kRunning) {
      fade_ = std::max(fade_ - fade_step_, 0.0f);
    }
    const float l = finite ? frames[i].l * fade_ : 0.0f;
    const float r = finite ? frames[i].r * fade_ : 0.0f;
    PushOutputFrame(l, r);
  }

  if (fade_ == 0.0f) {
    if (fade_state_ == Fade::kClearOut) {
      // Wet resumes at once, from an empty engine.
      ClearEngine();
      fade_ = 1.0f;
      fade_state_ = Fade::kRunning;
    } else if (fade_state_ == Fade::kQualityOut) {
      // The faded-out tail still in the output FIFO is dropped with the
      // streams; it is silence by now.
      Reconfigure();
      fade_state_ = Fade::kIn;
    }
  }
}

BrumeFrame BrumeEngine::Process(float l, float r) {
  BrumeFrame out = {0.0f, 0.0f};
  if (!ok()) {
    return out;
  }

  send_ += smoothing_ * (controls_.send - send_);
  wow_amount_ += smoothing_ * (controls_.wow - wow_amount_);
  width_ += smoothing_ * (controls_.width - width_);
  dry_wet_ += smoothing_ * (controls_.dry_wet - dry_wet_);

  l = Finite(l);
  r = Finite(r);
  // The wow stage runs in either scope, so its LFOs never stop.
  if (wow_scope_ == WowScope::kInput) {
    wow_.Process(&l, &r, wow_amount_);
  }
  const float dry_l = l;
  const float dry_r = r;

  const float send_gain = SendGain(send_);
  float send[2] = {l * send_gain, r * send_gain};
  if (wow_scope_ == WowScope::kReverbOnly) {
    wow_.Process(&send[0], &send[1], wow_amount_);
  }
  send[0] = Finite(Clamp(send[0], kMaxSend));
  send[1] = Finite(Clamp(send[1], kMaxSend));

  input_resampler_.Push(send);
  float tank_frame[2];
  while (input_resampler_.Pop(tank_frame)) {
    block_[block_fill_].l = tank_frame[0];
    block_[block_fill_].r = tank_frame[1];
    if (++block_fill_ == block_size_) {
      block_fill_ = 0;
      RunBlock();
    }
  }

  float wet_l = 0.0f;
  float wet_r = 0.0f;
  const size_t available =
      (fifo_write_ + kOutputFifoSize - fifo_read_) % kOutputFifoSize;
  if (!primed_ && available >= size_t(prime_threshold_)) {
    primed_ = true;
  }
  if (primed_) {
    if (available == 0) {
      primed_ = false;  // underflow: re-prime rather than stutter
      ++underflows_;
    } else {
      wet_l = output_fifo_[fifo_read_][0];
      wet_r = output_fifo_[fifo_read_][1];
      fifo_read_ = (fifo_read_ + 1) % kOutputFifoSize;
    }
  }

  // WIDTH on the wet.
  const float mid = 0.5f * (wet_l + wet_r);
  const float side = 0.5f * (wet_l - wet_r) * WidthGain(width_);
  wet_l = mid + side;
  wet_r = mid - side;

  // DRY/WET, Beads' laws.
  const float dry_gain = DryGain(dry_wet_);
  const float wet_gain = WetGain(dry_wet_);
  out.l = Clamp(Finite(dry_l * dry_gain + wet_l * wet_gain), kMaxOutput);
  out.r = Clamp(Finite(dry_r * dry_gain + wet_r * wet_gain), kMaxOutput);
  return out;
}

}  // namespace robotboy_beads
