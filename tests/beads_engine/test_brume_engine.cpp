// Brume's engine core (host/brume_engine.h): tanks, send effects,
// resampling, clear and quality fades.
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#include "host/brume_engine.h"

using robotboy_beads::BrumeControls;
using robotboy_beads::BrumeEngine;
using robotboy_beads::BrumeFrame;
using robotboy_beads::BrumeMode;
using robotboy_beads::BrumeQuality;
using robotboy_beads::DefaultBrumeControls;

namespace {

const float kPi = 3.14159265358979f;

std::unique_ptr<BrumeEngine> MakeEngine(float host_rate) {
  std::unique_ptr<BrumeEngine> e(new BrumeEngine);
  REQUIRE(e->Init(host_rate));
  return e;
}

std::vector<float> Noise(size_t n, uint32_t seed, float amplitude = 0.5f) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-amplitude, amplitude);
  std::vector<float> v(n);
  for (float& x : v) x = dist(rng);
  return v;
}

double Energy(const std::vector<float>& x, size_t begin, size_t end) {
  double e = 0.0;
  for (size_t i = begin; i < end; ++i) e += double(x[i]) * x[i];
  return e;
}

float MaxDelta(const std::vector<float>& x, size_t begin, size_t end) {
  float m = 0.0f;
  for (size_t i = std::max<size_t>(begin, 1); i < end; ++i) {
    m = std::max(m, std::fabs(x[i] - x[i - 1]));
  }
  return m;
}

const BrumeQuality kQualities[] = {
    BrumeQuality::kBright, BrumeQuality::kCold, BrumeQuality::kScorched};
const BrumeMode kModes[] = {BrumeMode::kBeads, BrumeMode::kClouds};

// Index of the first output at or after `from` above `threshold`.
size_t Onset(const std::vector<float>& x, size_t from, float threshold) {
  for (size_t i = from; i < x.size(); ++i) {
    if (std::fabs(x[i]) > threshold) return i;
  }
  return x.size();
}

}  // namespace

TEST_CASE("silence in, exact silence out", "[brume_engine]") {
  for (BrumeMode mode : kModes) {
    for (BrumeQuality quality : kQualities) {
      auto e = MakeEngine(48000.0f);
      e->SetMode(mode);
      e->SetQuality(quality);
      const BrumeControls c = DefaultBrumeControls();
      for (int i = 0; i < 96000; ++i) {
        e->SetControls(c);
        const BrumeFrame f = e->Process(0.0f, 0.0f);
        REQUIRE(f.l == 0.0f);
        REQUIRE(f.r == 0.0f);
      }
      REQUIRE(e->active_mode() == mode);
      REQUIRE(e->active_quality() == quality);
    }
  }
}

TEST_CASE("dry/wet 0 passes input exactly", "[brume_engine]") {
  auto e = MakeEngine(48000.0f);
  BrumeControls c = DefaultBrumeControls();
  c.dry_wet = 0.0f;
  c.wow = 0.0f;
  const std::vector<float> l = Noise(96000, 1);
  const std::vector<float> r = Noise(96000, 2);
  for (size_t i = 0; i < l.size(); ++i) {
    e->SetControls(c);
    const BrumeFrame f = e->Process(l[i], r[i]);
    REQUIRE(f.l == l[i]);
    REQUIRE(f.r == r[i]);
  }
}

// A 50 ms noise burst: the tail must come out of the tank at a real level
// before it decays away.
TEST_CASE("impulse tail decays", "[brume_engine]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(48000.0f);
    e->SetMode(mode);
    BrumeControls c = DefaultBrumeControls();
    c.dry_wet = 1.0f;
    c.decay = 0.5f;
    const size_t n = 48000 * 30;
    const size_t burst = 2400;  // 50 ms
    const std::vector<float> noise = Noise(burst, 8);
    double input = 0.0;
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
      e->SetControls(c);
      const float x = i < burst ? noise[i] : 0.0f;
      input += double(x) * x;
      const BrumeFrame f = e->Process(x, x);
      out[i] = 0.5f * (f.l + f.r);
    }
    const double first = Energy(out, 0, 48000);
    const double last = Energy(out, n - 48000, n);
    INFO("mode " << int(mode) << " input " << input << " first " << first
         << " last " << last);
    REQUIRE(first > input * 1e-4);  // -40 dB
    REQUIRE(last < first * 1e-9);
  }
}

TEST_CASE("width 0 is mono wet", "[brume_engine]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(48000.0f);
    e->SetMode(mode);
    BrumeControls c = DefaultBrumeControls();
    c.dry_wet = 1.0f;
    c.width = 0.0f;
    const std::vector<float> l = Noise(48000, 3);
    const std::vector<float> r = Noise(48000, 4);
    bool nonzero = false;
    for (size_t i = 0; i < l.size(); ++i) {
      e->SetControls(c);
      const BrumeFrame f = e->Process(l[i], r[i]);
      REQUIRE(f.l == f.r);
      nonzero = nonzero || f.l != 0.0f;
    }
    REQUIRE(nonzero);
  }
}

TEST_CASE("clear empties within 15 ms", "[brume_engine]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(48000.0f);
    e->SetMode(mode);
    BrumeControls c = DefaultBrumeControls();
    c.dry_wet = 1.0f;
    c.decay = 0.9f;
    const std::vector<float> noise = Noise(96000, 5);
    std::vector<float> l, r;
    for (size_t i = 0; i < noise.size(); ++i) {
      e->SetControls(c);
      const BrumeFrame f = e->Process(noise[i], -noise[i]);
      l.push_back(f.l);
      r.push_back(f.r);
    }
    const float pre_delta = std::max(
        MaxDelta(l, 48000, l.size()), MaxDelta(r, 48000, r.size()));
    const size_t request = l.size();
    e->RequestClear();
    for (int i = 0; i < 48000; ++i) {
      e->SetControls(c);
      const BrumeFrame f = e->Process(0.0f, 0.0f);
      l.push_back(f.l);
      r.push_back(f.r);
    }
    const size_t a = request + 720;   // 15 ms
    const size_t b = request + 4800;  // 100 ms
    INFO("mode " << int(mode));
    REQUIRE(Energy(l, a, b) == 0.0);
    REQUIRE(Energy(r, a, b) == 0.0);
    REQUIRE(MaxDelta(l, request, l.size()) <= pre_delta);
    REQUIRE(MaxDelta(r, request, r.size()) <= pre_delta);
  }
}

// A steady sine through a Clear: without the 10 ms fade-out the wet would
// drop to zero in one sample.
TEST_CASE("clear does not click", "[brume_engine]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(48000.0f);
    e->SetMode(mode);
    BrumeControls c = DefaultBrumeControls();
    c.dry_wet = 1.0f;
    c.decay = 0.9f;
    const size_t clear = 48000 * 3;
    const size_t n = 48000 * 4;
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) {
      if (i == clear) e->RequestClear();
      e->SetControls(c);
      const float x = 0.5f * std::sin(2.0f * kPi * 220.0f * float(i) / 48000.0f);
      const BrumeFrame f = e->Process(x, x);
      l[i] = f.l;
      r[i] = f.r;
    }
    const float steady = std::max(
        MaxDelta(l, 48000, clear), MaxDelta(r, 48000, clear));
    const float across = std::max(
        MaxDelta(l, clear, clear + 48000), MaxDelta(r, clear, clear + 48000));
    INFO("mode " << int(mode) << " steady " << steady << " across " << across);
    REQUIRE(steady > 0.0f);
    REQUIRE(across <= 1.5f * steady);
  }
}

TEST_CASE("host rates and qualities", "[brume_rates]") {
  const float kRates[] = {44100.0f, 48000.0f, 96000.0f};
  for (float rate : kRates) {
    for (BrumeQuality quality : kQualities) {
      INFO("rate " << rate << " quality " << int(quality));
      auto e = MakeEngine(rate);
      e->SetQuality(quality);
      BrumeControls c = DefaultBrumeControls();
      c.dry_wet = 1.0f;

      // Noise: every output finite, one output per input, wet comes out.
      const size_t n = size_t(rate * 3.0f);
      const std::vector<float> noise = Noise(n, 6);
      size_t count = 0;
      double energy = 0.0;
      for (size_t i = 0; i < n; ++i) {
        e->SetControls(c);
        const BrumeFrame f = e->Process(noise[i], noise[i]);
        REQUIRE(std::isfinite(f.l));
        REQUIRE(std::isfinite(f.r));
        energy += double(f.l) * f.l;
        ++count;
      }
      REQUIRE(count == n);
      REQUIRE(energy > 0.0);
      // The output FIFO never ran dry after it first primed.
      REQUIRE(e->underflows() == 0);

      // Wet onset latency: the same for two impulses 1 s apart.
      auto e2 = MakeEngine(rate);
      e2->SetQuality(quality);
      c.decay = 0.0f;
      const size_t second = size_t(rate);
      const size_t first_impulse = size_t(rate * 0.5f);
      const size_t second_impulse = first_impulse + second;
      std::vector<float> out(second_impulse + second);
      for (size_t i = 0; i < out.size(); ++i) {
        e2->SetControls(c);
        const float x =
            (i == first_impulse || i == second_impulse) ? 0.5f : 0.0f;
        out[i] = e2->Process(x, x).l;
      }
      const float threshold = 1e-3f;
      REQUIRE(Energy(out, first_impulse - size_t(rate * 0.1f), first_impulse)
              == 0.0);
      const size_t onset_1 = Onset(out, first_impulse - 200, threshold);
      const size_t onset_2 = Onset(out, second_impulse - 200, threshold);
      REQUIRE(onset_1 < second_impulse - 200);
      REQUIRE(onset_2 < out.size());
      REQUIRE(onset_1 - first_impulse == onset_2 - second_impulse);
    }
  }
}

TEST_CASE("non-finite input and controls are contained", "[brume_nan]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(48000.0f);
    e->SetMode(mode);
    BrumeControls c = DefaultBrumeControls();
    c.dry_wet = 0.8f;
    const std::vector<float> noise = Noise(96000, 7);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    size_t i = 0;
    for (; i < 24000; ++i) {
      e->SetControls(c);
      const BrumeFrame f = e->Process(noise[i], noise[i]);
      REQUIRE(std::isfinite(f.l));
    }
    for (int k = 0; k < 10; ++k, ++i) {
      BrumeControls bad = c;
      bad.decay = nan;
      bad.send = inf;
      bad.dry_wet = nan;
      e->SetControls(bad);
      const BrumeFrame f = e->Process(k % 2 ? nan : inf, k % 2 ? -inf : nan);
      REQUIRE(std::isfinite(f.l));
      REQUIRE(std::isfinite(f.r));
    }
    double tail = 0.0;
    for (; i < noise.size(); ++i) {
      e->SetControls(c);
      const BrumeFrame f = e->Process(noise[i], noise[i]);
      REQUIRE(std::isfinite(f.l));
      REQUIRE(std::isfinite(f.r));
      if (i + 4800 >= noise.size()) tail += double(f.l) * f.l;
    }
    REQUIRE(tail > 0.0);
  }
}

// Also checks the moment the wet stops for the reconfigure: it must have
// faded to (nearly) nothing first, not step to 0 from the last few faded
// frames.
TEST_CASE("quality change does not click", "[brume_engine]") {
  for (BrumeMode mode : kModes) {
    auto e = MakeEngine(48000.0f);
    e->SetMode(mode);
    BrumeControls c = DefaultBrumeControls();
    c.dry_wet = 1.0f;
    const size_t change = 48000 * 3;
    const size_t n = 48000 * 5;
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) {
      if (i == change) e->SetQuality(BrumeQuality::kScorched);
      e->SetControls(c);
      const float x = 0.5f * std::sin(2.0f * kPi * 220.0f * float(i) / 48000.0f);
      const BrumeFrame f = e->Process(x, x);
      l[i] = f.l;
      r[i] = f.r;
    }
    REQUIRE(e->active_quality() == BrumeQuality::kScorched);
    const float steady = std::max(
        MaxDelta(l, 48000, change), MaxDelta(r, 48000, change));
    const float across = std::max(
        MaxDelta(l, change, change + 48000), MaxDelta(r, change, change + 48000));
    float peak = 0.0f;
    for (size_t i = 48000; i < change; ++i) {
      peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
    // The largest step from a nonzero output to exact 0 during the change.
    float stop = 0.0f;
    for (size_t i = change; i < change + 4800; ++i) {
      if (l[i] == 0.0f && l[i - 1] != 0.0f) {
        stop = std::max(stop, std::fabs(l[i - 1]));
      }
      if (r[i] == 0.0f && r[i - 1] != 0.0f) {
        stop = std::max(stop, std::fabs(r[i - 1]));
      }
    }
    INFO("mode " << int(mode) << " steady " << steady << " across " << across
         << " peak " << peak << " stop " << stop);
    REQUIRE(steady > 0.0f);
    REQUIRE(across <= 1.5f * steady);
    // One 10 ms fade step at 48 kHz is 1/480 (0.2%) of the level; dropping
    // the last faded frames from the FIFO stepped about 1.6% of peak.
    REQUIRE(stop <= 5e-3f * peak);
  }
}

// DECAY 1.0 (the hold zone), a 0.2 s 110 Hz sine burst, Bright: tail energy
// in the 20th second against the 2nd.
static double HoldTailRatioDb(BrumeMode mode) {
  auto e = MakeEngine(48000.0f);
  e->SetMode(mode);
  BrumeControls c = DefaultBrumeControls();
  c.dry_wet = 1.0f;
  c.decay = 1.0f;
  const size_t n = 48000 * 21;
  std::vector<float> out(n);
  for (size_t i = 0; i < n; ++i) {
    e->SetControls(c);
    const float x = i < 9600
        ? 0.3f * std::sin(2.0f * kPi * 110.0f * float(i) / 48000.0f)
        : 0.0f;
    const BrumeFrame f = e->Process(x, x);
    out[i] = 0.5f * (f.l + f.r);
  }
  return 10.0 * std::log10(
      Energy(out, 48000 * 20, n) / Energy(out, 48000 * 2, 48000 * 3));
}

TEST_CASE("Beads hold sustains", "[brume_hold]") {
  const double db = HoldTailRatioDb(BrumeMode::kBeads);
  INFO("20 s re 2 s: " << db << " dB");
  REQUIRE(db > -6.0);
  REQUIRE(db < 6.0);
}

// Characterization, not a law: Clouds' tank stores 12-bit samples
// (clouds/dsp/fx/fx_engine.h), and the truncation plus its interpolated
// reads lose a little on every pass, so even at hold a tail slowly fades
// (about -5 dB over 18 s here; quiet or noisy material fades faster, down to
// nothing for quiet noise). This is upstream behavior, kept for
// authenticity; the test only pins that a sine tail is still well audible.
TEST_CASE("Clouds hold fades slowly", "[brume_hold]") {
  const double db = HoldTailRatioDb(BrumeMode::kClouds);
  INFO("20 s re 2 s: " << db << " dB");
  REQUIRE(db > -20.0);
  REQUIRE(db < 3.0);
}
