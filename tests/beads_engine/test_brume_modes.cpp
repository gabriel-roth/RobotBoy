// Brume's Mode switching (spillover), the per-Mode makeup gains and hold
// safety (host/brume_engine.h).
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

const float kRate = 48000.0f;
const BrumeMode kModes[] = {BrumeMode::kBeads, BrumeMode::kClouds};

// Flush-to-zero for the scope, as Rack's engine threads (system::
// resetFpuFlags) and the MetaModule (EnableMetaModuleFlushToZero) run.
// Without it, a pre-tank filter fed noise and then silence decays into a
// subnormal limit cycle (DCBlocker: y * pole rounds back to y) that the
// active tank passes on as output of about 1e-40 forever.
class ScopedFlushToZero {
 public:
  ScopedFlushToZero() {
#if defined(__aarch64__)
    __asm__ volatile("mrs %0, fpcr" : "=r"(saved_));
    const uint64_t ftz = saved_ | (uint64_t(1) << 24);
    __asm__ volatile("msr fpcr, %0" : : "r"(ftz));
#elif defined(__x86_64__)
    saved_ = __builtin_ia32_stmxcsr();
    __builtin_ia32_ldmxcsr(unsigned(saved_) | 0x8040);  // FTZ, DAZ
#endif
  }
  ~ScopedFlushToZero() {
#if defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0" : : "r"(saved_));
#elif defined(__x86_64__)
    __builtin_ia32_ldmxcsr(unsigned(saved_));
#endif
  }

 private:
  uint64_t saved_ = 0;
};

size_t At(float seconds) { return size_t(seconds * kRate); }

// Voss-McCartney pink noise: 16 rows, one updated per sample (the row is
// the number of trailing zeros of the sample counter), plus a white row.
// Peaks around +-1 times `amplitude`.
class PinkNoise {
 public:
  PinkNoise(uint32_t seed, float amplitude)
      : rng_(seed), dist_(-1.0f, 1.0f), amplitude_(amplitude) {
    for (float& r : rows_) {
      r = dist_(rng_);
      sum_ += r;
    }
  }

  float Next() {
    ++counter_;
    int row = 0;
    uint32_t n = counter_;
    while (row < kRows - 1 && (n & 1u) == 0) {
      n >>= 1;
      ++row;
    }
    const float r = dist_(rng_);
    sum_ += r - rows_[row];
    rows_[row] = r;
    return amplitude_ * (sum_ + dist_(rng_)) / float(kRows + 1);
  }

 private:
  static const int kRows = 16;
  std::mt19937 rng_;
  std::uniform_real_distribution<float> dist_;
  float amplitude_;
  float rows_[kRows];
  float sum_ = 0.0f;
  uint32_t counter_ = 0;
};

std::unique_ptr<BrumeEngine> MakeEngine(BrumeMode mode) {
  std::unique_ptr<BrumeEngine> e(new BrumeEngine);
  e->SetMode(mode);
  REQUIRE(e->Init(kRate));
  return e;
}

BrumeControls Wet() {
  BrumeControls c = DefaultBrumeControls();
  c.dry_wet = 1.0f;
  return c;
}

double Rms(const std::vector<float>& x, size_t a, size_t b) {
  double e = 0.0;
  for (size_t i = a; i < b; ++i) e += double(x[i]) * x[i];
  return std::sqrt(e / double(b - a));
}

double Db(double ratio) { return 20.0 * std::log10(ratio); }

float MaxDelta(const std::vector<float>& x, size_t begin, size_t end) {
  float m = 0.0f;
  for (size_t i = std::max<size_t>(begin, 1); i < end; ++i) {
    m = std::max(m, std::fabs(x[i] - x[i - 1]));
  }
  return m;
}

// Wet RMS (mono) of `seconds` of pink noise at the defaults, from 2 s on.
double PinkWetRms(BrumeMode mode, float seconds) {
  auto e = MakeEngine(mode);
  const BrumeControls c = Wet();
  PinkNoise left(11, 0.5f);
  PinkNoise right(12, 0.5f);
  std::vector<float> out(At(seconds));
  for (size_t i = 0; i < out.size(); ++i) {
    e->SetControls(c);
    const BrumeFrame f = e->Process(left.Next(), right.Next());
    out[i] = 0.5f * (f.l + f.r);
  }
  return Rms(out, At(2.0f), out.size());
}

}  // namespace

TEST_CASE("old tank rings out, then stops", "[brume_spillover]") {
  ScopedFlushToZero ftz;
  auto e = MakeEngine(BrumeMode::kBeads);
  const BrumeControls c = Wet();
  std::mt19937 rng(21);
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
  const size_t sw = At(2.0f);
  const size_t n = sw + At(3.0f);
  std::vector<float> l(n), r(n);
  for (size_t i = 0; i < n; ++i) {
    if (i == sw) e->SetMode(BrumeMode::kClouds);
    e->SetControls(c);
    const float x = i < sw ? dist(rng) : 0.0f;
    const float y = i < sw ? dist(rng) : 0.0f;
    const BrumeFrame f = e->Process(x, y);
    l[i] = f.l;
    r[i] = f.r;
  }
  REQUIRE(e->active_mode() == BrumeMode::kClouds);
  const double ringing = Rms(l, sw + At(0.95f), sw + At(1.05f));
  INFO("wet RMS at 1 s after the switch " << ringing);
  REQUIRE(ringing > 0.0);
  for (size_t i = sw + At(2.1f); i < n; ++i) {
    INFO("sample " << i - sw << " after the switch");
    REQUIRE(l[i] == 0.0f);
    REQUIRE(r[i] == 0.0f);
  }
}

TEST_CASE("switching back reverses the fade", "[brume_spillover]") {
  auto e = MakeEngine(BrumeMode::kBeads);
  const BrumeControls c = Wet();
  std::mt19937 rng(22);
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
  const size_t to_clouds = At(3.0f);
  const size_t to_beads = At(3.5f);
  const size_t n = At(5.0f);
  std::vector<float> l(n), r(n);
  float beads_gain_at_return = -1.0f;
  float beads_gain_after_return = -1.0f;
  for (size_t i = 0; i < n; ++i) {
    if (i == to_clouds) e->SetMode(BrumeMode::kClouds);
    if (i == to_beads) {
      beads_gain_at_return = e->tank_gain(BrumeMode::kBeads);
      e->SetMode(BrumeMode::kBeads);
    }
    if (i == to_beads + 96) {  // two Bright blocks later
      beads_gain_after_return = e->tank_gain(BrumeMode::kBeads);
    }
    e->SetControls(c);
    const BrumeFrame f = e->Process(dist(rng), dist(rng));
    l[i] = f.l;
    r[i] = f.r;
  }
  const float steady = std::max(
      MaxDelta(l, At(1.0f), to_clouds), MaxDelta(r, At(1.0f), to_clouds));
  const float across = std::max(
      MaxDelta(l, to_clouds, n), MaxDelta(r, to_clouds, n));
  const double before = Rms(l, to_clouds - At(0.05f), to_clouds);
  const double after = Rms(l, to_beads + At(0.01f), to_beads + At(0.06f));
  INFO("steady " << steady << " across " << across);
  INFO("RMS before " << before << " after return " << after
       << " (" << Db(after / before) << " dB)");
  INFO("Beads gain at return " << beads_gain_at_return
       << ", 2 ms later " << beads_gain_after_return);
  REQUIRE(steady > 0.0f);
  REQUIRE(across <= 1.5f * steady);
  REQUIRE(Db(after / before) >= -12.0);
  // The Beads tank was still fading (2 s ramp, 0.5 s in), and its gain
  // turns around from there instead of restarting.
  REQUIRE(beads_gain_at_return > 0.7f);
  REQUIRE(beads_gain_at_return < 0.8f);
  REQUIRE(beads_gain_after_return > beads_gain_at_return);
  REQUIRE(beads_gain_after_return < 1.0f);
  REQUIRE(e->tank_gain(BrumeMode::kBeads) == 1.0f);
}

TEST_CASE("clear and quality changes stop the spillover", "[brume_spillover]") {
  for (int quality_change = 0; quality_change < 2; ++quality_change) {
    auto e = MakeEngine(BrumeMode::kBeads);
    const BrumeControls c = Wet();
    std::mt19937 rng(23);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (size_t i = 0; i < At(2.5f); ++i) {
      if (i == At(2.0f)) e->SetMode(BrumeMode::kClouds);
      if (i == At(2.2f)) {
        INFO("quality change " << quality_change);
        REQUIRE(e->tank_gain(BrumeMode::kBeads) > 0.0f);
        if (quality_change) {
          e->SetQuality(robotboy_beads::BrumeQuality::kCold);
        } else {
          e->RequestClear();
        }
      }
      e->SetControls(c);
      e->Process(dist(rng), dist(rng));
    }
    INFO("quality change " << quality_change);
    REQUIRE(e->tank_gain(BrumeMode::kBeads) == 0.0f);
    REQUIRE(e->tank_gain(BrumeMode::kClouds) == 1.0f);
  }
}

TEST_CASE("modes level-matched", "[brume_levels]") {
  const double beads = PinkWetRms(BrumeMode::kBeads, 10.0f);
  const double clouds = PinkWetRms(BrumeMode::kClouds, 10.0f);
  INFO("Beads " << beads << " Clouds " << clouds
       << " (" << Db(clouds / beads) << " dB)");
  REQUIRE(std::fabs(Db(clouds / beads)) <= 1.0);
}

TEST_CASE("hold with sustained input stays bounded", "[brume_hold]") {
  double rms[2];
  for (int m = 0; m < 2; ++m) {
    auto e = MakeEngine(kModes[m]);
    BrumeControls c = Wet();
    c.decay = 1.0f;
    PinkNoise left(31, 0.5f);
    PinkNoise right(32, 0.5f);
    std::vector<float> out(At(20.0f));
    for (size_t i = 0; i < out.size(); ++i) {
      e->SetControls(c);
      const BrumeFrame f = e->Process(left.Next() + 0.2f, right.Next() + 0.2f);
      INFO("mode " << m << " sample " << i);
      REQUIRE(std::isfinite(f.l));
      REQUIRE(std::isfinite(f.r));
      REQUIRE(std::fabs(f.l) <= 2.0f);
      REQUIRE(std::fabs(f.r) <= 2.0f);
      out[i] = 0.5f * (f.l + f.r);
    }
    rms[m] = Rms(out, At(2.0f), out.size());
  }
  INFO("Beads " << rms[0] << " Clouds " << rms[1]
       << " (" << Db(rms[1] / rms[0]) << " dB)");
  REQUIRE(std::fabs(Db(rms[1] / rms[0])) <= 6.0);
}

// Measures kCloudsMakeup: the ratio of the Beads to the Clouds wet RMS for
// pink noise at the defaults, with the current makeups (so set
// kCloudsMakeup to 1 first, or multiply the result by it). Run by hand:
//   ./build/beads_engine_tests "[.calibrate]"
TEST_CASE("calibrate the Clouds makeup", "[.calibrate]") {
  const double beads = PinkWetRms(BrumeMode::kBeads, 30.0f);
  const double clouds = PinkWetRms(BrumeMode::kClouds, 30.0f);
  std::printf("Beads %.6f Clouds %.6f ratio %.5f (makeup now %.5f)\n",
              beads, clouds, beads / clouds,
              double(robotboy_beads::kCloudsMakeup));
}
