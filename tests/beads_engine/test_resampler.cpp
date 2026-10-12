// Resampler: bypass exactness, sine fidelity through a rate round trip,
// stopband rejection, and long-run frame-count accuracy.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <vector>

#include "host/resampler.h"

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
