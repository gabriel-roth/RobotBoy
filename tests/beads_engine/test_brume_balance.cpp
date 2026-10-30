// Brume's dry/wet balance light (host/balance_meter.h).
#include <catch2/catch_amalgamated.hpp>

#include <cmath>

#include "host/balance_meter.h"

using robotboy_beads::BalanceColor;
using robotboy_beads::BalanceMeter;

namespace {

// Mean square of a level in dB re 1 (5 V).
float Ms(float db) { return std::pow(10.0f, db / 10.0f); }

}  // namespace

TEST_CASE("equal levels are white", "[brume_balance]") {
  float rgb[3];
  BalanceColor(Ms(-6.0f), Ms(-6.0f), rgb);
  for (float c : rgb) REQUIRE(c == Catch::Approx(1.0f));
  // Still white within the even zone.
  BalanceColor(Ms(-6.0f), Ms(-7.0f), rgb);
  for (float c : rgb) REQUIRE(c == Catch::Approx(1.0f));
}

TEST_CASE("louder dry is amber, louder wet is cyan", "[brume_balance]") {
  float rgb[3];
  BalanceColor(Ms(-6.0f), Ms(-30.0f), rgb);
  REQUIRE(rgb[0] == Catch::Approx(1.0f));
  REQUIRE(rgb[1] == Catch::Approx(0.55f));
  REQUIRE(rgb[2] == Catch::Approx(0.0f).margin(1e-6));
  BalanceColor(Ms(-30.0f), Ms(-6.0f), rgb);
  REQUIRE(rgb[0] == Catch::Approx(0.0f).margin(1e-6));
  REQUIRE(rgb[1] == Catch::Approx(0.8f));
  REQUIRE(rgb[2] == Catch::Approx(1.0f));
  // Only one side at all.
  BalanceColor(0.0f, Ms(-6.0f), rgb);
  REQUIRE(rgb[0] == Catch::Approx(0.0f).margin(1e-6));
}

TEST_CASE("blend is partial between the zones", "[brume_balance]") {
  float rgb[3];
  BalanceColor(Ms(-12.75f), Ms(-6.0f), rgb);  // halfway: 6.75 dB
  REQUIRE(rgb[0] == Catch::Approx(0.5f));
  REQUIRE(rgb[2] == Catch::Approx(1.0f));
}

TEST_CASE("brightness follows the louder part", "[brume_balance]") {
  float rgb[3];
  BalanceColor(0.0f, 0.0f, rgb);
  for (float c : rgb) REQUIRE(c == 0.0f);
  BalanceColor(Ms(-60.0f), Ms(-60.0f), rgb);
  for (float c : rgb) REQUIRE(c == 0.0f);
  BalanceColor(Ms(-36.0f), Ms(-36.0f), rgb);
  for (float c : rgb) REQUIRE(c == Catch::Approx(0.5f));
}

TEST_CASE("meter smooths over about 100 ms", "[brume_balance]") {
  BalanceMeter m;
  m.Init(48000.0f);
  for (int i = 0; i < 4800; ++i) m.Process(0.5f, 0.5f, 0.0f, 0.0f);
  REQUIRE(m.dry() == Catch::Approx(0.25f * (1.0f - std::exp(-1.0f))).epsilon(0.01));
  for (int i = 0; i < 48000; ++i) m.Process(0.5f, 0.5f, 0.0f, 0.0f);
  float rgb[3];
  m.Color(rgb);
  REQUIRE(rgb[0] == Catch::Approx(1.0f));
  REQUIRE(rgb[2] == Catch::Approx(0.0f).margin(1e-6));
}
