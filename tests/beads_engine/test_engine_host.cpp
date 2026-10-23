// EngineHost: plumbing equivalence with a direct Processor run, streaming at
// foreign host rates, trigger preservation, reconfiguration, robustness,
// and the delay/granular behaviours the modules rely on.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "host/engine_host.h"
#include "stmlib/utils/random.h"

using namespace robotboy_beads;
using beads::Processor;

namespace {

beads::RandomizedParameter Knob(float base) {
  beads::RandomizedParameter r;
  r.base = base;
  r.attenuverter = 0.0f;
  r.cv = 0.0f;
  r.has_cv = false;
  return r;
}

struct FixedControls : ControlSource {
  BlockControls c;
  int blocks = 0;
  FixedControls() {
    c.parameters.time = Knob(0.0f);
    c.parameters.size = Knob(0.6f);
    c.parameters.shape = Knob(0.5f);
    c.parameters.pitch = Knob(0.0f);
    c.parameters.monophonic_mode = false;
    c.parameters.pitch_lock = nullptr;
    c.density.base = 0.8f;
    c.density.cv = 0.0f;
    c.mix.feedback = 0.0f;
    c.mix.dry_wet = 1.0f;
    c.mix.reverb_amount = 0.0f;
    c.mix.output_gate = false;
    c.seed_button = beads::SEED_BUTTON_STATE_LOCKED;
    c.seed_patched = false;
    c.gain_setting = 1;  // manual, ~unity: keeps the level deterministic
  }
  void ReadControls(BlockControls* out) override {
    *out = c;
    ++blocks;
  }
};

float SineAt(size_t n, float freq, float rate) {
  return 0.5f * std::sin(2.0 * M_PI * freq * double(n) / rate);
}

int CountRisingEdges(const std::vector<bool>& g) {
  int n = 0;
  for (size_t i = 1; i < g.size(); ++i) n += g[i] && !g[i - 1];
  return n;
}

size_t ArgMaxAbs(const std::vector<float>& s, size_t from, size_t to) {
  size_t best = from;
  for (size_t i = from; i < to; ++i)
    if (std::fabs(s[i]) > std::fabs(s[best])) best = i;
  return best;
}

}  // namespace

TEST_CASE("At 48 kHz Bright, the host equals a direct Processor run") {
  const float sr = 48000.0f;
  const size_t n = size_t(sr * 1.5f);
  const size_t block = 12;
  FixedControls controls;
  std::vector<float> input(n);
  for (size_t i = 0; i < n; ++i) input[i] = SineAt(i, 330.0f, sr);

  // Reference: what beads.cc does, block by block.
  std::vector<float> reference(n, 0.0f);
  {
    std::vector<uint8_t> delay(3 * 262144);
    std::vector<uint16_t> reverb(65536, 0);
    auto processor = std::make_unique<Processor>();
    uint8_t* banks[3] = {&delay[0], &delay[262144], &delay[2 * 262144]};
    processor->Init(banks, reverb.data());
    processor->set_mode_lock(Processor::MODE_GRANULAR);
    processor->Configure(Processor::MEDIA_DIGITAL, true);
    GainControl gain;
    gain.Init(sr, GainControl::SOFT_CLIPPING_MODE_OFF, true);
    robotboy_stmlib::Random::Seed(42);
    bool freeze[12] = {};
    for (size_t b = 0; b < n / block; ++b) {
      beads::FloatFrame in[12], g[12], out[12];
      for (size_t i = 0; i < block; ++i)
        in[i].l = in[i].r = input[b * block + i];
      const bool active = b >= 1000;
      gain.Process(active, 1, false, true, in, g, block);
      if (!active) continue;
      BlockControls c = controls.c;
      processor->Process(c.seed_button, nullptr, freeze, c.density,
                         c.parameters, c.mix, g, out, block);
      for (size_t i = 0; i < block; ++i) reference[b * block + i] = out[i].l;
    }
  }

  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_GRANULAR, true, sr));
  host.SetInputs(true, false);
  robotboy_stmlib::Random::Seed(42);
  std::vector<float> output(n);
  for (size_t i = 0; i < n; ++i) {
    output[i] = host.Process({input[i], 0.0f, false, false}, &controls).l;
  }
  const int lag = host.bypass_latency();
  REQUIRE(lag == 11);
  size_t compared = 0;
  for (size_t i = 12000; i + lag < n; ++i) {
    REQUIRE(output[i + lag] == reference[i]);
    ++compared;
  }
  CHECK(compared > 50000);
  float peak = 0.0f;
  for (float x : output) peak = std::max(peak, std::fabs(x));
  CHECK(peak > 0.05f);
}

TEST_CASE("Streaming at foreign host rates has no gaps, every quality") {
  for (float host_rate : {44100.0f, 96000.0f, 48000.0f}) {
    for (int q = 0; q < 4; ++q) {
      CAPTURE(host_rate, q);
      FixedControls controls;
      controls.c.mix.dry_wet = 0.0f;  // dry: a continuous sine end to end
      EngineHost host;
      REQUIRE(host.Init(Processor::MODE_DELAY, false, host_rate));
      host.SetQuality(Quality(q));
      host.SetInputs(true, true);
      const size_t n = size_t(host_rate * 1.5f);
      size_t zero_run = 0, worst_run = 0;
      float peak = 0.0f;
      for (size_t i = 0; i < n; ++i) {
        const float x = SineAt(i, 440.0f, host_rate);
        const HostFrameOut out =
            host.Process({x, x, false, false}, &controls);
        REQUIRE(std::isfinite(out.l));
        if (i < size_t(host_rate * 0.6f)) continue;  // mute + settling
        peak = std::max(peak, std::fabs(out.l));
        zero_run = std::fabs(out.l) < 1e-6f ? zero_run + 1 : 0;
        worst_run = std::max(worst_run, zero_run);
      }
      CHECK(worst_run < 3);
      CHECK(peak > 0.2f);
      CHECK(host.active_quality() == Quality(q));
    }
  }
}

TEST_CASE("One-sample Seed triggers at 96 kHz each start one grain") {
  const float sr = 96000.0f;
  FixedControls controls;
  controls.c.seed_patched = true;
  controls.c.density.base = 0.0f;  // divider 1/1, probability 1
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_GRANULAR, true, sr));
  host.SetInputs(true, true);
  std::vector<bool> gates;
  const size_t n = size_t(sr * 2.0f);
  for (size_t i = 0; i < n; ++i) {
    const bool seed = i == size_t(sr * 0.6f) || i == size_t(sr * 1.0f) ||
                      i == size_t(sr * 1.4f);
    const float x = SineAt(i, 220.0f, sr);
    gates.push_back(host.Process({x, x, false, seed}, &controls).gate);
  }
  CHECK(CountRisingEdges(gates) == 3);
}

TEST_CASE("A quality change mutes for 0.25 s, then sound returns") {
  const float sr = 48000.0f;
  FixedControls controls;
  controls.c.mix.dry_wet = 0.0f;
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_DELAY, false, sr));
  host.SetInputs(true, true);
  std::vector<float> out;
  for (size_t i = 0; i < size_t(sr * 2.0f); ++i) {
    if (i == size_t(sr)) host.SetQuality(Quality::kScorched);
    const float x = SineAt(i, 440.0f, sr);
    out.push_back(host.Process({x, x, false, false}, &controls).l);
  }
  auto peak = [&](float a, float b) {
    float p = 0.0f;
    for (size_t i = size_t(sr * a); i < size_t(sr * b); ++i)
      p = std::max(p, std::fabs(out[i]));
    return p;
  };
  CHECK(peak(0.5f, 0.99f) > 0.2f);
  CHECK(peak(1.01f, 1.24f) == 0.0f);
  CHECK(peak(1.4f, 2.0f) > 0.2f);
  CHECK(host.engine_rate() == 24000.0f);
}

TEST_CASE("A quality change waits while frozen") {
  FixedControls controls;
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_GRANULAR, true, 48000.0f));
  host.SetInputs(true, true);
  host.SetQuality(Quality::kCold);
  for (int i = 0; i < 4800; ++i) host.Process({0.1f, 0.1f, true, false}, &controls);
  CHECK(host.active_quality() == Quality::kBright);
  for (int i = 0; i < 4800; ++i) host.Process({0.1f, 0.1f, false, false}, &controls);
  CHECK(host.active_quality() == Quality::kCold);
}

TEST_CASE("NaN and huge inputs never reach the output") {
  FixedControls controls;
  controls.c.mix.feedback = 1.0f;
  controls.c.gain_setting = 0;  // auto gain
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_GRANULAR, true, 44100.0f));
  host.SetInputs(true, true);
  for (int i = 0; i < 44100 * 2; ++i) {
    const float x = (i % 3 == 0) ? NAN : ((i % 3 == 1) ? 200.0f : -INFINITY);
    const HostFrameOut out = host.Process({x, x, false, i % 100 == 0}, &controls);
    REQUIRE(std::isfinite(out.l));
    REQUIRE(std::isfinite(out.r));
    REQUIRE(std::fabs(out.l) < 4.0f);
  }
}

TEST_CASE("An uninitialized host outputs silence") {
  FixedControls controls;
  EngineHost host;  // Init never called (as after an allocation failure)
  CHECK_FALSE(host.ok());
  const HostFrameOut out = host.Process({0.5f, 0.5f, false, true}, &controls);
  CHECK(out.l == 0.0f);
  CHECK(out.r == 0.0f);
  CHECK_FALSE(out.gate);
}

TEST_CASE("Delay mode: free-running echo at the full buffer length") {
  const float sr = 48000.0f;
  FixedControls controls;
  controls.c.density.base = 0.5f;  // longest interval
  controls.c.parameters.shape = Knob(0.0f);
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_DELAY, false, sr));
  host.SetInputs(true, true);
  const size_t impulse = size_t(sr * 6.0f);  // after the interval settles
  std::vector<float> out;
  for (size_t i = 0; i < size_t(sr * 10.5f); ++i) {
    const float x = i == impulse ? 0.9f : 0.0f;
    out.push_back(host.Process({x, x, false, false}, &controls).l);
  }
  const size_t peak = ArgMaxAbs(out, impulse + 100, out.size());
  const float delay_s = float(peak - impulse) / sr;
  // The buffer is 192000 frames (4 s), but Beads' density hysteresis filter
  // (0.0025) settles a noon knob at 0.4975 when approached from zero, which
  // is 0.54 semitones short of the longest interval: 4 s * 2^(-0.54/12).
  CHECK(std::fabs(delay_s - 4.0f * std::pow(2.0f, -0.54f / 12.0f)) < 0.005f);
  CHECK(std::fabs(out[peak]) > 0.1f);
}

TEST_CASE("Delay mode: tapped tempo sets the echo spacing") {
  const float sr = 48000.0f;
  struct TapControls : FixedControls {
    size_t sample = 0;
    void ReadControls(BlockControls* out) override {
      *out = c;
      // Four taps 0.5 s apart, starting at 1 s: ON for one block each.
      const float t = float(sample) / 48000.0f;
      bool on = false;
      for (int k = 0; k < 4; ++k) {
        const float tap = 1.0f + 0.5f * k;
        on = on || (t >= tap && t < tap + 0.00025f);
      }
      out->seed_button = on ? beads::SEED_BUTTON_STATE_ON
                            : (t > 1.0f ? beads::SEED_BUTTON_STATE_OFF
                                        : beads::SEED_BUTTON_STATE_LOCKED);
    }
  } controls;
  controls.c.density.base = 0.5f;
  controls.c.parameters.shape = Knob(0.0f);
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_DELAY, false, sr));
  host.SetInputs(true, true);
  const size_t impulse = size_t(sr * 4.0f);
  std::vector<float> out;
  for (size_t i = 0; i < size_t(sr * 5.0f); ++i) {
    controls.sample = i;
    const float x = i == impulse ? 0.9f : 0.0f;
    out.push_back(host.Process({x, x, false, false}, &controls).l);
  }
  const size_t peak = ArgMaxAbs(out, impulse + 100, out.size());
  CHECK(std::fabs(float(peak - impulse) / sr - 0.5f) < 0.005f);
}

TEST_CASE("Freeze keeps grains playing from the frozen recording") {
  const float sr = 48000.0f;
  auto tail_rms = [&](bool freeze) {
    FixedControls controls;
    EngineHost host;
    REQUIRE(host.Init(Processor::MODE_GRANULAR, true, sr));
    host.SetInputs(true, true);
    double acc = 0.0;
    size_t count = 0;
    for (size_t i = 0; i < size_t(sr * 4.0f); ++i) {
      const bool second_half = i >= size_t(sr * 2.0f);
      const float x = second_half ? 0.0f : SineAt(i, 220.0f, sr);
      const HostFrameOut out =
          host.Process({x, x, freeze && second_half, false}, &controls);
      if (i >= size_t(sr * 3.0f)) { acc += out.l * out.l; ++count; }
    }
    return std::sqrt(acc / double(count));
  };
  CHECK(tail_rms(true) > 0.02);
  CHECK(tail_rms(false) < 0.001);
}

TEST_CASE("Clear empties the buffer and the menu flag follows") {
  FixedControls controls;
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_GRANULAR, true, 48000.0f));
  host.SetInputs(true, true);
  CHECK(host.buffer_empty());
  for (int i = 0; i < 48000; ++i)
    host.Process({SineAt(i, 220.0f, 48000.0f), 0.0f, false, false}, &controls);
  CHECK_FALSE(host.buffer_empty());
  host.RequestClear();
  for (int i = 0; i < 24; ++i) host.Process({0.0f, 0.0f, false, false}, &controls);
  CHECK(host.buffer_empty());
}

TEST_CASE("Output is clipped to the codec's full scale, as in beads.cc") {
  FixedControls controls;
  controls.c.gain_setting = 255;      // +32 dB manual gain
  controls.c.mix.feedback = 1.0f;
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_DELAY, false, 48000.0f));
  host.SetInputs(true, true);
  float peak = 0.0f;
  for (int i = 0; i < 48000 * 2; ++i) {
    const float x = (i % 4800 == 0) ? 1.0f : 0.0f;  // impulses: fast transients
    const HostFrameOut out = host.Process({x, x, false, false}, &controls);
    peak = std::max(peak, std::max(std::fabs(out.l), std::fabs(out.r)));
  }
  CHECK(peak <= 1.0f);
}

TEST_CASE("Scorched cassette hisses unless the hiss is switched off") {
  auto rms_of_silence = [](robotboy_beads::Quality q, bool hiss) {
    FixedControls controls;
    controls.c.mix.dry_wet = 0.0f;  // dry only: the hiss shows up directly
    // Auto gain: Beads' manual-gain soft clipper in Scorched is asymmetric
    // and turns silence into a small DC offset, which would mask the test.
    controls.c.gain_setting = 0;
    EngineHost host;
    REQUIRE(host.Init(Processor::MODE_GRANULAR, true, 48000.0f));
    host.SetQuality(q);
    host.SetHissEnabled(hiss);
    host.SetInputs(true, true);
    double acc = 0.0;
    size_t n = 0;
    for (int i = 0; i < 48000; ++i) {
      const HostFrameOut out = host.Process({0.0f, 0.0f, false, false}, &controls);
      if (i > 24000) { acc += out.l * out.l; ++n; }
    }
    return std::sqrt(acc / double(n));
  };
  const double hiss = rms_of_silence(Quality::kScorched, true);
  CHECK(hiss > 2e-5);
  CHECK(hiss < 1e-3);
  CHECK(rms_of_silence(Quality::kScorched, false) == 0.0);
  CHECK(rms_of_silence(Quality::kBright, true) == 0.0);
}

TEST_CASE("Hiss doesn't mark an empty buffer as recorded") {
  FixedControls controls;
  controls.c.gain_setting = 0;  // see above: no soft-clip DC offset
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_GRANULAR, true, 48000.0f));
  host.SetQuality(Quality::kScorched);
  host.SetInputs(true, true);
  for (int i = 0; i < 48000; ++i) host.Process({0.0f, 0.0f, false, false}, &controls);
  CHECK(host.buffer_empty());
}
