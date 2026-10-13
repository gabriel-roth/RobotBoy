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
#include "host/grain_capture.h"
#include "host/grain_feed.h"
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
  CHECK(h == 13996010712326909390ull);  // baseline recorded on the unmodified vendor tree
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

namespace {

struct Run {
  Controls controls;
  EngineHost host;
  GrainFeed feed;
  size_t n = 0;
  explicit Run(bool stereo = false) {
    REQUIRE(host.Init(Processor::MODE_GRANULAR, true, 48000.0f));
    host.SetInputs(true, stereo);
    host.SetGrainFeed(&feed);
    feed.attached.store(true);
    robotboy_stmlib::Random::Seed(99);
  }
  void Seconds(float s, bool freeze = false) {
    const size_t end = n + size_t(s * 48000.0f);
    for (; n < end; ++n) host.Process({Sine(n), Sine(n), freeze, false}, &controls);
  }
  std::vector<GrainStartEvent> Events(uint32_t* cursor) {
    std::vector<GrainStartEvent> out(GrainFeed::kEvents);
    out.resize(size_t(feed.ReadEvents(cursor, out.data(), GrainFeed::kEvents)));
    return out;
  }
};

}  // namespace

TEST_CASE("Index and column helpers") {
  // Hermite's latency fix: index = write + integral delay + max_delay - 1.
  CHECK(GrainCapture::ReadIndex(100, 10 << 10, 1000) == 109);
  CHECK(GrainCapture::ReadIndex(995, 10 << 10, 1000) == 4);
  CHECK(GrainCapture::Column(999, 1000) == 0);      // oldest index side is left
  CHECK(GrainCapture::Column(0, 1000) == 255);
}

TEST_CASE("Capture publishes start events that match the controls") {
  Run r;
  r.controls.c.parameters.time = Knob(0.5f);
  r.controls.c.parameters.size = Knob(0.6f);
  r.controls.c.parameters.pitch = Knob(12.0f);  // +1 octave: speed 2
  r.Seconds(1.5f);                                // past the 0.25 s mute
  uint32_t cursor = r.feed.event_write() - 10;
  const auto events = r.Events(&cursor);
  REQUIRE(events.size() == 10);
  GrainLiveFrame f;
  REQUIRE(r.feed.ReadFrame(&f));
  for (const auto& e : events) {
    CHECK(e.speed == Catch::Approx(2.0f).epsilon(0.01));
    CHECK(e.duration > 0.0f);
    CHECK(e.start_index >= 0);
    CHECK(e.start_index < f.max_delay);
    CHECK(e.slot >= 0);
    CHECK(e.slot < 32);
  }
  for (size_t i = 1; i < events.size(); ++i)
    if (events[i].slot == events[i - 1].slot) CHECK(events[i].serial > events[i - 1].serial);
}

TEST_CASE("Pan: centred for a steady pulse, spread for a random cloud") {
  auto pans = [](float density, float randomness) {
    Run r;
    r.controls.c.parameters.time = Knob(0.5f, randomness);
    r.controls.c.parameters.size = Knob(0.6f);
    r.controls.c.parameters.shape = Knob(0.5f);
    r.controls.c.parameters.pitch = Knob(0.0f);
    r.controls.c.density.base = density;
    r.Seconds(1.5f);
    uint32_t cursor = r.feed.event_write() - 20;
    std::vector<float> out;
    for (const auto& e : r.Events(&cursor)) out.push_back(e.pan);
    return out;
  };
  // Density counter-clockwise (steady pulse), no attenurandomizer: Beads'
  // spread is zero, so every grain is dead centre.
  const auto steady = pans(0.2f, 0.0f);
  REQUIRE(steady.size() == 20);
  for (float p : steady) CHECK(p == 0.5f);
  // Density clockwise (random cloud): full spread, pans scatter.
  const auto cloud = pans(0.85f, 0.0f);
  REQUIRE(cloud.size() == 20);
  float widest = 0.0f;
  for (float p : cloud) {
    CHECK(p >= 0.0f);
    CHECK(p <= 1.0f);
    widest = std::max(widest, std::fabs(p - 0.5f));
  }
  CHECK(widest > 0.1f);
}

TEST_CASE("Peaks fill where audio was written; a clear empties them") {
  Run r;
  r.Seconds(1.0f);
  int8_t lo[GrainFeed::kColumns], hi[GrainFeed::kColumns];
  r.feed.ReadPeaks(lo, hi);
  int loud = 0;
  for (int c = 0; c < GrainFeed::kColumns; ++c) loud += hi[c] > 20 && lo[c] < -20;
  CHECK(loud > 15);  // \~0.75 s of an 8 s mono buffer is \~24 columns
  const uint32_t epoch = r.feed.epoch();
  r.host.RequestClear();
  r.Seconds(0.01f);
  CHECK(r.feed.epoch() != epoch);
}

TEST_CASE("Attaching to a frozen buffer backfills the waveform") {
  Run r;
  r.feed.attached.store(false);
  r.Seconds(1.5f);
  r.Seconds(0.1f, /*freeze=*/true);
  r.feed.attached.store(true);
  int8_t lo[GrainFeed::kColumns], hi[GrainFeed::kColumns];
  // Gradual: one block reads 192 samples, less than one 1500-sample column.
  r.Seconds(24.0f / 48000.0f, true);
  r.feed.ReadPeaks(lo, hi);
  int filled = 0;
  for (int c = 0; c < GrainFeed::kColumns; ++c) filled += lo[c] != 0 || hi[c] != 0;
  CHECK(filled == 0);
  // Newest first: 0.3 s covers the 1.25 s of recorded audio.
  r.Seconds(0.3f, true);
  r.feed.ReadPeaks(lo, hi);
  int loud = 0;
  for (int c = 0; c < GrainFeed::kColumns; ++c) loud += hi[c] > 20 && lo[c] < -20;
  CHECK(loud > 15);
}

TEST_CASE("Freeze stops peak updates but not grains") {
  Run r;
  r.Seconds(1.0f);
  r.Seconds(0.1f, /*freeze=*/true);  // the frozen flag reaches the line a block late
  GrainLiveFrame a, b;
  REQUIRE(r.feed.ReadFrame(&a));
  CHECK(a.frozen);
  int8_t lo1[GrainFeed::kColumns], hi1[GrainFeed::kColumns];
  int8_t lo2[GrainFeed::kColumns], hi2[GrainFeed::kColumns];
  r.feed.ReadPeaks(lo1, hi1);
  uint32_t cursor = r.feed.event_write();
  r.Seconds(0.5f, true);
  REQUIRE(r.feed.ReadFrame(&b));
  CHECK(b.write_index == a.write_index);  // write head stopped
  r.feed.ReadPeaks(lo2, hi2);
  CHECK(std::memcmp(lo1, lo2, sizeof lo1) == 0);
  CHECK(std::memcmp(hi1, hi2, sizeof hi1) == 0);
  CHECK(!r.Events(&cursor).empty());       // grains keep coming
}

TEST_CASE("Reformat bumps the epoch") {
  Run r;
  r.Seconds(1.0f);
  const uint32_t epoch = r.feed.epoch();
  r.host.SetInputs(true, true);  // mono -> stereo
  r.Seconds(0.5f);
  CHECK(r.feed.epoch() != epoch);
  GrainLiveFrame f;
  REQUIRE(r.feed.ReadFrame(&f));
  CHECK(f.max_delay == 192000);  // Bright stereo: 768 KB / 4 bytes
}

TEST_CASE("Detach stops capture; reattach resyncs") {
  Run r;
  r.Seconds(1.0f);
  r.feed.attached.store(false);
  const uint32_t w = r.feed.event_write();
  r.Seconds(0.5f);
  CHECK(r.feed.event_write() == w);
  const uint32_t epoch = r.feed.epoch();
  r.feed.attached.store(true);
  r.Seconds(0.01f);
  CHECK(r.feed.epoch() != epoch);
}

TEST_CASE("Event ring overrun skips to the newest") {
  GrainFeed feed;
  for (uint32_t i = 0; i < 300; ++i) feed.PushEvent({i, 0, 0, 1.0f, 1.0f});
  uint32_t cursor = 0;
  std::vector<GrainStartEvent> out(GrainFeed::kEvents);
  const int n = feed.ReadEvents(&cursor, out.data(), GrainFeed::kEvents);
  CHECK(n == GrainFeed::kEvents - 1);  // the oldest ring slot is never trusted
  CHECK(out[0].serial == 300 - (GrainFeed::kEvents - 1));
  CHECK(out[n - 1].serial == 299);
  CHECK(cursor == 300);
}

TEST_CASE("Capture leaves the audio bit-identical") {
  Controls c1, c2;
  EngineHost plain, watched;
  GrainFeed feed;
  REQUIRE(plain.Init(Processor::MODE_GRANULAR, true, 48000.0f));
  REQUIRE(watched.Init(Processor::MODE_GRANULAR, true, 48000.0f));
  plain.SetInputs(true, true);
  watched.SetInputs(true, true);
  watched.SetGrainFeed(&feed);
  feed.attached.store(true);
  CHECK(Fingerprint(plain, c1, 48000 * 2) == Fingerprint(watched, c2, 48000 * 2));
}
