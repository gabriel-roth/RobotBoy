// Reverb sleep (vendor patch): with Reverb at exactly 0 and the tail decayed
// to exact zeros, Reverb::Process skips the tank. Every test runs a sleeping
// reverb next to one with sleep turned off and requires identical output.
#include <catch2/catch_amalgamated.hpp>

#include <cstdint>
#include <random>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

#include "beads/dsp/frame.h"
#include "beads/dsp/fx/reverb.h"

using beads::FloatFrame;
using beads::Reverb;

namespace {

// Flush-to-zero for the current thread while in scope, as VCV Rack sets it
// (rack::system::resetFpuFlags sets FPCR.FZ on ARM64).
class FlushToZero {
 public:
  explicit FlushToZero(bool on) {
#if defined(__aarch64__)
    asm volatile("mrs %0, fpcr" : "=r"(saved_));
    uint64_t flags = on ? (saved_ | (1ull << 24)) : (saved_ & ~(1ull << 24));
    asm volatile("msr fpcr, %0" : : "r"(flags));
#elif defined(__x86_64__) || defined(__i386__)
    saved_ = _mm_getcsr();
    _mm_setcsr(on ? (saved_ | 0x8040) : (saved_ & ~0x8040u));
#else
    (void)on;
#endif
  }
  ~FlushToZero() {
#if defined(__aarch64__)
    asm volatile("msr fpcr, %0" : : "r"(saved_));
#elif defined(__x86_64__) || defined(__i386__)
    _mm_setcsr(static_cast<unsigned>(saved_));
#endif
  }

 private:
  uint64_t saved_ = 0;
};

// Processor::Process's reverb settings for each Quality setting.
struct Media {
  const char* name;
  float sample_rate;
  bool caveman;
  float lp;
};

const Media kMedia[] = {
    {"Hot (digital)", 48000.0f, false, 0.6f},
    {"Cold (vintage digital)", 32000.0f, false, 0.75f},
    {"Tape", 24000.0f, true, 0.4f},
};

// A sleeping reverb and a reference with sleep turned off, fed the same
// blocks. Fails on the first output sample that differs.
struct Pair {
  const Media& media;
  std::vector<uint16_t> memory[2] = {std::vector<uint16_t>(65536),
                                     std::vector<uint16_t>(65536)};
  Reverb reverb[2];
  std::mt19937 rng{1234};
  size_t block_index = 0;
  int asleep_blocks = 0;
  int samples_compared = 0;

  explicit Pair(const Media& m) : media(m) {
    for (int i = 0; i < 2; ++i) {
      reverb[i].Init(memory[i].data(), 48000.0f);
      reverb[i].Reset(media.sample_rate);
    }
    reverb[1].set_sleep_enabled(false);
  }

  void Reset() {
    for (Reverb& r : reverb) r.Reset(media.sample_rate);
  }

  // One block at Reverb `amount`, with noise at `level` (0 = silence).
  void Block(float amount, float level) {
    static const size_t kSizes[] = {32, 16, 32, 7, 32, 24};
    const size_t size = kSizes[block_index++ % 6];
    std::uniform_real_distribution<float> noise(-level, level);
    FloatFrame frames[2][32];
    for (size_t i = 0; i < size; ++i) {
      frames[0][i].l = frames[1][i].l = level > 0.0f ? noise(rng) : 0.0f;
      frames[0][i].r = frames[1][i].r = level > 0.0f ? noise(rng) : 0.0f;
    }
    const float mix = amount * amount * 0.9f;
    const float t = amount * amount * amount;
    for (int k = 0; k < 2; ++k) {
      reverb[k].set_mix(mix);
      reverb[k].set_time(media.caveman ? (0.45f + 0.5f * t)
                                       : (0.3f + 0.6f * t));
      reverb[k].set_lp(media.lp);
      reverb[k].Process(frames[k], size);
    }
    if (reverb[0].asleep()) ++asleep_blocks;
    REQUIRE_FALSE(reverb[1].asleep());
    for (size_t i = 0; i < size; ++i) {
      if (frames[0][i].l != frames[1][i].l ||
          frames[0][i].r != frames[1][i].r) {
        FAIL(media.name << ": output differs at sample " << samples_compared
             << (reverb[0].asleep() ? " (asleep)" : " (awake)") << ": ("
             << std::hexfloat << frames[0][i].l << ", " << frames[0][i].r
             << ") vs (" << frames[1][i].l << ", " << frames[1][i].r << ")");
      }
      ++samples_compared;
    }
  }

  void Hold(float seconds, float amount, float level) {
    const int blocks = static_cast<int>(seconds * media.sample_rate / 24.0f);
    for (int b = 0; b < blocks; ++b) Block(amount, level);
  }

  void Ramp(float seconds, float from, float to, float level) {
    const int blocks = static_cast<int>(seconds * media.sample_rate / 24.0f);
    for (int b = 0; b < blocks; ++b) {
      Block(from + (to - from) * (b + 1) / blocks, level);
    }
  }

  bool asleep() const { return reverb[0].asleep(); }
};

}  // namespace

TEST_CASE("Reverb sleep: starts asleep at 0 and matches the full path",
          "[reverb_sleep]") {
  for (bool ftz : {true, false}) {
    FlushToZero guard(ftz);
    for (const Media& media : kMedia) {
      Pair pair(media);
      pair.Block(0.0f, 0.5f);
      REQUIRE(pair.asleep());
      pair.Hold(1.0f, 0.0f, 0.5f);
      REQUIRE(pair.asleep());
    }
  }
}

TEST_CASE("Reverb sleep: bit-identical through turn-down, sleep and wake",
          "[reverb_sleep]") {
  for (bool ftz : {true, false}) {
    FlushToZero guard(ftz);
    for (const Media& media : kMedia) {
      INFO(media.name << (ftz ? ", flush-to-zero" : ", no flush-to-zero"));
      Pair pair(media);

      // A wet sound, then the knob turned down while the input keeps playing.
      pair.Hold(2.0f, 0.8f, 0.5f);
      REQUIRE_FALSE(pair.asleep());
      pair.Ramp(0.3f, 0.8f, 0.0f, 0.5f);
      REQUIRE_FALSE(pair.asleep());  // the tail is still ringing
      pair.Hold(8.0f, 0.0f, 0.5f);
      if (ftz) REQUIRE(pair.asleep());

      // Wake with the input playing, then cut to 0 in one step over silence.
      pair.Ramp(0.1f, 0.0f, 0.5f, 0.5f);
      REQUIRE_FALSE(pair.asleep());
      pair.Hold(2.0f, 0.5f, 0.5f);
      pair.Hold(8.0f, 0.0f, 0.0f);
      if (ftz) REQUIRE(pair.asleep());

      // A quiet burst through a barely open reverb, then back to 0.
      pair.Hold(0.5f, 0.3f, 0.05f);
      pair.Hold(8.0f, 0.0f, 0.2f);
      if (ftz) REQUIRE(pair.asleep());
      pair.Hold(1.0f, 1.0f, 0.5f);
      // Without flush-to-zero the Tape filter states stick at the smallest
      // denormal and the reverb never sleeps (it still matches, of course).
      if (ftz || !media.caveman) {
        REQUIRE(pair.asleep_blocks > 0);
      } else {
        REQUIRE(pair.asleep_blocks == 0);
      }
    }
  }
}

TEST_CASE("Reverb sleep: survives a Quality change (Reset) while asleep",
          "[reverb_sleep]") {
  FlushToZero guard(true);
  for (const Media& media : kMedia) {
    INFO(media.name);
    Pair pair(media);
    pair.Hold(1.0f, 0.7f, 0.5f);
    pair.Hold(8.0f, 0.0f, 0.5f);
    REQUIRE(pair.asleep());
    pair.Reset();  // dry, wet and send glide back from 0
    pair.Hold(0.5f, 0.0f, 0.5f);
    REQUIRE(pair.asleep());
    pair.Hold(1.0f, 0.6f, 0.5f);
    REQUIRE_FALSE(pair.asleep());
  }
}

TEST_CASE("Reverb sleep: never sleeps unless Reverb is exactly 0",
          "[reverb_sleep]") {
  FlushToZero guard(true);
  for (const Media& media : kMedia) {
    INFO(media.name);
    Pair pair(media);
    pair.Hold(1.0f, 0.7f, 0.5f);
    pair.Hold(8.0f, 0.001f, 0.5f);
    REQUIRE(pair.asleep_blocks == 0);
  }
}
