// Brume's envelope follower with Beads-style auto-gain (host/envelope_follower.h).
#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>

#include "host/envelope_follower.h"

using robotboy_beads::EnvelopeFollower;
using robotboy_beads::FollowerTime;

namespace {

const float kRate = 48000.0f;

void Setup(EnvelopeFollower& f, bool auto_gain, float rise, float fall) {
  f.Init(kRate);
  f.set_times(rise, fall);
  f.set_auto_gain(auto_gain);
}

float Run(EnvelopeFollower& f, float in, float seconds) {
  float out = 0.0f;
  const size_t n = size_t(seconds * kRate);
  for (size_t i = 0; i < n; ++i) out = f.Process(in);
  return out;
}

}  // namespace

TEST_CASE("follower time law", "[brume_follower]") {
  REQUIRE(FollowerTime(0.0f) == Catch::Approx(0.01f));
  REQUIRE(FollowerTime(1.0f) == Catch::Approx(5.0f));
}

TEST_CASE("rise and fall time constants", "[brume_follower]") {
  EnvelopeFollower f;
  Setup(f, false, 0.1f, 0.5f);
  size_t i = 0;
  while (f.Process(1.0f) < 1.0f - std::exp(-1.0f)) ++i;
  REQUIRE(float(i) / kRate == Catch::Approx(0.1f).epsilon(0.1));
  Run(f, 1.0f, 2.0f);
  i = 0;
  while (f.Process(0.0f) > std::exp(-1.0f)) ++i;
  REQUIRE(float(i) / kRate == Catch::Approx(0.5f).epsilon(0.1));
}

TEST_CASE("auto-gain off is raw scaling", "[brume_follower]") {
  EnvelopeFollower f;
  Setup(f, false, 0.01f, 0.01f);
  REQUIRE(Run(f, 0.5f, 2.0f) == Catch::Approx(0.5f).margin(0.01));
  EnvelopeFollower g;
  Setup(g, false, 0.01f, 0.01f);
  REQUIRE(Run(g, 3.0f, 2.0f) == 1.0f);
}

TEST_CASE("auto-gain normalizes a quiet source", "[brume_follower]") {
  EnvelopeFollower f;
  Setup(f, true, 0.01f, 0.01f);
  const float out = Run(f, 0.1f, 10.0f);
  REQUIRE(out >= 0.85f);
  REQUIRE(out <= 1.0f);
}

TEST_CASE("first second is unscaled", "[brume_follower]") {
  EnvelopeFollower f;
  Setup(f, true, 0.01f, 0.01f);
  REQUIRE(Run(f, 0.1f, 0.9f) == Catch::Approx(0.1f).margin(0.02));
}

TEST_CASE("Restart relearns", "[brume_follower]") {
  EnvelopeFollower f;
  Setup(f, true, 0.01f, 0.01f);
  Run(f, 0.1f, 10.0f);
  f.Restart();
  float out = 0.0f;
  for (size_t i = 0; i < size_t(10.0f * kRate); ++i) {
    out = f.Process(0.5f);
    REQUIRE(out <= 1.0f);
  }
  REQUIRE(out >= 0.85f);
}

TEST_CASE("switching auto-gain back on restarts learning", "[brume_follower]") {
  EnvelopeFollower f;
  Setup(f, true, 0.01f, 0.01f);
  Run(f, 0.1f, 10.0f);
  f.set_auto_gain(false);
  REQUIRE(Run(f, 0.1f, 1.0f) == Catch::Approx(0.1f).margin(0.01));
  f.set_auto_gain(true);
  REQUIRE(Run(f, 0.1f, 0.9f) == Catch::Approx(0.1f).margin(0.02));
}

TEST_CASE("silence does not blow up", "[brume_follower]") {
  EnvelopeFollower f;
  Setup(f, true, 0.01f, 0.01f);
  float peak = 0.0f;
  for (size_t i = 0; i < size_t(10.0f * kRate); ++i) {
    peak = std::max(peak, f.Process(1e-6f));
  }
  REQUIRE(peak <= 1e-4f);
}
