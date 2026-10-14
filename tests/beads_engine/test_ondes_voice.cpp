// Ondes runs Beads' WavetableVoice: tuning, level and bank crossfades.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "stmlib/dsp/dsp.h"
#include "beads/resources.h"
#include "beads/dsp/frame.h"
#include "beads/dsp/granular_player.h"
#include "beads/dsp/wavetable_grain.h"
#include "beads/dsp/wavetable_voice.h"

namespace {

struct Voice {
  beads::WavetableVoice voice;
  beads::Parameters p{};
  explicit Voice(float sr) {
    voice.Init(sr);
    p.pitch_lock = nullptr;
  }
  float Render(int bank, float pitch, float position) {
    p.pitch.base = pitch;
    p.time.base = position;
    beads::FloatFrame out{0.0f, 0.0f};
    const bool freeze = false;
    voice.Render(bank, 16, &freeze, p, &out, 1);
    return out.l;
  }
};

}  // namespace

TEST_CASE("Ondes voice: +12 st over Beads' C3 root plays C4") {
  const float sr = 48000.0f;
  Voice v(sr);
  // Bank 0, position 0 is a plain sine.
  std::vector<float> s;
  for (int i = 0; i < 48000 * 2; ++i) s.push_back(v.Render(0, 12.0f, 0.0f));
  int crossings = 0;
  double first = -1, last = -1;
  for (size_t i = 24000; i < s.size(); ++i) {
    if (s[i - 1] < 0.0f && s[i] >= 0.0f) {
      const double t = double(i - 1) + s[i - 1] / (s[i - 1] - s[i]);
      if (first < 0) first = t; else ++crossings;
      last = t;
    }
  }
  const double f = crossings * sr / (last - first);
  CHECK(std::fabs(f - 261.62) < 0.5);
}

TEST_CASE("Ondes voice stays within full scale on every bank and position") {
  Voice v(44100.0f);
  for (int bank = 0; bank < 12; ++bank) {
    for (float position : {0.0f, 0.37f, 1.0f}) {
      float peak = 0.0f;
      for (int i = 0; i < 4410; ++i)
        peak = std::max(peak, std::fabs(v.Render(bank, -12.0f, position)));
      CAPTURE(bank, position);
      CHECK(peak <= 1.0f);
      CHECK(peak > 0.05f);
    }
  }
}

TEST_CASE("Ondes voice crossfades between banks without a jump") {
  Voice v(48000.0f);
  float previous = 0.0f, worst = 0.0f, typical = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    const int bank = i < 24000 ? 2 : 9;
    const float x = v.Render(bank, 0.0f, 0.5f);
    const float step = std::fabs(x - previous);
    if (i > 1000 && i < 23000) typical = std::max(typical, step);
    if (i >= 23900 && i < 26000) worst = std::max(worst, step);
    previous = x;
  }
  CHECK(worst < 4.0f * typical + 0.01f);
}

// --- OndesVoice: the optional oversampling ---------------------------------

#include "ondes_voice.h"

namespace {

void Fft(std::vector<std::complex<double>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const std::complex<double> wl = std::polar(1.0, -2.0 * M_PI / double(len));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w = 1.0;
      for (size_t k = 0; k < len / 2; ++k, w *= wl) {
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
      }
    }
  }
}

// Share of the output's power (dB) that is off the harmonic series, from a
// Blackman-Harris FFT: a bin counts as harmonic within 4 Hz of k * f0, with
// f0 taken from the strongest peak so the pitch table's rounding doesn't
// matter.
double InharmonicDb(int oversampling, int bank, float semitones, float position) {
  const float sr = 48000.0f;
  robotboy_beads::OndesVoice v;
  v.Init(sr, oversampling);
  for (int i = 0; i < 48000; ++i) v.Render(bank, 16, semitones, position);
  const size_t n = 1 << 16;
  std::vector<std::complex<double>> x(n);
  for (size_t i = 0; i < n; ++i) {
    const double t = 2.0 * M_PI * double(i) / double(n - 1);
    const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t)
        - 0.01168 * std::cos(3 * t);
    x[i] = w * v.Render(bank, 16, semitones, position);
  }
  Fft(x);
  const double bin = sr / double(n);
  std::vector<double> p(n / 2);
  size_t peak = 1;
  for (size_t i = 1; i < n / 2; ++i) {
    p[i] = std::norm(x[i]);
    if (p[i] > p[peak]) peak = i;
  }
  const double nominal = beads::kWavetableRootNote * std::pow(2.0, semitones / 12.0);
  const double f0 = double(peak) * bin / std::max(1.0, std::round(double(peak) * bin / nominal));
  double total = 0.0, off = 0.0;
  for (size_t i = 1; i < n / 2; ++i) {
    const double f = double(i) * bin;
    if (f < 5.0) continue;
    total += p[i];
    if (std::fabs(f - std::round(f / f0) * f0) > 4.0) off += p[i];
  }
  return 10.0 * std::log10(std::max(off, 1e-30) / total);
}

}  // namespace

TEST_CASE("OndesVoice without oversampling is the bare Beads voice") {
  Voice bare(44100.0f);
  robotboy_beads::OndesVoice v;
  v.Init(44100.0f, 1);
  for (int i = 0; i < 44100; ++i) {
    const int bank = i < 20000 ? 3 : 11;
    const float pitch = -12.0f + 30.0f * float(i) / 44100.0f;
    const float position = float(i % 9000) / 9000.0f;
    REQUIRE(v.Render(bank, 16, pitch, position) == bare.Render(bank, pitch, position));
  }
}

TEST_CASE("OndesVoice oversampling cuts the voice's aliasing") {
  // The brightest wave, bank 12's last, a little above C4.
  const double off = InharmonicDb(1, 11, 12.37f, 1.0f);
  const double x2 = InharmonicDb(2, 11, 12.37f, 1.0f);
  const double x4 = InharmonicDb(4, 11, 12.37f, 1.0f);
  CAPTURE(off, x2, x4);
  CHECK(off > -20.0);   // the problem is real
  CHECK(x2 < off - 25.0);
  CHECK(x4 < x2 - 10.0);
}

TEST_CASE("OndesVoice oversampling keeps tuning and level") {
  for (int oversampling : {2, 4}) {
    const float sr = 48000.0f;
    robotboy_beads::OndesVoice v;
    v.Init(sr, oversampling);
    std::vector<float> s;
    for (int i = 0; i < 48000 * 2; ++i) s.push_back(v.Render(0, 16, 12.0f, 0.0f));
    int crossings = 0;
    double first = -1, last = -1, power = 0.0;
    for (size_t i = 24000; i < s.size(); ++i) {
      power += double(s[i]) * s[i];
      if (s[i - 1] < 0.0f && s[i] >= 0.0f) {
        const double t = double(i - 1) + s[i - 1] / (s[i - 1] - s[i]);
        if (first < 0) first = t; else ++crossings;
        last = t;
      }
    }
    const double f = crossings * sr / (last - first);
    const double rms = std::sqrt(power / double(s.size() - 24000));
    CAPTURE(oversampling, f, rms);
    CHECK(std::fabs(f - 261.62) < 0.5);
    CHECK(std::fabs(rms - 0.7071) < 0.01);   // bank 1's first wave: a full-scale sine
  }
}
