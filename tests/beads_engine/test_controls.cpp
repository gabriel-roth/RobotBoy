// Beads knob laws, CV conditioning, Lock-pitch quantizer and gain control.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
#include <vector>

#include "host/controls.h"
#include "host/gain_control.h"
#include "host/pitch_lock.h"

using namespace robotboy_beads;
using Catch::Matchers::WithinAbs;

TEST_CASE("Detune law: end stops, centre and notches") {
  CHECK_THAT(DetuneLaw(0.0f), WithinAbs(-24.0f, 1e-4));
  CHECK_THAT(DetuneLaw(0.5f), WithinAbs(0.0f, 1e-6));
  CHECK_THAT(DetuneLaw(1.0f), WithinAbs(24.0f, 1e-4));
  // Each sixth of a half-turn starts with a flat third: 7 semitones holds
  // from 4/6 to 4/6 + 1/18 of the clockwise half.
  const float seventh_start = 0.5f + 0.5f * (4.0f / 6.0f) + 0.001f;
  const float seventh_end = 0.5f + 0.5f * (4.0f / 6.0f + 1.0f / 18.0f) - 0.001f;
  CHECK_THAT(DetuneLaw(seventh_start), WithinAbs(7.0f, 1e-4));
  CHECK_THAT(DetuneLaw(seventh_end), WithinAbs(7.0f, 1e-4));
  CHECK_THAT(DetuneLaw(1.0f - seventh_start), WithinAbs(-7.0f, 1e-4));
  float previous = -100.0f;
  for (int i = 0; i <= 1000; ++i) {  // monotonic
    const float v = DetuneLaw(i / 1000.0f);
    CHECK(v >= previous - 1e-5f);
    previous = v;
  }
}

TEST_CASE("Inverse detune law round-trips") {
  for (float st : {-24.0f, -12.0f, -7.0f, -2.5f, 0.0f, 0.4f, 3.0f, 9.5f, 24.0f}) {
    CAPTURE(st);
    CHECK_THAT(DetuneLaw(InverseDetuneLaw(st)), WithinAbs(st, 0.01));
  }
  CHECK_THAT(InverseDetuneLaw(0.0f), WithinAbs(0.5f, 1e-4));  // middle of notch
}

TEST_CASE("Attenuverter law: end stops and dead band") {
  CHECK_THAT(AttenuverterLaw(0.0f), WithinAbs(-1.0f, 1e-5));
  CHECK_THAT(AttenuverterLaw(1.0f), WithinAbs(1.0f, 1e-5));
  CHECK(AttenuverterLaw(0.5f) == 0.0f);
  CHECK(AttenuverterLaw(0.52f) == 0.0f);
  CHECK(AttenuverterLaw(0.6f) > 0.0f);
}

TEST_CASE("CV channel clamps and rejects non-finite input") {
  CvChannel pitch;
  InitPitchCv(&pitch);
  CHECK(pitch.Process(1000.0f * 12.0f) == 60.0f);
  CHECK(pitch.Process(NAN) == 0.0f);
  CHECK(pitch.Process(-INFINITY) == 0.0f);
  CvChannel time;
  InitTimeSizeShapeCv(&time);
  for (int i = 0; i < 100; ++i) time.Process(10.0f * 0.2f);
  CHECK_THAT(time.value(), WithinAbs(1.6f, 1e-6));
}

TEST_CASE("Pot channel starts at its first reading, then smooths") {
  PotChannel pot;
  pot.Init(PotLaw::kNormal, kPotLp, 0.0f);
  CHECK(pot.Process(0.8f) == 0.8f);
  const float after = pot.Process(0.0f);
  CHECK_THAT(after, WithinAbs(0.8f * 0.95f, 1e-6));
}

TEST_CASE("Mix parameter: knob scaling, attenuverted CV, clamping") {
  CHECK(MixParameter(1.0f, 0.0f, 0.0f) == 1.0f);
  CHECK_THAT(MixParameter(0.2f, 2.5f, 0.5f), WithinAbs(0.201f + 0.25f, 1e-5));
  CHECK(MixParameter(0.5f, NAN, 1.0f) == 0.5025f);
  CHECK(MixParameter(0.2f, -10.0f, 1.0f) == 0.0f);
}

TEST_CASE("Pitch lock modes") {
  ScalePitchLock lock;
  lock.Set(particules::kPitchOff, 0);
  CHECK_FALSE(lock.active());
  CHECK(lock.hook() == nullptr);

  lock.Set(particules::kPitchOctaves, 5);  // root ignored for octaves
  CHECK(lock.active());
  CHECK(lock.Quantize(5.9f) == 0.0f);
  CHECK(lock.Quantize(6.1f) == 12.0f);
  CHECK(lock.Quantize(-17.0f) == -12.0f);

  lock.Set(particules::kPitchOctavesFifths, 0);
  CHECK(lock.Quantize(4.0f) == 7.0f);
  CHECK(lock.Quantize(-4.0f) == -5.0f);   // the fifth below the octave
  CHECK(lock.Quantize(10.0f) == 12.0f);

  lock.Set(particules::kPitchChromatic, 0);
  CHECK(lock.Quantize(3.4f) == 3.0f);
  CHECK(lock.Quantize(-3.6f) == -4.0f);

  lock.Set(particules::kPitchMajor, 0);   // C major: 0 2 4 5 7 9 11
  CHECK(lock.Quantize(1.2f) == 2.0f);
  CHECK(lock.Quantize(6.2f) == 7.0f);
  CHECK(lock.Quantize(-1.2f) == -1.0f);   // B below

  lock.Set(particules::kPitchMajor, 2);   // D major: C# is in, C is not
  CHECK(lock.Quantize(0.2f) == 1.0f);
  CHECK(lock.Quantize(NAN) == 0.0f);

  lock.Set(particules::kPitchMinorPentatonic, 9);  // A minor pentatonic
  for (float st = -30.0f; st <= 30.0f; st += 0.25f) {
    const int q = int(lock.Quantize(st));
    const int degree = (((q - 9) % 12) + 12) % 12;
    CAPTURE(st, q);
    CHECK((degree == 0 || degree == 3 || degree == 5 || degree == 7 || degree == 10));
    CHECK(std::fabs(q - st) <= 2.0f);
  }
}

TEST_CASE("Gain control: auto gain raises quiet input, manual gain is fixed") {
  const float sr = 48000.0f;
  const size_t block = 12;
  std::vector<beads::FloatFrame> in(block), out(block);

  GainControl agc;
  agc.Init(sr, GainControl::SOFT_CLIPPING_MODE_OFF, true);
  float phase = 0.0f;
  for (int b = 0; b < int(7.0f * 4000.0f); ++b) {  // past the 6 s learning
    for (auto& f : in) {
      phase += 220.0f / sr;
      f.l = f.r = 0.1f * std::sin(2.0f * float(M_PI) * phase);
    }
    agc.Process(true, 0, false, false, in.data(), out.data(), block);
  }
  CHECK_THAT(agc.gain(), WithinAbs(9.0f, 0.5f));  // 0.9 / 0.1 peak

  GainControl manual;
  manual.Init(sr, GainControl::SOFT_CLIPPING_MODE_OFF, true);
  for (int b = 0; b < 4000; ++b) {
    manual.Process(true, 1, false, false, in.data(), out.data(), block);
  }
  CHECK_THAT(manual.gain(), WithinAbs(1.0f, 0.01f));

  // Right jack unpatched: the left signal feeds both channels.
  in[0].l = 0.3f; in[0].r = 0.0f;
  manual.Process(true, 1, false, true, in.data(), out.data(), 1);
  CHECK(out[0].r == out[0].l);
}
