#include "host/brume_engine.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace robotboy_beads {

namespace {

constexpr size_t kBeadsReverbSize = 65536;
constexpr size_t kCloudsReverbSize = 65536;
constexpr size_t kDiffuserSize = 2048;
// Per channel: 500 ms at 48 kHz, plus the interpolation's second sample and
// the sample being written.
constexpr size_t kPredelaySize = 24002;

constexpr float kSmoothingSeconds = 0.002f;
constexpr float kFadeSeconds = 0.010f;
constexpr float kGainRiseSeconds = 0.020f;
constexpr float kSpilloverSeconds = 2.0f;
constexpr float kGlideSeconds = 0.5f;
constexpr float kGlideSnap = 1e-6f;
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

// One-pole step toward `target`, landing on it exactly once close.
inline void Glide(float* value, float target, float coefficient) {
  *value += coefficient * (target - *value);
  if (std::fabs(target - *value) < kGlideSnap) *value = target;
}

// A non-finite value keeps the previous one.
inline float Sanitize(float value, float previous, float lo, float hi) {
  if (!std::isfinite(value)) return previous;
  return std::min(std::max(value, lo), hi);
}

}  // namespace

BrumeControls DefaultBrumeControls() {
  BrumeControls c;
  c.send = kSendDefault;
  c.filter = 0.5f;
  c.smear = 0.0f;
  c.predelay = kPredelayDefault;
  c.wow = 0.0f;
  c.size = kSizeDefault;
  c.decay = 0.5f;
  c.damp = kDampDefault;
  c.diffuse = kDiffuseDefault;
  c.speed = kSpeedDefault;
  c.depth = kDepthDefault;
  c.width = 0.5f;
  c.dry_wet = 0.5f;
  return c;
}

BrumeEngine::BrumeEngine() {
  controls_ = DefaultBrumeControls();
  applied_speed_[0] = applied_speed_[1] = -1.0f;
  block_.fill(beads::FloatFrame{0.0f, 0.0f});
  spill_.fill(beads::FloatFrame{0.0f, 0.0f});
  for (auto& f : output_fifo_) f.fill(0.0f);
}

BrumeEngine::~BrumeEngine() = default;

bool BrumeEngine::Init(float host_rate) {
  beads_memory_.reset(new (std::nothrow) uint16_t[kBeadsReverbSize]);
  clouds_memory_.reset(new (std::nothrow) uint16_t[kCloudsReverbSize]);
  diffuser_memory_.reset(new (std::nothrow) float[kDiffuserSize]);
  wash_memory_.reset(new (std::nothrow) float[WashDiffuser::kMemorySize]);
  predelay_memory_.reset(new (std::nothrow) float[2 * kPredelaySize]);
  if (!beads_memory_ || !clouds_memory_ || !diffuser_memory_ ||
      !wash_memory_ || !predelay_memory_) {
    beads_memory_.reset();
    clouds_memory_.reset();
    diffuser_memory_.reset();
    wash_memory_.reset();
    predelay_memory_.reset();
    return false;
  }

  host_rate_ = host_rate;
  quality_ = requested_quality_;
  mode_ = requested_mode_;
  tank_rate_ = BrumeTankRate(quality_);

  beads_tank_.Init(beads_memory_.get(), tank_rate_);
  clouds_tank_.Init(clouds_memory_.get());
  // Always the layout with room for 4x depth, so DEPTH never switches
  // layouts under a tail.
  beads_tank_.set_force_custom(true);
  clouds_tank_.set_force_custom(true);
  diffuser_.Init(diffuser_memory_.get());
  diffuser_.set_amount(0.0f);
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
  c.predelay = Sanitize(in.predelay, c.predelay, 0.0f, 1.0f);
  c.wow = Sanitize(in.wow, c.wow, 0.0f, 1.0f);
  c.size = Sanitize(in.size, c.size, 0.0f, 1.0f);
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
    wow_amount_ = WowAmount(c.wow);
    width_ = c.width;
    dry_wet_ = c.dry_wet;
    size_ = SizeScale(c.size);
    predelay_ = PredelaySeconds(c.predelay);
  }
}

// Quality (tank rate and block size) takes effect, and everything restarts
// empty.
void BrumeEngine::Reconfigure() {
  quality_ = requested_quality_;
  tank_rate_ = BrumeTankRate(quality_);
  block_size_ = size_t(tank_rate_ / 4000.0f);
  fade_step_ = 1.0f / (kFadeSeconds * tank_rate_);
  gain_rise_ = 1.0f / (kGainRiseSeconds * tank_rate_);
  gain_fall_ = 1.0f / (kSpilloverSeconds * tank_rate_);
  size_coefficient_ = 1.0f - std::exp(
      -float(block_size_) / (kGlideSeconds * tank_rate_));
  predelay_coefficient_ = 1.0f - std::exp(-1.0f / (kGlideSeconds * tank_rate_));
  wash_.Init(wash_memory_.get(), tank_rate_);
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
  // As Beads' output_limiter_ (processor.cc).
  post_limiter_[int(mode)].Init(1.0f / (tank_rate_ * 6.0f), 0.9f);
}

void BrumeEngine::ClearEngine() {
  ClearTank(BrumeMode::kBeads);
  ClearTank(BrumeMode::kClouds);
  // The spillover stops; the active tank plays at full gain.
  for (int m = 0; m < 2; ++m) {
    tank_running_[m] = m == int(mode_);
    tank_gain_[m] = tank_running_[m] ? 1.0f : 0.0f;
  }
  diffuser_.Clear();
  wash_.Clear();
  std::fill(&predelay_memory_[0], &predelay_memory_[2 * kPredelaySize], 0.0f);
  predelay_write_ = 0;
  const float pole = 1.0f - 2.0f * 3.14159265f * kDcBlockerHertz / tank_rate_;
  for (int i = 0; i < 2; ++i) {
    lp_[i].Reset();
    hp_[i].Reset();
    dc_blocker_[i].Init(pole);
  }
  // As Beads' pre_reverb_limiter_ (processor.cc).
  pre_limiter_.Init(1.0f / (tank_rate_ * 4.0f), 0.9f);
}

void BrumeEngine::ApplyTankControls(BrumeMode mode, const BrumeControls& c) {
  const float time = DecayTime(c.decay, mode);
  const float lp = DampLp(c.damp);
  const float diffusion = DiffuseCoefficient(c.diffuse, mode);
  const float speed = SpeedMultiplier(c.speed);
  const float depth = DepthMultiplier(c.depth);
  float& applied_speed = applied_speed_[int(mode)];
  if (mode == BrumeMode::kBeads) {
    beads_tank_.set_mix(1.0f);
    beads_tank_.set_time(time);
    beads_tank_.set_lp(lp);
    beads_tank_.set_diffusion(diffusion);
    beads_tank_.set_mod_depth(depth);
    beads_tank_.set_size(size_);
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
    clouds_tank_.set_mod_depth(depth);
    clouds_tank_.set_size(size_);
    if (speed != applied_speed) {
      clouds_tank_.set_lfo_rate(speed, tank_rate_);
      applied_speed = speed;
    }
  }
}

// Runs one tank, fully wet, in place. The Clouds class takes
// clouds::FloatFrame, which has the same two floats; the block is copied
// across rather than reinterpreted.
void BrumeEngine::ProcessTank(
    BrumeMode mode, beads::FloatFrame* frames, size_t size) {
  if (mode == BrumeMode::kBeads) {
    beads_tank_.Process(frames, size);
    return;
  }
  clouds::FloatFrame clouds_frames[kMaxBlockSize];
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

// PREDELAY, in place, one line per channel. The lines are always written,
// so that a glide up from 0 starts from the recent input; they are read only
// away from 0.
void BrumeEngine::ProcessPredelay(
    clouds::FloatFrame* frames, size_t size, float seconds) {
  const float max_delay = float(kPredelaySize - 2);
  float* line_l = predelay_memory_.get();
  float* line_r = line_l + kPredelaySize;
  for (size_t i = 0; i < size; ++i) {
    Glide(&predelay_, seconds, predelay_coefficient_);
    line_l[predelay_write_] = frames[i].l;
    line_r[predelay_write_] = frames[i].r;
    if (predelay_ > 0.0f) {
      const float delay = std::min(predelay_ * tank_rate_, max_delay);
      const size_t integral = size_t(delay);
      const float fractional = delay - float(integral);
      const size_t a = (predelay_write_ + kPredelaySize - integral) %
          kPredelaySize;
      const size_t b = (a + kPredelaySize - 1) % kPredelaySize;
      frames[i].l = line_l[a] + (line_l[b] - line_l[a]) * fractional;
      frames[i].r = line_r[a] + (line_r[b] - line_r[a]) * fractional;
    }
    predelay_write_ = (predelay_write_ + 1) % kPredelaySize;
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
    // The old tank spills over. The new one, if stopped, is already empty
    // and starts at full gain; if still spilling over, its gain turns
    // around from where it is.
    mode_ = requested_mode_;
    if (!tank_running_[int(mode_)]) {
      tank_running_[int(mode_)] = true;
      tank_gain_[int(mode_)] = 1.0f;
    }
  }
  const bool quality_fading =
      fade_state_ == Fade::kQualityOut || fade_state_ == Fade::kQualityDrain;
  if (requested_quality_ != quality_ && !quality_fading) {
    fade_state_ = Fade::kQualityOut;
  }
  if (clear_requested_) {
    clear_requested_ = false;
    if (!quality_fading) {
      fade_state_ = Fade::kClearOut;  // a Quality change clears anyway
    }
  }

  const BrumeControls c = controls_;
  Glide(&size_, SizeScale(c.size), size_coefficient_);
  const BrumeMode spill_mode =
      mode_ == BrumeMode::kBeads ? BrumeMode::kClouds : BrumeMode::kBeads;
  const bool spilling = tank_running_[int(spill_mode)];
  ApplyTankControls(mode_, c);
  if (spilling) {
    ApplyTankControls(spill_mode, c);
  }

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

  // SMEAR: the stock Clouds diffuser, then the wash (brume_controls.h).
  // The Clouds classes take clouds::FloatFrame, which has the same
  // two floats; the block is copied across rather than reinterpreted.
  clouds::FloatFrame clouds_frames[kMaxBlockSize];
  for (size_t i = 0; i < size; ++i) {
    clouds_frames[i].l = frames[i].l;
    clouds_frames[i].r = frames[i].r;
  }
  const SmearSettings smear = SmearForKnob(c.smear);
  diffuser_.set_amount(smear.amount);
  diffuser_.Process(clouds_frames, size);
  wash_.Process(clouds_frames, size, smear.wash);

  ProcessPredelay(clouds_frames, size, PredelaySeconds(c.predelay));

  for (size_t i = 0; i < size; ++i) {
    frames[i].l = clouds_frames[i].l;
    frames[i].r = clouds_frames[i].r;
    dc_blocker_[0].Process(&frames[i].l, 1);
    dc_blocker_[1].Process(&frames[i].r, 1);
  }
  pre_limiter_.Process(frames, size);

  // The active tank, fully wet (both sum L + R at their input), its makeup,
  // its limiter and its output gain, rising to 1. At gain 1 and Beads'
  // makeup of 1 the multiplications are exact.
  const float makeup[2] = {kBeadsMakeup, kCloudsMakeup};
  const int active = int(mode_);
  ProcessTank(mode_, frames, size);
  for (size_t i = 0; i < size; ++i) {
    frames[i].l *= makeup[active];
    frames[i].r *= makeup[active];
  }
  post_limiter_[active].Process(frames, size);
  float& rise = tank_gain_[active];
  for (size_t i = 0; i < size; ++i) {
    rise = std::min(rise + gain_rise_, 1.0f);
    frames[i].l *= rise;
    frames[i].r *= rise;
  }

  // The spillover: the other tank, with silence in, its gain falling to 0.
  beads::FloatFrame* spill = spill_.data();
  if (spilling) {
    const int other = int(spill_mode);
    for (size_t i = 0; i < size; ++i) {
      spill[i].l = spill[i].r = 0.0f;
    }
    ProcessTank(spill_mode, spill, size);
    for (size_t i = 0; i < size; ++i) {
      spill[i].l *= makeup[other];
      spill[i].r *= makeup[other];
    }
    post_limiter_[other].Process(spill, size);
    float& fall = tank_gain_[other];
    for (size_t i = 0; i < size; ++i) {
      fall = std::max(fall - gain_fall_, 0.0f);
      spill[i].l *= fall;
      spill[i].r *= fall;
    }
    if (fall == 0.0f) {
      ClearTank(spill_mode);
      tank_running_[other] = false;
    }
  }

  bool finite = true;
  for (size_t i = 0; i < size; ++i) {
    finite = finite && std::isfinite(frames[i].l) && std::isfinite(frames[i].r);
    if (spilling) {
      finite = finite && std::isfinite(spill[i].l) && std::isfinite(spill[i].r);
    }
  }
  if (!finite) {
    // Can't happen with finite, clamped input; if it does, start over
    // rather than carry a NaN in a filter state.
    ClearEngine();
  }

  for (size_t i = 0; i < size; ++i) {
    if (fade_state_ == Fade::kIn) {
      fade_ = std::min(fade_ + fade_step_, 1.0f);
      if (fade_ == 1.0f) fade_state_ = Fade::kRunning;
    } else if (fade_state_ != Fade::kRunning) {
      fade_ = std::max(fade_ - fade_step_, 0.0f);
    }
    float l = frames[i].l;
    float r = frames[i].r;
    if (spilling) {
      l += spill[i].l;
      r += spill[i].r;
    }
    l = finite ? l * fade_ : 0.0f;
    r = finite ? r * fade_ : 0.0f;
    PushOutputFrame(l, r);
  }

  if (fade_ == 0.0f) {
    if (fade_state_ == Fade::kClearOut) {
      // Wet resumes at once, from an empty engine.
      ClearEngine();
      fade_ = 1.0f;
      fade_state_ = Fade::kRunning;
    } else if (fade_state_ == Fade::kQualityOut) {
      // The output FIFO still holds the last faded frames. One more block
      // at fade 0 lets the host read them out before the streams go.
      fade_state_ = Fade::kQualityDrain;
    } else if (fade_state_ == Fade::kQualityDrain) {
      // What is dropped with the streams now is that block: silence.
      Reconfigure();
      fade_state_ = Fade::kIn;
    }
  }
}

BrumeFrame BrumeEngine::Process(float l, float r) {
  BrumeFrame out = {0.0f, 0.0f};
  if (!ok()) {
    wet_ = out;
    return out;
  }

  send_ += smoothing_ * (controls_.send - send_);
  wow_amount_ += smoothing_ * (WowAmount(controls_.wow) - wow_amount_);
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

  wet_ = {wet_l, wet_r};

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
