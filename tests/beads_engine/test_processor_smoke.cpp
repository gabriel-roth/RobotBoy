// Smoke test of the vendored, patched beads::Processor: every media in both
// mono and stereo, and both pinned modes, render finite, non-silent audio.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "beads/dsp/processor.h"
#include "stmlib/utils/random.h"

using beads::FloatFrame;
using beads::Processor;

namespace {

struct ProcessorRig {
  std::vector<uint8_t> delay_memory = std::vector<uint8_t>(3 * 262144);
  std::vector<uint16_t> reverb_memory = std::vector<uint16_t>(32768);
  std::unique_ptr<Processor> processor = std::make_unique<Processor>();

  ProcessorRig() {
    uint8_t* banks[3] = {
        &delay_memory[0], &delay_memory[262144], &delay_memory[2 * 262144]};
    processor->Init(banks, reverb_memory.data());
  }
};

beads::RandomizedParameter Knob(float base) {
  beads::RandomizedParameter r;
  r.base = base;
  r.attenuverter = 0.0f;
  r.cv = 0.0f;
  r.has_cv = false;
  return r;
}

// Renders `seconds` of a 220 Hz sine through the processor and returns
// the peak absolute output; fails the test on any non-finite sample.
float Render(Processor* processor, float seconds, float size, float density) {
  const float sr = processor->sample_rate();
  const size_t block = size_t(sr / 4000.0f);
  beads::Parameters p;
  p.time = Knob(0.05f);
  p.size = Knob(size);
  p.shape = Knob(0.5f);
  p.pitch = Knob(0.0f);
  p.monophonic_mode = false;
  p.pitch_lock = nullptr;
  beads::ModulatedParameter d;
  d.base = density;
  d.cv = 0.0f;
  Processor::MixParameters m;
  m.feedback = 0.3f;
  m.dry_wet = 1.0f;
  m.reverb_amount = 0.3f;
  m.output_gate = false;

  std::vector<FloatFrame> in(block), out(block);
  bool freeze[64] = {};
  float peak = 0.0f;
  float phase = 0.0f;
  const size_t blocks = size_t(seconds * 4000.0f);
  for (size_t b = 0; b < blocks; ++b) {
    for (size_t i = 0; i < block; ++i) {
      phase += 220.0f / sr;
      if (phase >= 1.0f) phase -= 1.0f;
      in[i].l = in[i].r = 0.5f * std::sin(2.0f * float(M_PI) * phase);
    }
    processor->Process(beads::SEED_BUTTON_STATE_LOCKED, nullptr, freeze, d,
                       p, m, in.data(), out.data(), block);
    for (size_t i = 0; i < block; ++i) {
      REQUIRE(std::isfinite(out[i].l));
      REQUIRE(std::isfinite(out[i].r));
      peak = std::max(peak, std::max(std::fabs(out[i].l), std::fabs(out[i].r)));
    }
  }
  return peak;
}

}  // namespace

TEST_CASE("Patched processor renders every media, mono and stereo") {
  ProcessorRig rig;
  robotboy_stmlib::Random::Seed(1);
  for (int media = 0; media < 4; ++media) {
    for (int mono = 0; mono < 2; ++mono) {
      rig.processor->Configure(Processor::Media(media), mono != 0);
      rig.processor->set_mode_lock(Processor::MODE_GRANULAR);
      CAPTURE(media, mono);
      CHECK(Render(rig.processor.get(), 2.0f, 0.6f, 0.8f) > 0.01f);
      rig.processor->set_mode_lock(Processor::MODE_DELAY);
      CHECK(Render(rig.processor.get(), 2.0f, 0.6f, 0.4f) > 0.01f);
    }
  }
}

TEST_CASE("Granular lock ignores a fully-clockwise Size") {
  ProcessorRig rig;
  rig.processor->Configure(Processor::MEDIA_DIGITAL, false);
  rig.processor->set_mode_lock(Processor::MODE_GRANULAR);
  // Size 1.0 would cross into delay mode after 1.5 s in MODE_AUTO; with the
  // lock, grains keep playing (num_active_grains > 0 at the end).
  Render(rig.processor.get(), 3.0f, 1.0f, 0.8f);
  CHECK(rig.processor->num_active_grains() > 0);
}
