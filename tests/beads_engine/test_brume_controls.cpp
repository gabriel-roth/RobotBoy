// Brume's knob laws and modulation math (host/brume_controls.h).
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <limits>

#include "host/brume_controls.h"

using namespace robotboy_beads;
using Catch::Approx;

TEST_CASE("tank rates", "[brume_controls]") {
  REQUIRE(BrumeTankRate(BrumeQuality::kBright) == 48000.0f);
  REQUIRE(BrumeTankRate(BrumeQuality::kCold) == 32000.0f);
  REQUIRE(BrumeTankRate(BrumeQuality::kScorched) == 24000.0f);
}

TEST_CASE("send, width and dry/wet laws", "[brume_controls]") {
  REQUIRE(SendGain(0.0f) == 0.0f);
  REQUIRE(SendGain(0.5f) == Approx(1.0f));
  REQUIRE(SendGain(1.0f) == Approx(2.0f));
  REQUIRE(WidthGain(0.5f) == Approx(1.0f));
  REQUIRE(WidthGain(0.0f) == 0.0f);
  REQUIRE(DryGain(0.0f) == 1.0f);
  REQUIRE(DryGain(1.0f) == 0.0f);
  REQUIRE(WetGain(0.0f) == 0.0f);
  REQUIRE(WetGain(1.0f) == 1.0f);
  REQUIRE(DryGain(0.5f) == Approx(0.75f));
  REQUIRE(WetGain(0.5f) == Approx(0.75f));
}

TEST_CASE("decay law per mode", "[brume_controls]") {
  REQUIRE(DecayTime(0.0f, BrumeMode::kBeads) == Approx(0.30f));
  REQUIRE(DecayTime(0.95f, BrumeMode::kBeads) == Approx(0.95f));
  REQUIRE(DecayTime(0.97f, BrumeMode::kBeads) == 1.0f);
  REQUIRE(DecayTime(0.0f, BrumeMode::kClouds) == Approx(0.35f));
  REQUIRE(DecayTime(0.95f, BrumeMode::kClouds) == Approx(0.98f));
  REQUIRE(DecayTime(0.5f, BrumeMode::kClouds) ==
          Approx(0.35f + 0.63f * 0.5f / 0.95f));
  REQUIRE(DecayTime(0.97f, BrumeMode::kClouds) == 1.0f);
  REQUIRE(DecayTime(1.0f, BrumeMode::kClouds) == 1.0f);
}

TEST_CASE("tank character laws", "[brume_controls]") {
  REQUIRE(DampLp(0.0f) == Approx(1.0f));
  REQUIRE(DampLp(1.0f) == Approx(0.3f));
  REQUIRE(DampLp(4.0f / 7.0f) == Approx(0.6f));
  REQUIRE(DiffuseCoefficient(0.0f) == Approx(0.3f));
  REQUIRE(DiffuseCoefficient(1.0f) == Approx(0.8f));
  REQUIRE(DiffuseCoefficient(0.65f) == Approx(0.625f));
  REQUIRE(SpeedMultiplier(0.0f) == Approx(0.25f));
  REQUIRE(SpeedMultiplier(0.5f) == Approx(1.0f));
  REQUIRE(SpeedMultiplier(1.0f) == Approx(4.0f));
}

TEST_CASE("shimmer law", "[brume_controls]") {
  REQUIRE(ShimmerGain(0.0f) == 0.0f);
  REQUIRE(ShimmerGain(0.475f) == Approx(0.25f * kShimmerSustainGain));
  REQUIRE(ShimmerGain(0.97f) == Approx(1.15f * kShimmerSustainGain));
  REQUIRE(ShimmerGain(1.0f) == Approx(1.15f * kShimmerSustainGain));
}

TEST_CASE("interval law", "[brume_controls]") {
  REQUIRE(IntervalSemitones(0.0f) == Approx(-24.0f));
  REQUIRE(IntervalSemitones(0.5f) == Approx(0.0f));
  REQUIRE(IntervalSemitones(0.75f) == Approx(12.0f));
  REQUIRE(IntervalSemitones(1.0f) == Approx(24.0f));
}

TEST_CASE("filter law", "[brume_controls]") {
  const FilterSettings mid = FilterForKnob(0.5f);
  REQUIRE(mid.lp == Approx(0.499f));
  REQUIRE(mid.hp == Approx(0.25f * std::pow(2.0f, -9.0f)).epsilon(0.01));
  REQUIRE(mid.lpq == Approx(1.003f).margin(0.001));

  const FilterSettings ccw = FilterForKnob(0.0f);
  REQUIRE(ccw.lp == Approx(0.5f * std::pow(2.0f, -9.0f)).epsilon(0.01));
  REQUIRE(ccw.hp == Approx(0.25f * std::pow(2.0f, -9.0f)).epsilon(0.01));
  REQUIRE(ccw.lpq > 1.4f);

  const FilterSettings cw = FilterForKnob(1.0f);
  REQUIRE(cw.lp == Approx(0.499f));
  REQUIRE(cw.hp == Approx(0.25f));
}

TEST_CASE("knob modulation", "[brume_controls]") {
  REQUIRE(ModulatedKnob(0.3f, false, 0.0f, 0.0f, 0.8f) == Approx(0.3f));
  REQUIRE(ModulatedKnob(0.3f, true, 0.0f, 0.0f, 0.8f) == Approx(0.3f));
  // A patched jack ignores the envelope.
  REQUIRE(ModulatedKnob(0.3f, true, 0.0f, 1.0f, 1.0f) == Approx(0.3f));
  REQUIRE(ModulatedKnob(0.5f, true, 5.0f, 1.0f, 0.0f) == 1.0f);
  REQUIRE(ModulatedKnob(0.5f, true, 1.0f, 1.0f, 0.0f) == Approx(0.7f));
  REQUIRE(ModulatedKnob(0.5f, true, 1.0f, -1.0f, 0.0f) == Approx(0.3f));
  REQUIRE(ModulatedKnob(0.5f, false, 0.0f, -1.0f, 1.0f) == 0.0f);
  REQUIRE(ModulatedKnob(0.5f, false, 0.0f, 0.5f, 0.4f) == Approx(0.7f));
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  REQUIRE(ModulatedKnob(0.4f, true, nan, 1.0f, 0.0f) == Approx(0.4f));
  REQUIRE(ModulatedKnob(0.4f, true, inf, 1.0f, 0.0f) == Approx(0.4f));
}

TEST_CASE("interval modulation", "[brume_controls]") {
  REQUIRE(ModulatedInterval(12.0f, false, 0.0f, 0.0f, 1.0f) == Approx(12.0f));
  REQUIRE(ModulatedInterval(12.0f, true, 1.0f, 1.0f, 0.0f) == Approx(24.0f));
  REQUIRE(ModulatedInterval(12.0f, true, 1.0f, 1.0f, 1.0f) == Approx(24.0f));
  REQUIRE(ModulatedInterval(0.0f, false, 0.0f, 1.0f, 0.5f) == Approx(12.0f));
  REQUIRE(ModulatedInterval(24.0f, true, 10.0f, 1.0f, 0.0f) == 48.0f);
  REQUIRE(ModulatedInterval(-24.0f, true, -10.0f, 1.0f, 0.0f) == -48.0f);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  REQUIRE(ModulatedInterval(7.0f, true, nan, 1.0f, 0.0f) == Approx(7.0f));
}

TEST_CASE("input normalization", "[brume_controls]") {
  BrumeInput in = NormalizeInputs(true, false, 0.3f, 0.9f);
  REQUIRE(in.l == 0.3f);
  REQUIRE(in.r == 0.3f);
  in = NormalizeInputs(false, true, 0.3f, 0.9f);
  REQUIRE(in.l == 0.9f);
  REQUIRE(in.r == 0.9f);
  in = NormalizeInputs(true, true, 0.3f, 0.9f);
  REQUIRE(in.l == 0.3f);
  REQUIRE(in.r == 0.9f);
  in = NormalizeInputs(false, false, 0.3f, 0.9f);
  REQUIRE(in.l == 0.0f);
  REQUIRE(in.r == 0.0f);
}
