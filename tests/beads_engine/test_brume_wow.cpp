// Brume's rate-independent wow and flutter (host/wow_flutter.h).
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "host/wow_flutter.h"
#include "stmlib/utils/random.h"

using robotboy_beads::WowFlutter;

namespace {

const float kPi = 3.14159265358979f;

std::vector<float> Noise(size_t n, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  std::vector<float> v(n);
  for (float& x : v) x = dist(rng);
  return v;
}

// Power above 16 kHz (at a 48 kHz rate) of a 4096-sample segment, by DFT.
double HighPower(const std::vector<float>& x, size_t start) {
  const size_t n = 4096;
  double total = 0.0;
  for (size_t k = n / 3; k < n / 2; ++k) {  // 16 kHz = bin n/3
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
      const double ph = 2.0 * kPi * double(k) * double(i) / double(n);
      re += w * x[start + i] * std::cos(ph);
      im -= w * x[start + i] * std::sin(ph);
    }
    total += re * re + im * im;
  }
  return total;
}

// Peak delay in ms of an impulse train passed at `amount`.
double PeakDelayMs(float rate, float amount = 1.0f) {
  stmlib::Random::Seed(12345);
  WowFlutter w;
  w.Init(rate);
  const float scale = std::max(1.0f, amount);
  const size_t spacing = size_t(rate * 0.01f * scale);
  const size_t window = size_t(rate * 0.008f * scale);
  const size_t total = size_t(rate * 10.0f);
  std::vector<float> out(total);
  for (size_t i = 0; i < total; ++i) {
    float l = (i % spacing == 0) ? 1.0f : 0.0f;
    float r = l;
    w.Process(&l, &r, amount);
    out[i] = l;
  }
  double peak = 0.0;
  for (size_t pos = size_t(rate * 0.1f); pos + window < total; pos += spacing) {
    pos = (pos / spacing) * spacing;
    double sum = 0.0, moment = 0.0;
    for (size_t j = 0; j < window; ++j) {
      sum += out[pos + j];
      moment += out[pos + j] * double(j);
    }
    if (sum > 0.1) peak = std::max(peak, moment / sum);
  }
  return peak / rate * 1000.0;
}

}  // namespace

TEST_CASE("amount 0 is a bit-exact bypass", "[brume_wow]") {
  WowFlutter w;
  w.Init(48000.0f);
  std::vector<float> in = Noise(48000, 1);
  for (size_t i = 0; i < in.size(); ++i) {
    float l = in[i], r = -in[i];
    w.Process(&l, &r, 0.0f);
    REQUIRE(l == in[i]);
    REQUIRE(r == -in[i]);
  }
}

TEST_CASE("crossing zero does not click", "[brume_wow]") {
  WowFlutter w;
  w.Init(48000.0f);
  const size_t n = 24000;
  float prev_in = 0.0f, prev_out = 0.0f, max_in = 0.0f, max_out = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const float x = std::sin(2.0f * kPi * 440.0f * float(i) / 48000.0f);
    const float t = float(i) / float(n);
    const float amount = 0.3f * (t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t);
    float l = x, r = x;
    w.Process(&l, &r, amount);
    if (i > 0) {
      max_in = std::max(max_in, std::fabs(x - prev_in));
      max_out = std::max(max_out, std::fabs(l - prev_out));
    }
    prev_in = x;
    prev_out = l;
  }
  REQUIRE(max_out <= 1.2f * max_in);
}

TEST_CASE("delay depth is rate independent", "[brume_wow]") {
  const double d48 = PeakDelayMs(48000.0f);
  const double d96 = PeakDelayMs(96000.0f);
  const double limit_ms = (1.0 + 105.0 + 17.0) / 24000.0 * 1000.0;
  REQUIRE(d48 > 1.0);
  REQUIRE(d48 <= limit_ms);
  REQUIRE(d96 <= limit_ms);
  REQUIRE(std::fabs(d48 - d96) <= 0.05 * d48);
}

// Amount 3, Brume's maximum: three times the delay, at any host rate up to
// 768 kHz, where it needs most of the ring.
TEST_CASE("delay depth at amount 3", "[brume_wow]") {
  const double d1 = PeakDelayMs(48000.0f, 1.0f);
  const double limit_ms = 3.0 * (1.0 + 105.0 + 17.0) / 24000.0 * 1000.0;
  for (float rate : {48000.0f, 96000.0f, 768000.0f}) {
    INFO("rate " << rate);
    const double d3 = PeakDelayMs(rate, 3.0f);
    REQUIRE(d3 <= limit_ms);
    REQUIRE(std::fabs(d3 - 3.0 * d1) <= 0.05 * 3.0 * d1);
  }
}

TEST_CASE("lowpass matches Beads at full amount", "[brume_wow]") {
  stmlib::Random::Seed(7);
  WowFlutter w;
  w.Init(48000.0f);
  std::vector<float> in = Noise(48000, 3);
  std::vector<float> out(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    float l = in[i], r = in[i];
    w.Process(&l, &r, 1.0f);
    out[i] = l;
  }
  const double pin = HighPower(in, 20000);
  const double pout = HighPower(out, 20000);
  REQUIRE(10.0 * std::log10(pout / pin) <= -6.0);
}
