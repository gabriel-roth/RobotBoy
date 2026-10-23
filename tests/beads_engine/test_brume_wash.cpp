// Brume's SMEAR wash (host/wash_diffuser.h).
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <random>
#include <vector>

#include "host/wash_diffuser.h"

using namespace robotboy_beads;
using Catch::Approx;

namespace {

struct Rig {
  std::vector<float> memory;
  WashDiffuser wash;
  explicit Rig(float rate) : memory(WashDiffuser::kMemorySize) {
    wash.Init(memory.data(), rate);
  }
};

// One impulse in both channels, then silence, at the given amount.
void ImpulseResponse(Rig& rig, size_t frames, std::vector<float>* l,
                     std::vector<float>* r) {
  for (size_t i = 0; i < frames; ++i) {
    clouds::FloatFrame f = {i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f};
    rig.wash.Process(&f, 1, 1.0f);
    l->push_back(f.l);
    r->push_back(f.r);
  }
}

double Energy(const std::vector<float>& x) {
  double e = 0.0;
  for (float v : x) e += double(v) * v;
  return e;
}

}  // namespace

TEST_CASE("wash is energy-preserving", "[brume_wash]") {
  for (float rate : {24000.0f, 32000.0f, 48000.0f}) {
    Rig rig(rate);
    std::vector<float> l, r;
    ImpulseResponse(rig, size_t(6.0f * rate), &l, &r);
    for (float v : l) REQUIRE(std::isfinite(v));
    for (float v : r) REQUIRE(std::isfinite(v));
    CHECK(Energy(l) == Approx(1.0).margin(0.01));
    CHECK(Energy(r) == Approx(1.0).margin(0.01));
  }
}

TEST_CASE("wash kills the transient", "[brume_wash]") {
  Rig rig(48000.0f);
  std::vector<float> l, r;
  ImpulseResponse(rig, 16, &l, &r);
  CHECK(std::fabs(l[0]) == Approx(std::pow(0.7, 8)));
}

TEST_CASE("wash is stereo", "[brume_wash]") {
  Rig rig(48000.0f);
  std::vector<float> l, r;
  ImpulseResponse(rig, 48000, &l, &r);
  CHECK(l != r);
}

TEST_CASE("amount 0 is bypass", "[brume_wash]") {
  Rig rig(48000.0f);
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  for (int b = 0; b < 100; ++b) {
    clouds::FloatFrame in[10], out[10];
    for (int i = 0; i < 10; ++i) in[i] = out[i] = {noise(rng), noise(rng)};
    rig.wash.Process(out, 10, 0.0f);
    for (int i = 0; i < 10; ++i) {
      REQUIRE(out[i].l == in[i].l);
      REQUIRE(out[i].r == in[i].r);
    }
  }
}

TEST_CASE("no stale audio after sleep", "[brume_wash]") {
  const float rate = 48000.0f;
  Rig rig(rate);
  std::mt19937 rng(9);
  std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
  auto run = [&](float seconds, float amount, bool silent) {
    float peak = 0.0f;
    for (size_t i = 0; i < size_t(seconds * rate); ++i) {
      clouds::FloatFrame f = silent ? clouds::FloatFrame{0.0f, 0.0f}
                                    : clouds::FloatFrame{noise(rng), noise(rng)};
      rig.wash.Process(&f, 1, amount);
      peak = std::max({peak, std::fabs(f.l), std::fabs(f.r)});
    }
    return peak;
  };
  run(1.0f, 1.0f, false);
  run(3.0f, 0.0f, false);
  CHECK(run(1.0f, 1.0f, true) < 1e-3f);
}
