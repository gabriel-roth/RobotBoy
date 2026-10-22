// Brume's vendor patches: the Beads reverb's new setters at their defaults
// leave its output bit-identical, and the patched Clouds classes still match
// pristine upstream (clouds_ref) at default settings.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "beads/dsp/frame.h"
#include "beads/dsp/fx/reverb.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/reverb.h"
#include "clouds_ref/dsp/frame.h"
#include "clouds_ref/dsp/fx/reverb.h"

using beads::FloatFrame;

namespace {

struct Medium {
  const char* name;
  float sample_rate;
  float lp;
  uint64_t golden;
};

// Golden hashes recorded from the tree before any Brume patch.
const Medium kMedia[] = {
    {"48k", 48000.0f, 0.6f, 0x98e0ad1e268d77b3ull},
    {"32k", 32000.0f, 0.75f, 0xdca8711d7adcd994ull},
    {"24k caveman", 24000.0f, 0.4f, 0xa878fb486ae5707cull},
};

uint64_t Fnv(uint64_t h, float x) {
  uint32_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  for (int i = 0; i < 4; ++i) {
    h ^= (bits >> (8 * i)) & 0xff;
    h *= 1099511628211ull;
  }
  return h;
}

// 4 s of seeded noise through beads::Reverb (mix 0.7, time 0.8) in blocks of
// 24; with `setters` the Brume setters are called at their defaults each block.
uint64_t HashBeads(const Medium& m, bool setters) {
  std::vector<uint16_t> memory(32768);
  beads::Reverb reverb;
  reverb.Init(memory.data(), 48000.0f);
  reverb.Reset(m.sample_rate);
  std::mt19937 rng(99);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  uint64_t h = 14695981039346656037ull;
  const int blocks = static_cast<int>(4.0f * m.sample_rate / 24.0f);
  for (int b = 0; b < blocks; ++b) {
    FloatFrame frames[24];
    for (FloatFrame& f : frames) {
      f.l = noise(rng);
      f.r = noise(rng);
    }
    reverb.set_mix(0.7f);
    reverb.set_time(0.8f);
    reverb.set_lp(m.lp);
    if (setters) {
      reverb.set_diffusion(0.625f);
      reverb.set_lfo_rate(1.0f, m.sample_rate);
      reverb.set_mod_depth(1.0f);
    }
    reverb.Process(frames, 24);
    for (const FloatFrame& f : frames) h = Fnv(Fnv(h, f.l), f.r);
  }
  return h;
}

// Placement-new into zeroed storage: upstream Clouds leaves the reverb's two
// filter states uninitialized, and the reference must not depend on that.
template <typename T>
struct Zeroed {
  alignas(T) unsigned char bytes[sizeof(T)];
  T* get() {
    std::memset(bytes, 0, sizeof(bytes));
    return new (bytes) T();
  }
};

void SetUpClouds(clouds::Reverb* r) {
  r->set_amount(0.54f);
  r->set_input_gain(0.2f);
  r->set_time(0.8f);
  r->set_diffusion(0.7f);
  r->set_lp(0.8f);
}

void SetUpClouds(clouds_ref::Reverb* r) {
  r->set_amount(0.54f);
  r->set_input_gain(0.2f);
  r->set_time(0.8f);
  r->set_diffusion(0.7f);
  r->set_lp(0.8f);
}

}  // namespace

TEST_CASE("Beads reverb unchanged by Brume setters", "[brume_vendor]") {
  for (const Medium& m : kMedia) {
    CHECK(HashBeads(m, false) == m.golden);
  }
}

TEST_CASE("Beads reverb setters at defaults are bit-identical",
          "[brume_vendor]") {
  for (const Medium& m : kMedia) {
    CHECK(HashBeads(m, true) == m.golden);
  }
}

TEST_CASE("Clouds reverb with default rate matches upstream",
          "[brume_vendor]") {
  std::vector<uint16_t> memory(16384), ref_memory(16384);
  Zeroed<clouds::Reverb> storage;
  Zeroed<clouds_ref::Reverb> ref_storage;
  clouds::Reverb* reverb = storage.get();
  clouds_ref::Reverb* ref = ref_storage.get();
  reverb->Init(memory.data());
  reverb->Clear();
  ref->Init(ref_memory.data());
  reverb->set_lfo_rate(1.0f, 32000.0f);
  reverb->set_mod_depth(1.0f);
  SetUpClouds(reverb);
  SetUpClouds(ref);

  std::mt19937 rng(7);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  const int blocks = 4 * 32000 / 32;
  for (int b = 0; b < blocks; ++b) {
    clouds::FloatFrame a[32];
    clouds_ref::FloatFrame r[32];
    for (int i = 0; i < 32; ++i) {
      a[i].l = r[i].l = noise(rng);
      a[i].r = r[i].r = noise(rng);
    }
    reverb->Process(a, 32);
    ref->Process(r, 32);
    for (int i = 0; i < 32; ++i) {
      if (a[i].l != r[i].l || a[i].r != r[i].r) {
        FAIL("output differs at sample " << b * 32 + i);
      }
    }
  }
}

namespace {

struct BeadsRig {
  static constexpr float kRate = 48000.0f;
  std::vector<uint16_t> memory = std::vector<uint16_t>(32768);
  beads::Reverb reverb;
  BeadsRig() {
    reverb.Init(memory.data(), kRate);
    reverb.Reset(kRate);
    reverb.set_mix(1.0f);
    reverb.set_time(0.8f);
    reverb.set_lp(0.6f);
  }
  float Process(float x) {
    FloatFrame f = {x, x};
    reverb.Process(&f, 1);
    return f.l;
  }
  void SetRate(float multiplier) { reverb.set_lfo_rate(multiplier, kRate); }
  float Lfo(int i) const { return reverb.lfo_value(i); }
  void Silence(int n) {
    for (int i = 0; i < n; ++i) Process(0.0f);
  }
};

struct CloudsRig {
  static constexpr float kRate = 32000.0f;
  std::vector<uint16_t> memory = std::vector<uint16_t>(16384);
  clouds::Reverb reverb;
  CloudsRig() {
    reverb.Init(memory.data());
    reverb.Clear();
    reverb.set_amount(1.0f);
    reverb.set_input_gain(0.2f);
    reverb.set_time(0.8f);
    reverb.set_diffusion(0.7f);
    reverb.set_lp(0.8f);
  }
  float Process(float x) {
    clouds::FloatFrame f = {x, x};
    reverb.Process(&f, 1);
    return f.l;
  }
  void SetRate(float multiplier) { reverb.set_lfo_rate(multiplier, kRate); }
  float Lfo(int i) const { return reverb.lfo_value(i); }
  void Silence(int n) {
    for (int i = 0; i < n; ++i) Process(0.0f);
  }
};

// Two identical tanks fed a 220 Hz sine, one with its LFO rate doubled after
// one second. A rate change that keeps the LFO phase leaves the two
// outputs close for the next 64 samples (two LFO ticks); one that restarts
// the oscillator makes the LFO jump and the outputs part at once. The changed
// tank must also show no step beyond 4x the earlier maximum.
template <typename Rig>
void CheckRateChangeKeepsPhase() {
  Rig a, b;
  a.SetRate(1.0f);
  b.SetRate(1.0f);
  float phase = 0.0f;
  const float w = 2.0f * 3.14159265f * 220.0f / Rig::kRate;
  float prev = 0.0f, max_step = 0.0f, peak = 0.0f;
  auto next = [&]() {
    const float x = 0.5f * std::sin(phase);
    phase += w;
    return x;
  };
  const int warmup = static_cast<int>(Rig::kRate);  // LFO 1 at half a cycle
  for (int i = 0; i < warmup; ++i) {
    const float x = next();
    const float ya = a.Process(x);
    REQUIRE(ya == b.Process(x));
    if (i >= warmup - 1000) {
      max_step = std::max(max_step, std::fabs(ya - prev));
      peak = std::max(peak, std::fabs(ya));
    }
    prev = ya;
  }
  REQUIRE(max_step > 0.0f);
  b.SetRate(2.0f);
  float max_diff = 0.0f, max_step_after = 0.0f;
  for (int i = 0; i < 64; ++i) {
    const float x = next();
    const float ya = a.Process(x);
    const float yb = b.Process(x);
    max_diff = std::max(max_diff, std::fabs(ya - yb));
    max_step_after = std::max(max_step_after, std::fabs(yb - prev));
    prev = yb;
  }
  CHECK(max_diff < 0.05f * peak);
  CHECK(max_step_after <= 4.0f * max_step);
}

// Both LFOs must stay in [0, 1] (they offset delay reads) however the rate
// moves: single changes at several phases, then a 10 s random walk.
template <typename Rig>
void CheckLfoBounded() {
  const float lo = -1e-3f, hi = 1.0f + 1e-3f;
  const int period = static_cast<int>(2.0f * Rig::kRate);  // 2 s: a full cycle
  for (float factor : {0.5f, 2.0f, 4.0f}) {
    for (int at : {0, 2000, 8000, 16000, 24000, 31000}) {
      Rig r;
      r.SetRate(1.0f);
      r.Silence(at);
      r.SetRate(factor);
      for (int i = 0; i < period; ++i) {
        r.Process(0.0f);
        for (int k = 0; k < 2; ++k) {
          const float v = r.Lfo(k);
          if (v < lo || v > hi) {
            FAIL("LFO " << k << " = " << v << " (x" << factor << " at " << at
                 << ")");
          }
        }
      }
    }
  }
  Rig r;
  std::mt19937 rng(11);
  std::normal_distribution<float> step(0.0f, 0.15f);
  float log_rate = 0.0f;
  const int blocks = static_cast<int>(10.0f * Rig::kRate / 32.0f);
  for (int b = 0; b < blocks; ++b) {
    log_rate = std::min(std::log(4.0f), std::max(std::log(0.25f), log_rate + step(rng)));
    r.SetRate(std::exp(log_rate));
    r.Silence(32);
    for (int k = 0; k < 2; ++k) {
      const float v = r.Lfo(k);
      if (v < lo || v > hi) FAIL("random walk: LFO " << k << " = " << v);
    }
  }
}

}  // namespace

TEST_CASE("LFO stays within [0, 1] when the rate moves", "[brume_vendor]") {
  SECTION("Beads") { CheckLfoBounded<BeadsRig>(); }
  SECTION("Clouds") { CheckLfoBounded<CloudsRig>(); }
}

TEST_CASE("Non-default depth and diffusion change the output",
          "[brume_vendor]") {
  auto run = [](auto make_rig, auto configure) {
    auto rig = make_rig();
    configure(rig->reverb);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    std::vector<float> out;
    for (int i = 0; i < 20000; ++i) out.push_back(rig->Process(noise(rng)));
    return out;
  };
  auto beads = [] { return std::make_unique<BeadsRig>(); };
  auto clouds_rig = [] { return std::make_unique<CloudsRig>(); };
  auto none = [](auto&) {};
  SECTION("Beads") {
    auto base = run(beads, none);
    CHECK(run(beads, [](auto& r) { r.set_mod_depth(0.0f); }) != base);
    CHECK(run(beads, [](auto& r) { r.set_diffusion(0.5f); }) != base);
    CHECK(run(beads, [](auto& r) { r.set_mod_depth(1.0f); r.set_diffusion(0.625f); }) == base);
  }
  SECTION("Clouds") {
    auto base = run(clouds_rig, [](auto& r) { r.set_diffusion(0.7f); });
    CHECK(run(clouds_rig, [](auto& r) { r.set_mod_depth(0.0f); }) != base);
    CHECK(run(clouds_rig, [](auto& r) { r.set_diffusion(0.5f); }) != base);
  }
}

TEST_CASE("LFO rate change keeps phase", "[brume_vendor]") {
  SECTION("Beads") { CheckRateChangeKeepsPhase<BeadsRig>(); }
  SECTION("Clouds") { CheckRateChangeKeepsPhase<CloudsRig>(); }
}

TEST_CASE("Clouds Clear empties the tank", "[brume_vendor]") {
  std::vector<uint16_t> memory(16384);
  Zeroed<clouds::Reverb> storage;
  clouds::Reverb* reverb = storage.get();
  reverb->Init(memory.data());
  SetUpClouds(reverb);
  reverb->set_amount(1.0f);
  std::mt19937 rng(3);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  for (int b = 0; b < 1000; ++b) {
    clouds::FloatFrame f[32];
    for (auto& x : f) x = {noise(rng), noise(rng)};
    reverb->Process(f, 32);
  }
  reverb->Clear();
  for (int b = 0; b < 1000; ++b) {
    clouds::FloatFrame f[32] = {};
    reverb->Process(f, 32);
    for (auto& x : f) {
      REQUIRE(x.l == 0.0f);
      REQUIRE(x.r == 0.0f);
    }
  }
}
