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
#include "clouds/dsp/fx/diffuser.h"
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
  std::vector<uint16_t> memory(65536);
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
  // The patched engine is 65536 words (room for Brume's 2x size and 4x
  // depth); the stock layout inside it must still match upstream's 16384.
  std::vector<uint16_t> memory(65536), ref_memory(16384);
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
  std::vector<uint16_t> memory = std::vector<uint16_t>(65536);
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
  std::vector<uint16_t> memory = std::vector<uint16_t>(65536);
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
  std::vector<uint16_t> memory(65536);
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

namespace {

// Seeded noise through a rig, left channel out.
template <typename Rig, typename Configure>
std::vector<float> RunNoise(Configure configure, float seconds,
                            uint32_t seed = 21) {
  auto rig = std::make_unique<Rig>();
  configure(rig->reverb);
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  std::vector<float> out;
  const int n = static_cast<int>(seconds * Rig::kRate);
  for (int i = 0; i < n; ++i) out.push_back(rig->Process(noise(rng)));
  return out;
}

double Rms(const std::vector<float>& x) {
  double sum = 0.0;
  for (float v : x) sum += double(v) * v;
  return std::sqrt(sum / x.size());
}

double RmsDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double sum = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    const double d = double(a[i]) - b[i];
    sum += d * d;
  }
  return std::sqrt(sum / a.size());
}

}  // namespace

// Brume always runs the custom path, whose delay layout has room for 4x
// excursions. At the default depth it must sound like the stock path: the
// enlarged lines must keep every tap the signal hears where it was. Only
// rounding may differ (variables in place of constants), which the 12-bit
// Clouds memory turns into whole LSBs (1/4096): about -59 dB here. A moved
// tap, such as ap1's tail, differs by as much as the signal itself.
TEST_CASE("Forced custom path at defaults tracks the stock path",
          "[brume_vendor]") {
  auto stock = [](auto&) {};
  auto forced = [](auto& r) { r.set_force_custom(true); };
  SECTION("Beads") {
    auto a = RunNoise<BeadsRig>(stock, 4.0f);
    auto b = RunNoise<BeadsRig>(forced, 4.0f);
    REQUIRE(Rms(a) > 0.05);
    CHECK(RmsDiff(a, b) < 2e-3 * Rms(a));
  }
  SECTION("Clouds") {
    auto a = RunNoise<CloudsRig>(stock, 4.0f);
    auto b = RunNoise<CloudsRig>(forced, 4.0f);
    REQUIRE(Rms(a) > 0.05);
    CHECK(RmsDiff(a, b) < 2e-3 * Rms(a));
  }
}

// The modulated reads sit at offset + amplitude * depth * lfo (+1 for the
// interpolation). The vendored reverbs static_assert that this fits inside
// each line's reserve for depth 4 and an LFO in [0, 1], and set_mod_depth
// clamps to 4. Here: the LFOs really stay in [0, 1] at 16x for 10 s of
// noise at depth 4, the output stays finite, and depths beyond 4 clamp.
template <typename Rig>
void CheckFullExcursion() {
  Rig r;
  r.reverb.set_force_custom(true);
  r.reverb.set_mod_depth(4.0f);
  r.SetRate(16.0f);
  std::mt19937 rng(17);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  const int n = static_cast<int>(10.0f * Rig::kRate);
  float lo = 1.0f, hi = 0.0f, peak = 0.0f;
  for (int i = 0; i < n; ++i) {
    const float y = r.Process(noise(rng));
    if (!std::isfinite(y)) FAIL("non-finite output at sample " << i);
    peak = std::max(peak, std::fabs(y));
    for (int k = 0; k < 2; ++k) {
      lo = std::min(lo, r.Lfo(k));
      hi = std::max(hi, r.Lfo(k));
    }
  }
  CHECK(lo >= -1e-3f);
  CHECK(hi <= 1.0f + 1e-3f);
  CHECK(lo < 0.01f);  // both ends of the sweep were visited
  CHECK(hi > 0.99f);
  CHECK(peak > 0.05f);
  CHECK(peak < 4.0f);

  auto at = [](float depth) {
    return [depth](auto& rv) {
      rv.set_force_custom(true);
      rv.set_mod_depth(depth);
    };
  };
  CHECK(RunNoise<Rig>(at(100.0f), 0.5f) == RunNoise<Rig>(at(4.0f), 0.5f));
  CHECK(RunNoise<Rig>(at(4.0f), 0.5f) != RunNoise<Rig>(at(1.0f), 0.5f));
}

TEST_CASE("Depth 4 at 16x stays inside the delay reserves", "[brume_vendor]") {
  SECTION("Beads") { CheckFullExcursion<BeadsRig>(); }
  SECTION("Clouds") { CheckFullExcursion<CloudsRig>(); }
}

TEST_CASE("Clouds diffuser coefficient", "[brume_vendor]") {
  auto run = [](bool set, float diffusion) {
    std::vector<float> memory(2048);
    clouds::Diffuser d;
    d.Init(memory.data());
    d.set_amount(1.0f);
    if (set) d.set_diffusion(diffusion);
    std::mt19937 rng(4);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    std::vector<float> out;
    for (int b = 0; b < 500; ++b) {
      clouds::FloatFrame f[32];
      for (auto& x : f) x = {noise(rng), noise(rng)};
      d.Process(f, 32);
      for (auto& x : f) out.push_back(x.l);
    }
    return out;
  };
  CHECK(run(true, 0.625f) == run(false, 0.0f));
  CHECK(run(true, 0.8f) != run(false, 0.0f));
}

namespace {

uint64_t HashOf(const std::vector<float>& x) {
  uint64_t h = 14695981039346656037ull;
  for (float v : x) h = Fnv(h, v);
  return h;
}

}  // namespace

// Recorded from the custom paths before SIZE existed (e233450): at size 1
// every scaled offset is whole, so the interpolated reads return exactly what
// the tail reads did, and moving ap1's smear write (with its tail) changes
// nothing the signal hears.
TEST_CASE("Custom path at size 1 is unchanged", "[brume_vendor]") {
  auto forced = [](float depth, bool set_size) {
    return [depth, set_size](auto& r) {
      r.set_force_custom(true);
      r.set_mod_depth(depth);
      r.set_lfo_rate(3.0f, 48000.0f);
      if (set_size) r.set_size(1.0f);
    };
  };
  for (bool set_size : {false, true}) {
    CHECK(HashOf(RunNoise<BeadsRig>(forced(1.0f, set_size), 3.0f)) ==
          0x71f66218fb5385f0ull);
    CHECK(HashOf(RunNoise<CloudsRig>(forced(1.0f, set_size), 3.0f)) ==
          0x2481d7050fea14fdull);
    CHECK(HashOf(RunNoise<BeadsRig>(forced(4.0f, set_size), 3.0f)) ==
          0xf9f348f5614a572eull);
    CHECK(HashOf(RunNoise<CloudsRig>(forced(4.0f, set_size), 3.0f)) ==
          0x41945afa8f333f78ull);
  }
}

namespace {

// How fast the tail of a 0.1 s noise burst decays, forced custom path at
// `size` with the decay setting fixed: dB from 0.5..1.5 s to 1.5..2.5 s.
template <typename Rig>
double TailDropDb(float size) {
  Rig r;
  r.reverb.set_force_custom(true);
  r.reverb.set_size(size);
  std::mt19937 rng(8);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  const int burst = static_cast<int>(0.1f * Rig::kRate);
  double e[2] = {0.0, 0.0};
  for (int i = 0; i < static_cast<int>(2.5f * Rig::kRate); ++i) {
    const float y = r.Process(i < burst ? noise(rng) : 0.0f);
    const float t = float(i) / Rig::kRate;
    if (t >= 0.5f) e[t < 1.5f ? 0 : 1] += double(y) * y;
  }
  return 10.0 * std::log10(e[0] / e[1]);
}

template <typename Rig>
void CheckSizeScalesDecay() {
  const double small = TailDropDb<Rig>(0.5f);
  const double unity = TailDropDb<Rig>(1.0f);
  const double large = TailDropDb<Rig>(2.0f);
  INFO("dB per second: 0.5x " << small << ", 1x " << unity << ", 2x "
       << large);
  // Each pass round the loop loses the same fraction, so the decay rate in
  // time goes as 1 / size.
  CHECK(large < 0.7 * unity);
  CHECK(small > 1.4 * unity);
}

}  // namespace

TEST_CASE("Size lengthens and shortens the decay", "[brume_vendor]") {
  SECTION("Beads") { CheckSizeScalesDecay<BeadsRig>(); }
  SECTION("Clouds") { CheckSizeScalesDecay<CloudsRig>(); }
}

namespace {

// The size clamps to 0.5 .. 2, and both ends at depth 4 and 16x stay
// finite (the reserves are static_asserted for 2x size with 4x depth).
template <typename Rig>
void CheckSizeEnds() {
  auto at = [](float size) {
    return [size](auto& rv) {
      rv.set_force_custom(true);
      rv.set_mod_depth(4.0f);
      rv.set_lfo_rate(16.0f, Rig::kRate);
      rv.set_size(size);
    };
  };
  const auto large = RunNoise<Rig>(at(2.0f), 3.0f);
  const auto small = RunNoise<Rig>(at(0.5f), 3.0f);
  for (float y : large) REQUIRE(std::isfinite(y));
  for (float y : small) REQUIRE(std::isfinite(y));
  CHECK(Rms(large) > 0.05);
  CHECK(Rms(small) > 0.05);
  CHECK(RunNoise<Rig>(at(100.0f), 0.5f) == RunNoise<Rig>(at(2.0f), 0.5f));
  CHECK(RunNoise<Rig>(at(0.0f), 0.5f) == RunNoise<Rig>(at(0.5f), 0.5f));
}

}  // namespace

TEST_CASE("Size ends at depth 4 stay inside the delay reserves",
          "[brume_vendor]") {
  SECTION("Beads") { CheckSizeEnds<BeadsRig>(); }
  SECTION("Clouds") { CheckSizeEnds<CloudsRig>(); }
}

namespace {

// A size change glides linearly across the next block: one block of 32
// frames at a new size of 2 matches 32 one-frame blocks stepping through
// 1 + k / 32 (exact in binary, as is the glide's running sum).
template <typename Rig, typename Frame>
void CheckSizeGlidesAcrossBlock() {
  Rig a, b;
  a.reverb.set_force_custom(true);
  b.reverb.set_force_custom(true);
  std::mt19937 rng(9);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  for (int i = 0; i < 4800; ++i) {
    const float x = noise(rng);
    REQUIRE(a.Process(x) == b.Process(x));
  }
  Frame block[32];
  float expected[32];
  for (int i = 0; i < 32; ++i) {
    const float x = noise(rng);
    block[i].l = block[i].r = x;
    b.reverb.set_size(1.0f + float(i + 1) / 32.0f);
    expected[i] = b.Process(x);
  }
  a.reverb.set_size(2.0f);
  a.reverb.Process(block, 32);
  for (int i = 0; i < 32; ++i) {
    INFO("sample " << i);
    CHECK(block[i].l == expected[i]);
  }
}

}  // namespace

TEST_CASE("Size glides across a block", "[brume_vendor]") {
  SECTION("Beads") { CheckSizeGlidesAcrossBlock<BeadsRig, FloatFrame>(); }
  SECTION("Clouds") {
    CheckSizeGlidesAcrossBlock<CloudsRig, clouds::FloatFrame>();
  }
}
