// Brume's shimmer loop (host/brume_engine.h) and the calibration of its
// sustain gain (host/brume_controls.h).
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "host/brume_engine.h"

using robotboy_beads::BrumeControls;
using robotboy_beads::BrumeEngine;
using robotboy_beads::BrumeFrame;
using robotboy_beads::BrumeMode;
using robotboy_beads::DefaultBrumeControls;

namespace {

const float kPi = 3.14159265358979f;
const float kRate = 48000.0f;

// The brief's settings: defaults, Beads, Bright, DECAY 0.5, fully wet,
// INTERVAL +12.
BrumeControls ShimmerControls(float shimmer) {
  BrumeControls c = DefaultBrumeControls();
  c.decay = 0.5f;
  c.dry_wet = 1.0f;
  c.interval_semitones = 12.0f;
  c.shimmer = shimmer;
  return c;
}

uint64_t Fnv(uint64_t h, float x) {
  uint32_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  for (int i = 0; i < 4; ++i) {
    h ^= (bits >> (8 * i)) & 0xff;
    h *= 0x100000001b3ULL;
  }
  return h;
}

// 4 s of fixed noise through the engine at shimmer 0.
uint64_t ShimmerZeroHash(BrumeMode mode) {
  std::unique_ptr<BrumeEngine> e(new BrumeEngine);
  e->SetMode(mode);
  REQUIRE(e->Init(kRate));
  const BrumeControls c = ShimmerControls(0.0f);
  std::mt19937 rng(1234);
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
  uint64_t h = 0xcbf29ce484222325ULL;
  for (int i = 0; i < 4 * 48000; ++i) {
    e->SetControls(c);
    const float l = dist(rng);
    const float r = dist(rng);
    const BrumeFrame f = e->Process(l, r);
    h = Fnv(h, f.l);
    h = Fnv(h, f.r);
  }
  return h;
}

// Mono output of a 0.2 s noise burst then silence, `seconds` long.
std::vector<float> BurstResponse(float shimmer, float seconds) {
  std::unique_ptr<BrumeEngine> e(new BrumeEngine);
  REQUIRE(e->Init(kRate));
  const BrumeControls c = ShimmerControls(shimmer);
  std::mt19937 rng(99);
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
  const size_t burst = size_t(0.2f * kRate);
  const size_t n = size_t(seconds * kRate);
  std::vector<float> out(n);
  for (size_t i = 0; i < n; ++i) {
    e->SetControls(c);
    const float l = i < burst ? dist(rng) : 0.0f;
    const float r = i < burst ? dist(rng) : 0.0f;
    const BrumeFrame f = e->Process(l, r);
    out[i] = 0.5f * (f.l + f.r);
  }
  return out;
}

double Rms(const std::vector<float>& x, float from_s, float to_s) {
  const size_t a = size_t(from_s * kRate);
  const size_t b = size_t(to_s * kRate);
  double e = 0.0;
  for (size_t i = a; i < b; ++i) e += double(x[i]) * x[i];
  return std::sqrt(e / double(b - a));
}

double Db(double ratio) { return 20.0 * std::log10(ratio); }

float Peak(const std::vector<float>& x) {
  float m = 0.0f;
  for (float v : x) m = std::max(m, std::fabs(v));
  return m;
}

// Change in level from 2..4 s to 18..20 s after a burst, in dB.
double TailChangeDb(float shimmer) {
  const std::vector<float> out = BurstResponse(shimmer, 20.0f);
  return Db(Rms(out, 18.0f, 20.0f) / Rms(out, 2.0f, 4.0f));
}

// Power at `hz` over [a, b), Goertzel, Hann-windowed.
double PowerAt(const std::vector<float>& x, size_t a, size_t b, float hz) {
  const double w = 2.0 * M_PI * hz / kRate;
  const double coeff = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  const size_t n = b - a;
  for (size_t i = 0; i < n; ++i) {
    const double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(n));
    const double s = x[a + i] * win + coeff * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

}  // namespace

// Recorded at 69bb4e7 (Task 5's engine, before the shimmer loop existed).
TEST_CASE("shimmer 0 is identical to no shimmer", "[brume_shimmer]") {
  const uint64_t beads = ShimmerZeroHash(BrumeMode::kBeads);
  const uint64_t clouds = ShimmerZeroHash(BrumeMode::kClouds);
  INFO("hashes: beads " << std::hex << beads << " clouds " << clouds);
  REQUIRE(beads == 0xa1e5ef1d0c06f662ULL);
  REQUIRE(clouds == 0x455d221c2d88b5c0ULL);
}

TEST_CASE("0.94 decays", "[brume_shimmer]") {
  const double change = TailChangeDb(0.94f);
  INFO("tail change " << change << " dB");
  REQUIRE(change < -6.0);
}

TEST_CASE("0.96 sustains", "[brume_shimmer]") {
  const std::vector<float> out = BurstResponse(0.96f, 20.0f);
  const double change = Db(Rms(out, 18.0f, 20.0f) / Rms(out, 2.0f, 4.0f));
  INFO("tail change " << change << " dB, peak " << Peak(out));
  REQUIRE(change >= -3.0);
  REQUIRE(Peak(out) <= 2.0f);
}

TEST_CASE("shimmer raises pitch", "[brume_shimmer]") {
  double power[2];
  const float shimmers[2] = {0.0f, 0.6f};
  const size_t burst = size_t(0.5f * kRate);
  const size_t n = size_t(4.0f * kRate);
  for (int k = 0; k < 2; ++k) {
    std::unique_ptr<BrumeEngine> e(new BrumeEngine);
    REQUIRE(e->Init(kRate));
    const BrumeControls c = ShimmerControls(shimmers[k]);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
      e->SetControls(c);
      const float x = i < burst
          ? 0.5f * std::sin(2.0f * kPi * 440.0f * float(i) / kRate) : 0.0f;
      const BrumeFrame f = e->Process(x, x);
      out[i] = 0.5f * (f.l + f.r);
    }
    // The tail, from 0.5 s after the burst ends.
    power[k] = PowerAt(out, size_t(1.0f * kRate), size_t(3.0f * kRate), 880.0f);
  }
  const double gain = 10.0 * std::log10(power[1] / power[0]);
  INFO("880 Hz: shimmer " << power[1] << " none " << power[0]
       << " (" << gain << " dB)");
  REQUIRE(gain >= 10.0);
}

// Measures g1 (kShimmerSustainGain): the shimmer gain at which the tail of
// a burst, at the brief's settings, is as loud at 18..20 s as at 2..4 s.
// Bisects the gain through the SHIMMER knob below 0.95, so the constant
// must be above the result while this runs. Run by hand:
//   ./build/beads_engine_tests "[.calibrate]"
TEST_CASE("calibrate the shimmer sustain gain", "[.calibrate]") {
  const float k = robotboy_beads::kShimmerSustainGain;
  float lo = 0.0f;  // decays
  float hi = k;     // sustains
  REQUIRE(TailChangeDb(0.95f * 0.999f) >= 0.0);
  for (int i = 0; i < 16; ++i) {
    const float g = 0.5f * (lo + hi);
    const double change = TailChangeDb(0.95f * std::sqrt(g / k));
    std::printf("gain %.5f: %.2f dB\n", g, change);
    (change >= 0.0 ? hi : lo) = g;
  }
  std::printf("sustain gain between %.5f and %.5f\n", lo, hi);
}
