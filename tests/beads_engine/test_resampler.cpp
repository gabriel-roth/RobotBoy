// Resampler: bypass exactness, sine fidelity through a rate round trip,
// stopband rejection, long-run frame-count accuracy, agreement with the
// pre-polyphase implementation, and the held (gate) channel.
#include <catch2/catch_amalgamated.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "host/resampler.h"
#include "reference_resampler.h"

using robotboy_beads::Resampler;

namespace {

std::vector<float> Convert(Resampler<1>& r, const std::vector<float>& in) {
  std::vector<float> out;
  for (float x : in) {
    r.Push(&x);
    float y;
    while (r.Pop(&y)) out.push_back(y);
  }
  return out;
}

std::vector<float> Sine(float freq, float rate, float seconds, float amp) {
  std::vector<float> s(size_t(rate * seconds));
  for (size_t i = 0; i < s.size(); ++i)
    s[i] = amp * std::sin(2.0 * M_PI * freq * double(i) / rate);
  return s;
}

float Rms(const std::vector<float>& s, size_t from, size_t to) {
  double acc = 0.0;
  for (size_t i = from; i < to; ++i) acc += double(s[i]) * s[i];
  return float(std::sqrt(acc / double(to - from)));
}

// Frequency from rising zero crossings with linear interpolation.
float ZeroCrossingFrequency(const std::vector<float>& s, float rate,
                            size_t from, size_t to) {
  double first = -1.0, last = -1.0;
  int count = 0;
  for (size_t i = from + 1; i < to; ++i) {
    if (s[i - 1] < 0.0f && s[i] >= 0.0f) {
      const double t = double(i - 1) + s[i - 1] / (s[i - 1] - s[i]);
      if (first < 0.0) first = t; else ++count;
      last = t;
    }
  }
  return float(count * rate / (last - first));
}

int CountEdges(const std::vector<bool>& g) {
  int n = 0;
  for (size_t i = 1; i < g.size(); ++i) n += g[i] != g[i - 1];
  return n;
}

}  // namespace

TEST_CASE("Resampler at equal rates is a bit-exact pass-through") {
  Resampler<1> r;
  r.Init(48000.0f, 48000.0f);
  REQUIRE(r.bypass());
  CHECK(r.delay_in_output_samples() == 0.0f);
  std::vector<float> in = Sine(1234.0f, 48000.0f, 0.1f, 0.7f);
  in[17] = NAN;  // even a NaN passes through untouched in bypass
  std::vector<float> out = Convert(r, in);
  REQUIRE(out.size() == in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (i == 17) CHECK(std::isnan(out[i]));
    else CHECK(out[i] == in[i]);
  }
}

TEST_CASE("1 kHz sine survives 44.1k -> 24k -> 44.1k") {
  Resampler<1> down, up;
  down.Init(44100.0f, 24000.0f);
  up.Init(24000.0f, 44100.0f);
  const std::vector<float> in = Sine(1000.0f, 44100.0f, 2.0f, 0.5f);
  const std::vector<float> out = Convert(up, Convert(down, in));
  REQUIRE(out.size() > in.size() - 400);
  const size_t a = 4410, b = out.size() - 4410;
  const float gain_db = 20.0f * std::log10(Rms(out, a, b) / Rms(in, a, b));
  CHECK(std::fabs(gain_db) < 0.5f);
  const float f = ZeroCrossingFrequency(out, 44100.0f, a, b);
  CHECK_THAT(f, Catch::Matchers::WithinRel(1000.0f, 0.005f));
}

TEST_CASE("Content above the lower rate's Nyquist is rejected") {
  Resampler<1> down;
  down.Init(48000.0f, 24000.0f);
  // 15 kHz is above 12 kHz (24k Nyquist); it would alias to 9 kHz.
  const std::vector<float> in = Sine(15000.0f, 48000.0f, 1.0f, 1.0f);
  const std::vector<float> out = Convert(down, in);
  const float level_db = 20.0f * std::log10(
      Rms(out, 2400, out.size() - 100) / Rms(in, 4800, in.size() - 100));
  CHECK(level_db < -40.0f);
}

TEST_CASE("Output frame count tracks the rate ratio over a long run") {
  const float rates[][2] = {
      {44100.0f, 32000.0f}, {96000.0f, 24000.0f}, {24000.0f, 44100.0f},
      {48000.0f, 32000.0f}, {32000.0f, 192000.0f}};
  for (const auto& rr : rates) {
    Resampler<1> r;
    r.Init(rr[0], rr[1]);
    const size_t n_in = size_t(rr[0] * 10.0f);
    size_t n_out = 0;
    float x = 0.25f, y;
    for (size_t i = 0; i < n_in; ++i) {
      r.Push(&x);
      while (r.Pop(&y)) ++n_out;
    }
    const double expected =
        double(n_in) * rr[1] / rr[0] - r.delay_in_output_samples();
    CAPTURE(rr[0], rr[1], n_out, expected);
    CHECK(std::fabs(double(n_out) - expected) <= 2.0);
    CHECK_THAT(y, Catch::Matchers::WithinAbs(0.25f, 0.002f));  // DC gain ~1
  }
}

TEST_CASE("768 kHz host into the 24 kHz engine keeps full rejection") {
  Resampler<1> down;
  down.Init(768000.0f, 24000.0f);
  // 20 kHz would alias to 4 kHz at 24 kHz.
  const std::vector<float> in = Sine(20000.0f, 768000.0f, 0.5f, 1.0f);
  const std::vector<float> out = Convert(down, in);
  const float level_db = 20.0f * std::log10(
      Rms(out, 2400, out.size() - 100) / Rms(in, 76800, in.size() - 100));
  CHECK(level_db < -40.0f);
  // and a passband tone survives
  Resampler<1> down2;
  down2.Init(768000.0f, 24000.0f);
  const std::vector<float> tone = Sine(1000.0f, 768000.0f, 0.5f, 0.5f);
  const std::vector<float> out2 = Convert(down2, tone);
  const float gain_db = 20.0f * std::log10(
      Rms(out2, 2400, out2.size() - 100) / Rms(tone, 76800, tone.size() - 100));
  CHECK(std::fabs(gain_db) < 0.5f);
}

namespace {

// Rate pairs the plugin meets: every engine rate against common host
// rates, both directions, plus Ondes' 2x and 4x decimators.
const float kRatePairs[][2] = {
    {48000.0f, 32000.0f}, {32000.0f, 48000.0f}, {48000.0f, 24000.0f},
    {24000.0f, 48000.0f}, {44100.0f, 48000.0f}, {48000.0f, 44100.0f},
    {44100.0f, 32000.0f}, {32000.0f, 44100.0f}, {44100.0f, 24000.0f},
    {24000.0f, 44100.0f}, {96000.0f, 32000.0f}, {32000.0f, 96000.0f},
    {96000.0f, 48000.0f}, {192000.0f, 48000.0f}, {176400.0f, 44100.0f},
    {768000.0f, 24000.0f}};

// Deterministic broadband test signal, two channels.
std::vector<std::array<float, 2>> Noise(size_t n) {
  std::vector<std::array<float, 2>> s(n);
  uint32_t state = 0x12345678u;
  auto next = [&state]() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(int32_t(state)) * (0.5f / 2147483648.0f);
  };
  for (auto& f : s) f = {next(), next()};
  return s;
}

}  // namespace

TEST_CASE("Polyphase output matches the per-tap reference") {
  const auto in = Noise(96000);
  for (const auto& rr : kRatePairs) {
    Resampler<2> r;
    reference::ReferenceResampler<2> ref;
    r.Init(rr[0], rr[1]);
    ref.Init(rr[0], rr[1]);
    CAPTURE(rr[0], rr[1]);
    // 768k -> 24k (32:1, 1 x 1142 taps) also fits.
    CHECK(r.polyphase());
    // Compared as whole streams: the reference's double-precision phase
    // can land a hair under a whole sample and so emit an output one push
    // later than the exact phase does (same instant, same value).
    std::vector<std::array<float, 2>> a, b;
    for (const auto& f : in) {
      r.Push(f.data());
      ref.Push(f.data());
      std::array<float, 2> y;
      while (r.Pop(y.data())) a.push_back(y);
      while (ref.Pop(y.data())) b.push_back(y);
    }
    CAPTURE(a.size(), b.size());
    CHECK(a.size() + 1 >= b.size());
    CHECK(b.size() + 1 >= a.size());
    float max_diff = 0.0f;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
      for (int c = 0; c < 2; ++c) max_diff = std::max(max_diff, std::fabs(a[i][c] - b[i][c]));
    CAPTURE(max_diff);
    // About -120 dB under the signal: float rounding, nothing audible.
    CHECK(max_diff < 1e-6f);
  }
}

TEST_CASE("A ratio with no small fraction keeps the per-tap path, unchanged") {
  const auto in = Noise(20000);
  const float pairs[][2] = {{48000.0f, 31999.5f}, {44100.0f, 47999.0f}};
  for (const auto& rr : pairs) {
    Resampler<2> r;
    reference::ReferenceResampler<2> ref;
    r.Init(rr[0], rr[1]);
    ref.Init(rr[0], rr[1]);
    CAPTURE(rr[0], rr[1]);
    CHECK_FALSE(r.polyphase());
    for (const auto& f : in) {
      r.Push(f.data());
      ref.Push(f.data());
      float a[2], b[2];
      while (r.Pop(a)) {
        REQUIRE(ref.Pop(b));
        CHECK(a[0] == b[0]);
        CHECK(a[1] == b[1]);
      }
      CHECK_FALSE(ref.Pop(b));
    }
  }
}

TEST_CASE("A held channel gives the gate the filtered channel would give") {
  // The old host resampled the gate with the sinc and thresholded at 0.5.
  // Gates as Beads makes them: high for half of each grain, lengths from
  // one engine sample up.
  std::vector<float> gate;
  for (int len = 1; len < 400; len = len * 3 / 2 + 1) {
    gate.insert(gate.end(), size_t(len), 1.0f);
    gate.insert(gate.end(), size_t(len + 7), 0.0f);
  }
  for (const auto& rr : kRatePairs) {
    // The host only sends the gate from the engine (24 to 48 kHz) out to
    // the host rate (44.1 kHz and up), so at most 48k -> 44.1k down.
    if (rr[1] < 0.9f * rr[0]) continue;
    Resampler<1, 1> r;
    reference::ReferenceResampler<2> ref;
    r.Init(rr[0], rr[1]);
    ref.Init(rr[0], rr[1]);
    CAPTURE(rr[0], rr[1]);
    std::vector<bool> held, filtered;
    for (float g : gate) {
      const float f[2] = {0.0f, g};
      r.Push(f);
      ref.Push(f);
      float a[2], b[2];
      while (r.Pop(a)) held.push_back(a[1] > 0.5f);
      while (ref.Pop(b)) filtered.push_back(b[1] > 0.5f);
    }
    REQUIRE(held.size() == filtered.size());
    // Edges may move by one output sample (the sinc rings around short
    // pulses); never more, and no pulse appears or vanishes.
    CHECK(CountEdges(held) == CountEdges(filtered));
    for (size_t i = 0; i < held.size(); ++i) {
      if (held[i] != filtered[i]) {
        CAPTURE(i);
        CHECK(((i > 0 && held[i - 1] == filtered[i]) ||
               (i + 1 < held.size() && held[i + 1] == filtered[i])));
      }
    }
  }
}

TEST_CASE("Held channels pass through untouched in bypass") {
  Resampler<1, 1> r;
  r.Init(48000.0f, 48000.0f);
  const float f[2] = {0.25f, 1.0f};
  r.Push(f);
  float out[2];
  REQUIRE(r.Pop(out));
  CHECK(out[0] == 0.25f);
  CHECK(out[1] == 1.0f);
}
