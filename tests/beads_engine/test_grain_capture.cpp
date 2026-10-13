// Grain capture for the Lucarne display: vendor accessors, the feed, and
// proof that none of it changes Particules' audio.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include "host/engine_host.h"
#include "stmlib/utils/random.h"

using namespace robotboy_beads;
using beads::Processor;

namespace {

beads::RandomizedParameter Knob(float base, float attenuverter = 0.0f) {
  beads::RandomizedParameter r;
  r.base = base;
  r.attenuverter = attenuverter;
  r.cv = 0.0f;
  r.has_cv = false;
  return r;
}

struct Controls : ControlSource {
  BlockControls c;
  Controls() {
    c.parameters.time = Knob(0.3f, 0.4f);   // randomized: grains spread out
    c.parameters.size = Knob(0.6f, 0.3f);
    c.parameters.shape = Knob(0.5f);
    c.parameters.pitch = Knob(0.0f, 0.5f);  // randomized pitch
    c.parameters.monophonic_mode = false;
    c.parameters.pitch_lock = nullptr;
    c.density.base = 0.85f;
    c.density.cv = 0.0f;
    c.mix.feedback = 0.0f;
    c.mix.dry_wet = 1.0f;
    c.mix.reverb_amount = 0.0f;
    c.mix.output_gate = false;
    c.seed_button = beads::SEED_BUTTON_STATE_LOCKED;
    c.seed_patched = false;
    c.gain_setting = 1;
  }
  void ReadControls(BlockControls* out) override { *out = c; }
};

float Sine(size_t n) { return 0.5f * float(std::sin(2.0 * M_PI * 220.0 * double(n) / 48000.0)); }

// FNV-1a over the output's float bits: a fingerprint of the exact audio.
uint64_t Fingerprint(EngineHost& host, Controls& controls, size_t n) {
  robotboy_stmlib::Random::Seed(1234);
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i) {
    const HostFrameOut o = host.Process({Sine(i), Sine(i + 77), false, false}, &controls);
    uint32_t bits[2];
    std::memcpy(&bits[0], &o.l, 4);
    std::memcpy(&bits[1], &o.r, 4);
    for (uint32_t b : bits)
      for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xff; h *= 1099511628211ull; }
  }
  return h;
}

struct Probe {
  int write = -1;
  uint32_t serials = 0;
  int max_delay = 0;
  template <typename GP> void operator()(const GP& gp, bool mono) {
    REQUIRE(mono);
    write = gp.mono_delay_line().write_ptr();
    max_delay = std::remove_reference_t<decltype(gp.mono_delay_line())>::MAX_DELAY;
    serials = 0;
    for (int s = 0; s < 32; ++s) serials += gp.granular_player().grain(s).serial();
  }
};

}  // namespace

TEST_CASE("Particules' Beads output matches the pre-display fingerprint") {
  Controls controls;
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_GRANULAR, true, 48000.0f));
  host.SetInputs(true, true);
  const uint64_t h = Fingerprint(host, controls, 48000 * 2);
  INFO("fingerprint " << h);
  CHECK(h == 13996010712326909390ull);  // replaced with the baseline in Step 2
}

TEST_CASE("Grain accessors: serials count starts, write pointer moves") {
  std::vector<uint8_t> delay(3 * 262144);
  std::vector<uint16_t> reverb(32768, 0);
  auto proc = std::make_unique<Processor>();
  Processor& p = *proc;
  uint8_t* banks[3] = {&delay[0], &delay[262144], &delay[2 * 262144]};
  p.Init(banks, reverb.data());
  p.set_mode_lock(Processor::MODE_GRANULAR);
  p.Configure(Processor::MEDIA_DIGITAL, true);
  Controls controls;
  robotboy_stmlib::Random::Seed(7);
  Probe before;
  p.VisitGranular(before);
  bool freeze[12] = {};
  for (int b = 0; b < 4000; ++b) {  // 1 s
    beads::FloatFrame in[12], out[12];
    for (int i = 0; i < 12; ++i) in[i].l = in[i].r = Sine(size_t(b * 12 + i));
    p.Process(controls.c.seed_button, nullptr, freeze, controls.c.density,
              controls.c.parameters, controls.c.mix, in, out, 12);
  }
  Probe after;
  p.VisitGranular(after);
  CHECK(after.serials > before.serials + 10);  // many grains started
  // 48000 samples written; the pointer moved back by that much, mod length.
  CHECK(after.write == ((before.write - 48000) % after.max_delay + after.max_delay) % after.max_delay);
}
