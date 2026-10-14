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

TEST_CASE("Retours' Beads output matches the pre-tap-display fingerprint") {
  Controls controls;
  controls.c.parameters.size = Knob(1.0f);   // delay mode's own knob position
  controls.c.density.base = 0.7f;            // Interval: a second ratio fades in
  controls.c.mix.feedback = 0.5f;
  EngineHost host;
  REQUIRE(host.Init(Processor::MODE_DELAY, true, 48000.0f));
  host.SetInputs(true, true);
  const uint64_t h = Fingerprint(host, controls, 48000 * 2);
  INFO("fingerprint " << h);
  CHECK(h == 5147439100107793283ull);  // baseline on the unmodified vendor tree
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

TEST_CASE("Pan: centered for a steady pulse, spread for a random cloud") {
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
  // spread is zero, so every grain is dead center.
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

namespace {
struct TapProbe {
  bool tap_mode = false;
  int ratio = 0;
  beads::DelayController::Output taps{};
  template <typename GP> void operator()(const GP& gp, bool) {
    tap_mode = gp.tap_mode();
    ratio = GP::kDownsampleRatio;
    taps = gp.last_taps();
  }
};
}  // namespace

TEST_CASE("Tap accessors: delay mode exposes taps and play heads") {
  std::vector<uint8_t> delay(3 * 262144);
  std::vector<uint16_t> reverb(32768, 0);
  auto proc = std::make_unique<Processor>();
  Processor& p = *proc;
  uint8_t* banks[3] = {&delay[0], &delay[262144], &delay[2 * 262144]};
  p.Init(banks, reverb.data());
  p.set_mode_lock(Processor::MODE_DELAY);
  p.Configure(Processor::MEDIA_DIGITAL, true);
  Controls controls;
  controls.c.parameters.size = Knob(1.0f);
  bool freeze[12] = {};
  auto run = [&](int blocks) {
    for (int b = 0; b < blocks; ++b) {
      beads::FloatFrame in[12], out[12];
      for (int i = 0; i < 12; ++i) in[i].l = in[i].r = Sine(size_t(b * 12 + i));
      p.Process(controls.c.seed_button, nullptr, freeze, controls.c.density,
                controls.c.parameters, controls.c.mix, in, out, 12);
    }
  };
  run(4000);
  TapProbe t;
  p.VisitGranular(t);
  CHECK(t.tap_mode);
  CHECK(t.ratio == 1);   // MEDIA_DIGITAL runs at the engine rate
  int audible = 0;
  for (int i = 0; i < beads::DelayController::NUM_TAPS; ++i)
    if (t.taps.tap[i].gain > 0.0f && t.taps.tap[i].delay > 0) ++audible;
  CHECK(audible >= 1);
  CHECK_FALSE(p.delay_controller().frozen());

  for (bool& f : freeze) f = true;
  run(4000);
  CHECK(p.delay_controller().frozen());
  uint32_t serials = 0;
  for (int h = 0; h < 4; ++h) {
    serials += p.delay_controller().play_head(h).serial() + p.delay_controller().slave_play_head(h).serial();
    const auto& ph = p.delay_controller().play_head(h);
    if (!ph.done()) {
      CHECK(ph.duration() > 0);
      CHECK(ph.remaining() <= ph.duration());
    }
  }
  CHECK(serials > 0);

  // Granular mode never reports tap mode.
  p.set_mode_lock(Processor::MODE_GRANULAR);
  for (bool& f : freeze) f = false;
  run(4000);
  TapProbe g;
  p.VisitGranular(g);
  CHECK_FALSE(g.tap_mode);
}

namespace {
struct DelayRun {
  Controls controls;
  EngineHost host;
  GrainFeed feed;
  size_t n = 0;
  DelayRun() {
    controls.c.parameters.size = Knob(1.0f);
    controls.c.density.base = 0.7f;
    controls.c.mix.feedback = 0.5f;
    REQUIRE(host.Init(Processor::MODE_DELAY, true, 48000.0f));
    host.SetInputs(true, false);
    host.SetGrainFeed(&feed);
    feed.attached.store(true);
    robotboy_stmlib::Random::Seed(5);
  }
  void Seconds(float s, bool freeze = false) {
    const size_t end = n + size_t(s * 48000.0f);
    for (; n < end; ++n) host.Process({Sine(n), Sine(n), freeze, false}, &controls);
  }
};
}  // namespace

TEST_CASE("Delay mode publishes taps, not grains") {
  DelayRun r;
  const uint32_t events_before = r.feed.event_write();
  r.Seconds(1.5f);
  CHECK(r.feed.event_write() == events_before);
  GrainLiveFrame f;
  REQUIRE(r.feed.ReadFrame(&f));
  CHECK(f.tap_mode);
  int audible = 0;
  for (const TapState& t : f.taps) {
    CHECK_FALSE(t.slice);
    CHECK(t.read_index >= 0);
    CHECK(t.read_index < f.max_delay);
    if (t.level > 0.0f) ++audible;
  }
  CHECK(audible >= 1);
  for (const auto& s : f.slots) CHECK(s.done);
}

TEST_CASE("Tap read index matches the rendered delay") {
  DelayRun r;
  r.Seconds(1.0f);
  GrainLiveFrame f;
  REQUIRE(r.feed.ReadFrame(&f));
  struct Got {
    beads::DelayController::Output taps;
    int32_t write = 0, max_delay = 0;
  } g;
  auto get = [&g](const auto& gp, bool mono) {
    g.taps = gp.last_taps();
    if (mono) {
      g.write = gp.mono_delay_line().write_ptr();
      g.max_delay = std::remove_reference_t<decltype(gp.mono_delay_line())>::MAX_DELAY;
    } else {
      g.write = gp.stereo_delay_line().write_ptr();
      g.max_delay = std::remove_reference_t<decltype(gp.stereo_delay_line())>::MAX_DELAY;
    }
  };
  r.host.processor().VisitGranular(get);
  REQUIRE(g.max_delay == f.max_delay);
  CHECK(f.write_index == g.write);
  for (int i = 0; i < 8; ++i)
    if (g.taps.tap[i].delay > 0)
      CHECK(f.taps[i].read_index == GrainCapture::ReadIndex(g.write, g.taps.tap[i].delay, g.max_delay));
}

TEST_CASE("Slice: play heads publish slices whose serials advance") {
  DelayRun r;
  r.Seconds(1.0f);
  r.Seconds(0.2f, true);
  GrainLiveFrame a;
  REQUIRE(r.feed.ReadFrame(&a));
  REQUIRE(a.tap_mode);
  uint32_t sa = 0;
  int slices = 0;
  for (const TapState& t : a.taps) {
    sa += t.serial;
    if (t.slice && t.level > 0.0f) {
      ++slices;
      CHECK(t.slice_length > 0);
      CHECK(t.slice_length <= a.max_delay);
      CHECK(t.slice_start >= 0);
      CHECK(t.slice_start < a.max_delay);
    }
  }
  CHECK(slices >= 1);
  r.Seconds(1.0f, true);
  GrainLiveFrame b;
  REQUIRE(r.feed.ReadFrame(&b));
  uint32_t sb = 0;
  for (const TapState& t : b.taps) sb += t.serial;
  CHECK(sb > sa);
}

TEST_CASE("Tap capture leaves Retours' audio bit-identical") {
  auto fp = [](bool attach) {
    Controls c;
    c.c.parameters.size = Knob(1.0f);
    c.c.density.base = 0.7f;
    c.c.mix.feedback = 0.5f;
    EngineHost host;
    GrainFeed feed;
    REQUIRE(host.Init(Processor::MODE_DELAY, true, 48000.0f));
    host.SetInputs(true, true);
    host.SetGrainFeed(&feed);
    feed.attached.store(attach);
    return Fingerprint(host, c, 48000 * 2);
  };
  CHECK(fp(true) == fp(false));
}

TEST_CASE("Scope ring: scale and live fill in delay mode") {
  DelayRun r;
  r.Seconds(0.5f);
  const auto a = r.feed.ReadScopeInfo();
  GrainLiveFrame f;
  REQUIRE(r.feed.ReadFrame(&f));
  CHECK(a.samples_per_entry == (f.max_delay + GrainFeed::kScopeEntries - 1) / GrainFeed::kScopeEntries);
  CHECK(a.line_rate == Catch::Approx(48000.0f));   // Bright: 48 kHz line
  r.Seconds(1.0f);
  const auto b = r.feed.ReadScopeInfo();
  const double per_second = double(b.head - a.head);
  CHECK(per_second == Catch::Approx(a.line_rate / a.samples_per_entry).epsilon(0.02));
  std::vector<int8_t> lo(64), hi(64);
  REQUIRE(r.feed.ReadScope(b.head, lo.data(), hi.data(), 64) == 64);
  int loud = 0;
  for (int i = 0; i < 64; ++i) if (hi[i] > 20 || lo[i] < -20) ++loud;
  CHECK(loud > 48);   // a 0.5-amplitude sine fills nearly every entry
}

TEST_CASE("Scope ring: backfill fills older entries after attach") {
  DelayRun r;
  r.feed.attached.store(false);
  r.Seconds(2.0f);                 // audio in the line, nothing captured
  r.feed.attached.store(true);
  r.Seconds(1.0f);                 // resync + backfill
  const auto s = r.feed.ReadScopeInfo();
  std::vector<int8_t> lo(GrainFeed::kScopeEntries), hi(GrainFeed::kScopeEntries);
  const int n = r.feed.ReadScope(s.head, lo.data(), hi.data(), GrainFeed::kScopeEntries);
  // Entries 1.5-2.0 s old (written before attach) must be backfilled.
  const int i0 = int(1.5f * s.line_rate / s.samples_per_entry), i1 = int(2.0f * s.line_rate / s.samples_per_entry);
  REQUIRE(i1 < n);
  int filled = 0;
  // An entry spans ~1 ms, a fifth of the sine's period: count either peak.
  for (int i = i0; i < i1; ++i) if (hi[size_t(i)] > 20 || lo[size_t(i)] < -20) ++filled;
  CHECK(filled > (i1 - i0) * 3 / 4);
}

TEST_CASE("Scope ring: untouched in granular mode") {
  Run r;   // Particules-style granular run, attached
  r.Seconds(1.0f);
  const auto s = r.feed.ReadScopeInfo();
  CHECK(s.head == 0u);             // never reset or published
}

TEST_CASE("Scope ring: scale change resets the ring") {
  DelayRun r;
  r.Seconds(0.5f);
  const auto a = r.feed.ReadScopeInfo();
  r.host.SetInputs(true, true);    // mono <-> stereo flips the line length
  r.Seconds(0.5f);
  const auto b = r.feed.ReadScopeInfo();
  CHECK(b.epoch != a.epoch);
  CHECK(b.samples_per_entry != a.samples_per_entry);
}
