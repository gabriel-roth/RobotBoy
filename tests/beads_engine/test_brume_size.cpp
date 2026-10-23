// Brume's SIZE and PREDELAY (host/brume_engine.h, host/brume_controls.h).
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#include "host/brume_engine.h"
#include "stmlib/utils/random.h"

using robotboy_beads::BrumeControls;
using robotboy_beads::BrumeEngine;
using robotboy_beads::BrumeFrame;
using robotboy_beads::BrumeMode;
using robotboy_beads::BrumeQuality;
using robotboy_beads::DefaultBrumeControls;
using robotboy_beads::kPredelayDefault;
using robotboy_beads::kSizeDefault;

namespace {

uint64_t Fnv(uint64_t h, float x) {
  uint32_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  for (int i = 0; i < 4; ++i) {
    h ^= (bits >> (8 * i)) & 0xff;
    h *= 0x100000001b3ULL;
  }
  return h;
}

// Every control away from its default, so that the hash covers the
// modulated reads at 4x depth, the shimmer loop, SMEAR and wow.
BrumeControls BusyControls() {
  BrumeControls c = DefaultBrumeControls();
  c.filter = 0.3f;
  c.smear = 0.8f;
  c.wow = 0.3f;
  c.shimmer = 0.6f;
  c.decay = 0.8f;
  c.damp = 0.3f;
  c.diffuse = 0.8f;
  c.speed = 0.8f;
  c.depth = 1.0f;
  c.width = 0.7f;
  c.dry_wet = 0.9f;
  return c;
}

// 3 s of fixed noise through the engine.
uint64_t EngineHash(BrumeMode mode, BrumeQuality quality,
                    const BrumeControls& c) {
  std::unique_ptr<BrumeEngine> e(new BrumeEngine);
  e->SetMode(mode);
  e->SetQuality(quality);
  REQUIRE(e->Init(48000.0f));
  std::mt19937 rng(4321);
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
  uint64_t h = 0xcbf29ce484222325ULL;
  for (int i = 0; i < 3 * 48000; ++i) {
    e->SetControls(c);
    const float l = dist(rng);
    const float r = dist(rng);
    const BrumeFrame f = e->Process(l, r);
    h = Fnv(h, f.l);
    h = Fnv(h, f.r);
  }
  return h;
}

}  // namespace

// Recorded at e233450, before SIZE and PREDELAY existed. At their defaults
// (size 1, pre-delay 0) the output must not change by one bit. Wow's re-roll
// draws from stmlib's global Random, so the four runs start from its initial
// state, in the order they were recorded.
TEST_CASE("size 1 and pre-delay 0 leave the output unchanged",
          "[brume_size]") {
  stmlib::Random::Seed(0x21);
  BrumeControls busy = BusyControls();
  BrumeControls plain = DefaultBrumeControls();
  REQUIRE(busy.size == kSizeDefault);
  REQUIRE(busy.predelay == kPredelayDefault);
  busy.size = plain.size = 0.5f;
  busy.predelay = plain.predelay = 0.0f;
  CHECK(EngineHash(BrumeMode::kBeads, BrumeQuality::kCold, busy) ==
        0xe9c7508115ab2525ULL);
  CHECK(EngineHash(BrumeMode::kBeads, BrumeQuality::kScorched, plain) ==
        0xde3d96ade854ff50ULL);
  CHECK(EngineHash(BrumeMode::kClouds, BrumeQuality::kCold, busy) ==
        0xa5e2c6a8b1a7d3aaULL);
  CHECK(EngineHash(BrumeMode::kClouds, BrumeQuality::kScorched, plain) ==
        0xc908c95b5879ce9aULL);
}

namespace {

const float kRate = 48000.0f;
const float kPi = 3.14159265358979f;
const BrumeMode kModes[] = {BrumeMode::kBeads, BrumeMode::kClouds};

std::unique_ptr<BrumeEngine> MakeEngine(BrumeMode mode) {
  std::unique_ptr<BrumeEngine> e(new BrumeEngine);
  e->SetMode(mode);
  REQUIRE(e->Init(kRate));
  return e;
}

// Wet only, DECAY 0.7.
BrumeControls WetControls() {
  BrumeControls c = DefaultBrumeControls();
  c.dry_wet = 1.0f;
  c.decay = 0.7f;
  return c;
}

// The mono wet response to a 0.1 s noise burst, `seconds` long.
std::vector<float> BurstResponse(BrumeMode mode, const BrumeControls& c,
                                 float seconds) {
  auto e = MakeEngine(mode);
  std::mt19937 rng(77);
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
  const size_t burst = size_t(0.1f * kRate);
  std::vector<float> out(size_t(seconds * kRate));
  for (size_t i = 0; i < out.size(); ++i) {
    e->SetControls(c);
    const float x = i < burst ? dist(rng) : 0.0f;
    const BrumeFrame f = e->Process(x, x);
    out[i] = 0.5f * (f.l + f.r);
  }
  return out;
}

double Energy(const std::vector<float>& x, size_t a, size_t b) {
  double e = 0.0;
  for (size_t i = a; i < b && i < x.size(); ++i) e += double(x[i]) * x[i];
  return e;
}

// Decay rate of the tail: dB from 0.5..1.5 s to 1.5..2.5 s.
double TailDropDb(BrumeMode mode, float size_knob) {
  BrumeControls c = WetControls();
  c.size = size_knob;
  const std::vector<float> out = BurstResponse(mode, c, 2.5f);
  return 10.0 * std::log10(Energy(out, size_t(0.5f * kRate), size_t(1.5f * kRate)) /
                           Energy(out, size_t(1.5f * kRate), out.size()));
}

size_t Onset(const std::vector<float>& x, float threshold) {
  for (size_t i = 0; i < x.size(); ++i) {
    if (std::fabs(x[i]) > threshold) return i;
  }
  return x.size();
}

float MaxStep(const std::vector<float>& x, size_t a, size_t b) {
  float m = 0.0f;
  for (size_t i = std::max<size_t>(a, 1); i < b; ++i) {
    m = std::max(m, std::fabs(x[i] - x[i - 1]));
  }
  return m;
}

}  // namespace

TEST_CASE("SIZE lengthens and shortens the decay", "[brume_size]") {
  for (BrumeMode mode : kModes) {
    const double small = TailDropDb(mode, 0.0f);
    const double unity = TailDropDb(mode, 0.5f);
    const double large = TailDropDb(mode, 1.0f);
    INFO("mode " << int(mode) << " dB per second: 0.5x " << small << ", 1x "
         << unity << ", 2x " << large);
    CHECK(large < 0.7 * unity);
    CHECK(small > 1.4 * unity);
  }
}

// PredelaySeconds(sqrt(0.5)) = 250 ms. The engine starts at the panel
// value, so there is no glide here.
TEST_CASE("PREDELAY 250 ms delays the wet onset by 250 ms", "[brume_size]") {
  for (BrumeMode mode : kModes) {
    for (BrumeQuality quality :
         {BrumeQuality::kBright, BrumeQuality::kCold, BrumeQuality::kScorched}) {
      size_t onset[2];
      for (int k = 0; k < 2; ++k) {
        std::unique_ptr<BrumeEngine> e(new BrumeEngine);
        e->SetMode(mode);
        e->SetQuality(quality);
        REQUIRE(e->Init(kRate));
        BrumeControls c = WetControls();
        c.predelay = k ? std::sqrt(0.5f) : 0.0f;
        std::vector<float> out(size_t(0.6f * kRate));
        for (size_t i = 0; i < out.size(); ++i) {
          e->SetControls(c);
          const float x = i == 100 ? 0.5f : 0.0f;
          out[i] = e->Process(x, x).l;
        }
        onset[k] = Onset(out, 1e-4f);
        REQUIRE(onset[k] < out.size());
      }
      const double ms = 1000.0 * double(onset[1] - onset[0]) / kRate;
      INFO("mode " << int(mode) << " quality " << int(quality) << ": " << ms
           << " ms");
      CHECK(std::fabs(ms - 250.0) <= 1.0);
    }
  }
}

// A 220 Hz sine into the wet; SIZE or PREDELAY jumps end to end. Every step
// stays within 2.5x the larger of the two steady states' largest steps.
// (Not within 1x: a delay shrinking under glide raises the pitch, by up to
// an octave for PREDELAY's full sweep, and a shrinking loop compounds it;
// that peaks near 1.9x here. An unglided SIZE jump steps 17x to 37x.)
TEST_CASE("SIZE and PREDELAY changes glide without steps", "[brume_size]") {
  for (BrumeMode mode : kModes) {
    for (int which = 0; which < 2; ++which) {
      for (float from : {0.0f, 1.0f}) {
        auto e = MakeEngine(mode);
        BrumeControls c = WetControls();
        float& knob = which ? c.predelay : c.size;
        knob = from;
        const size_t change = size_t(3.0f * kRate);
        std::vector<float> out(size_t(6.0f * kRate));
        for (size_t i = 0; i < out.size(); ++i) {
          if (i == change) knob = 1.0f - from;
          e->SetControls(c);
          const float x = 0.5f * std::sin(2.0f * kPi * 220.0f * float(i) / kRate);
          out[i] = e->Process(x, x).l;
        }
        const float before = MaxStep(out, size_t(1.0f * kRate), change);
        const float after = MaxStep(out, size_t(5.0f * kRate), out.size());
        const float across = MaxStep(out, change, out.size());
        INFO("mode " << int(mode) << (which ? " predelay" : " size") << " from "
             << from << ": before " << before << " after " << after
             << " across " << across);
        REQUIRE(before > 0.0f);
        REQUIRE(after > 0.0f);
        CHECK(across <= 2.5f * std::max(before, after));
      }
    }
  }
}

// PREDELAY jumps end to end; an impulse 100 ms later is read when the
// gliding delay d(t) catches it: t - d(t) = 0.1 s, with d a one-pole of
// 0.5 s. Up from 0, d = 0.5 (1 - e^(-2t)), t = 353.4 ms; down from 500 ms,
// d = 0.5 e^(-2t), t = 348.9 ms. So the impulse is heard 253.4 ms and
// 248.9 ms late, not 500 ms or 0 as with a jump.
TEST_CASE("PREDELAY glides", "[brume_size]") {
  for (BrumeMode mode : kModes) {
    for (float from : {0.0f, 1.0f}) {
      size_t onset[2];
      for (int k = 0; k < 2; ++k) {
        auto e = MakeEngine(mode);
        BrumeControls c = WetControls();
        c.predelay = from;
        const size_t change = size_t(0.2f * kRate);
        const size_t impulse = change + size_t(0.1f * kRate);
        std::vector<float> out(size_t(1.2f * kRate));
        for (size_t i = 0; i < out.size(); ++i) {
          // Run 0 stays put, for the reference onset.
          if (k && i == change) c.predelay = 1.0f - from;
          e->SetControls(c);
          const float x = i == impulse ? 0.5f : 0.0f;
          out[i] = e->Process(x, x).l;
        }
        onset[k] = Onset(out, 1e-4f);
        REQUIRE(onset[k] < out.size());
      }
      // The pre-delay heard, relative to the setting before the change.
      const double ms = 1000.0 * from * 0.5 +
          1000.0 * (double(onset[1]) - double(onset[0])) / kRate;
      INFO("mode " << int(mode) << " from " << from << ": " << ms << " ms");
      CHECK(std::fabs(ms - (from == 0.0f ? 253.4 : 248.9)) < 2.0);
    }
  }
}

// The shimmer return joins after the pre-delay, so the loop period does not
// include it: with PREDELAY at 400 ms the shimmer's contribution (the
// difference from the same run without shimmer) starts within 100 ms of the
// wet itself, not 400 ms later.
TEST_CASE("the shimmer loop bypasses the pre-delay", "[brume_size]") {
  for (BrumeMode mode : kModes) {
    std::vector<float> out[2];
    for (int k = 0; k < 2; ++k) {
      auto e = MakeEngine(mode);
      BrumeControls c = WetControls();
      c.depth = 0.0f;
      c.predelay = std::sqrt(0.8f);  // 400 ms
      c.shimmer = k ? 0.8f : 0.0f;
      out[k].resize(size_t(1.5f * kRate));
      for (size_t i = 0; i < out[k].size(); ++i) {
        e->SetControls(c);
        const float x = i < 480 ? 0.5f * std::sin(2.0f * kPi * 440.0f * float(i) / kRate) : 0.0f;
        out[k][i] = e->Process(x, x).l;
      }
    }
    std::vector<float> diff(out[0].size());
    for (size_t i = 0; i < diff.size(); ++i) diff[i] = out[1][i] - out[0][i];
    const size_t wet = Onset(out[0], 1e-4f);
    const size_t shimmer = Onset(diff, 1e-4f);
    INFO("mode " << int(mode) << ": wet at " << wet << ", shimmer at "
         << shimmer);
    CHECK(wet > size_t(0.4f * kRate));
    CHECK(shimmer >= wet);
    CHECK(shimmer < wet + size_t(0.1f * kRate));
  }
}

// Clear empties the pre-delay line too: nothing comes out after the fade,
// including what was still on its way through 300 ms of pre-delay.
TEST_CASE("clear empties the pre-delay", "[brume_size]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(mode);
    BrumeControls c = WetControls();
    c.predelay = std::sqrt(0.6f);  // 300 ms
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (int i = 0; i < 48000; ++i) {
      e->SetControls(c);
      const float x = dist(rng);
      e->Process(x, x);
    }
    e->RequestClear();
    std::vector<float> out(48000);
    for (float& y : out) {
      e->SetControls(c);
      y = e->Process(0.0f, 0.0f).l;
    }
    INFO("mode " << int(mode));
    CHECK(Energy(out, 720, out.size()) == 0.0);
  }
}

TEST_CASE("non-finite SIZE and PREDELAY are contained", "[brume_size]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(mode);
    BrumeControls c = WetControls();
    c.size = 0.8f;
    c.predelay = 0.3f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    std::mt19937 rng(6);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    double tail = 0.0;
    for (int i = 0; i < 96000; ++i) {
      BrumeControls bad = c;
      if (i >= 24000 && i < 24010) {
        bad.size = i % 2 ? nan : inf;
        bad.predelay = i % 2 ? -inf : nan;
      }
      e->SetControls(bad);
      const float x = dist(rng);
      const BrumeFrame f = e->Process(x, x);
      REQUIRE(std::isfinite(f.l));
      REQUIRE(std::isfinite(f.r));
      if (i >= 96000 - 4800) tail += double(f.l) * f.l;
    }
    CHECK(tail > 0.0);
  }
}
