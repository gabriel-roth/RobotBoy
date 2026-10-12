// Ondes runs Beads' WavetableVoice: tuning, level and bank crossfades.
#include <catch2/catch_amalgamated.hpp>

#include <cmath>
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
