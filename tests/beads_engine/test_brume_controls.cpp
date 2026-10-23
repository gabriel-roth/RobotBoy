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
  REQUIRE(DampLp(0.5f) == Approx(0.6f));
  REQUIRE(DampLp(1.0f) == Approx(0.15f));
  REQUIRE(DiffuseCoefficient(0.0f) == Approx(0.3f));
  REQUIRE(DiffuseCoefficient(0.5f) == Approx(0.625f));
  REQUIRE(DiffuseCoefficient(1.0f) == Approx(0.9f));
  REQUIRE(SpeedMultiplier(0.0f) == Approx(0.1f));
  REQUIRE(SpeedMultiplier(0.25f) == Approx(0.1f * std::sqrt(10.0f)));
  REQUIRE(SpeedMultiplier(0.5f) == Approx(1.0f));
  REQUIRE(SpeedMultiplier(0.75f) == Approx(4.0f));
  REQUIRE(SpeedMultiplier(1.0f) == Approx(16.0f));
  REQUIRE(DepthMultiplier(0.0f) == 0.0f);
  REQUIRE(DepthMultiplier(kDepthDefault) == Approx(1.0f));
  REQUIRE(DepthMultiplier(1.0f) == Approx(4.0f));
}

TEST_CASE("tank laws are continuous and monotonic at noon", "[brume_controls]") {
  const float below = 0.5f - 1e-5f, above = 0.5f + 1e-5f;
  REQUIRE(DampLp(below) == Approx(DampLp(above)).epsilon(1e-3));
  REQUIRE(DiffuseCoefficient(below) == Approx(DiffuseCoefficient(above)).epsilon(1e-3));
  REQUIRE(SpeedMultiplier(below) == Approx(SpeedMultiplier(above)).epsilon(1e-3));
  float damp = DampLp(0.0f), diffuse = DiffuseCoefficient(0.0f);
  float speed = SpeedMultiplier(0.0f);
  for (int i = 1; i <= 100; i++) {
    const float v = i / 100.0f;
    REQUIRE(DampLp(v) < damp);
    REQUIRE(DiffuseCoefficient(v) > diffuse);
    REQUIRE(SpeedMultiplier(v) > speed);
    damp = DampLp(v);
    diffuse = DiffuseCoefficient(v);
    speed = SpeedMultiplier(v);
  }
}

TEST_CASE("defaults are Beads' original values at noon", "[brume_controls]") {
  REQUIRE(kSpeedDefault == 0.5f);
  REQUIRE(kDiffuseDefault == 0.5f);
  REQUIRE(kDampDefault == 0.5f);
  REQUIRE(kDepthDefault == 0.5f);
  REQUIRE(SpeedMultiplier(kSpeedDefault) == Approx(1.0f));
  REQUIRE(DiffuseCoefficient(kDiffuseDefault) == Approx(0.625f));
  REQUIRE(DampLp(kDampDefault) == Approx(0.6f));
}

TEST_CASE("smear law", "[brume_controls]") {
  SmearSettings s = SmearForKnob(0.0f);
  REQUIRE(s.amount == 0.0f);
  REQUIRE(s.coefficient == Approx(0.625f));
  s = SmearForKnob(0.35f);
  REQUIRE(s.amount == Approx(0.5f));
  REQUIRE(s.coefficient == Approx(0.625f));
  s = SmearForKnob(0.7f);
  REQUIRE(s.amount == Approx(1.0f));
  REQUIRE(s.coefficient == Approx(0.625f));
  s = SmearForKnob(0.85f);
  REQUIRE(s.amount == 1.0f);
  REQUIRE(s.coefficient == Approx(0.7125f));
  s = SmearForKnob(1.0f);
  REQUIRE(s.amount == 1.0f);
  REQUIRE(s.coefficient == Approx(0.8f));
}

TEST_CASE("wow law", "[brume_controls]") {
  REQUIRE(WowAmount(0.0f) == 0.0f);
  REQUIRE(WowAmount(std::sqrt(1.0f / 3.0f)) == Approx(1.0f));
  REQUIRE(WowAmount(1.0f) == Approx(3.0f));
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

// Noon is exactly 1, so the default leaves the tanks' offsets whole.
TEST_CASE("size and pre-delay laws", "[brume_controls]") {
  REQUIRE(SizeScale(0.0f) == Approx(0.5f));
  REQUIRE(SizeScale(kSizeDefault) == 1.0f);
  REQUIRE(SizeScale(0.25f) == Approx(0.5f * std::sqrt(2.0f)));
  REQUIRE(SizeScale(1.0f) == Approx(2.0f));
  REQUIRE(kSizeDefault == 0.5f);
  REQUIRE(PredelaySeconds(0.0f) == 0.0f);
  REQUIRE(PredelaySeconds(kPredelayDefault) == 0.0f);
  REQUIRE(PredelaySeconds(0.5f) == Approx(0.125f));
  REQUIRE(PredelaySeconds(1.0f) == Approx(0.5f));
  REQUIRE(kMaxPredelaySeconds == 0.5f);
}
