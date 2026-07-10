#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "beads/types.h"
#include "fx/reverb.h"
#include "fx/saturation.h"
#include "quality/quality_processor.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

TEST_CASE("Reverb: Init and process without crash", "[reverb]") {
    std::vector<float> reverb_mem(kReverbBufferSize, 0.0f);

    Reverb rev;
    rev.Init(reverb_mem.data(), kReverbBufferSize, kSampleRate);
    rev.SetAmount(0.5f);
    rev.SetDecay(0.5f);

    // Feed an impulse
    float out_l, out_r;
    rev.Process(1.0f, 1.0f, &out_l, &out_r);

    // Process silence for a while
    float max_tail = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        rev.Process(0.0f, 0.0f, &out_l, &out_r);
        max_tail = std::max(max_tail, std::max(std::abs(out_l), std::abs(out_r)));
    }

    // Reverb tail should be non-zero after impulse
    REQUIRE(max_tail > 0.001f);
}

TEST_CASE("Reverb: undersized buffer disables instead of crashing", "[reverb]") {
    // Deliberately smaller than kMinBufferSize: Init() must not allocate any
    // of the 12 delay lines against this buffer (they'd otherwise partially
    // fall back to nullptr and Process() would write through it).
    std::vector<float> tiny_mem(Reverb::kMinBufferSize / 2, 0.0f);

    Reverb rev;
    rev.Init(tiny_mem.data(), tiny_mem.size(), kSampleRate);
    rev.SetAmount(1.0f);
    rev.SetDecay(0.95f);

    float out_l = 1.0f, out_r = 1.0f;
    for (int i = 0; i < 1000; ++i) {
        float input = (i == 0) ? 1.0f : 0.0f;
        rev.Process(input, input, &out_l, &out_r);   // must not crash
        REQUIRE(out_l == 0.0f);
        REQUIRE(out_r == 0.0f);
    }
}

TEST_CASE("Reverb: null buffer disables instead of crashing", "[reverb]") {
    Reverb rev;
    rev.Init(nullptr, Reverb::kMinBufferSize, kSampleRate);

    float out_l, out_r;
    rev.Process(1.0f, 1.0f, &out_l, &out_r);   // must not crash
    REQUIRE(out_l == 0.0f);
    REQUIRE(out_r == 0.0f);
}

TEST_CASE("Reverb: Dry signal passes when amount is 0", "[reverb]") {
    std::vector<float> reverb_mem(kReverbBufferSize, 0.0f);

    Reverb rev;
    rev.Init(reverb_mem.data(), kReverbBufferSize, kSampleRate);
    rev.SetAmount(0.0f);
    rev.SetDecay(0.5f);

    float out_l, out_r;
    // With amount=0, output should be the dry signal
    rev.Process(0.5f, -0.3f, &out_l, &out_r);
    // The reverb implementation may still add some wet signal even at amount=0
    // depending on implementation, so just check it doesn't crash
    REQUIRE(std::isfinite(out_l));
    REQUIRE(std::isfinite(out_r));
}

TEST_CASE("Reverb: High decay produces longer tail", "[reverb]") {
    std::vector<float> reverb_mem_short(kReverbBufferSize, 0.0f);
    std::vector<float> reverb_mem_long(kReverbBufferSize, 0.0f);

    Reverb rev_short, rev_long;
    rev_short.Init(reverb_mem_short.data(), kReverbBufferSize, kSampleRate);
    rev_long.Init(reverb_mem_long.data(), kReverbBufferSize, kSampleRate);

    rev_short.SetAmount(1.0f);
    rev_short.SetDecay(0.2f);
    rev_long.SetAmount(1.0f);
    rev_long.SetDecay(0.9f);

    float out_l, out_r;

    // Feed impulse to both
    rev_short.Process(1.0f, 1.0f, &out_l, &out_r);
    rev_long.Process(1.0f, 1.0f, &out_l, &out_r);

    // Process enough silence for feedback to recirculate through the tank.
    // Total tank round-trip is ~8000+ samples, so we need significantly more.
    float energy_short = 0.0f, energy_long = 0.0f;
    for (int i = 0; i < 24000; ++i) {
        rev_short.Process(0.0f, 0.0f, &out_l, &out_r);
        energy_short += out_l * out_l + out_r * out_r;

        rev_long.Process(0.0f, 0.0f, &out_l, &out_r);
        energy_long += out_l * out_l + out_r * out_r;
    }

    // Longer decay should have more energy in the tail
    REQUIRE(energy_long > energy_short);
}

TEST_CASE("Reverb: No NaN in output", "[reverb]") {
    std::vector<float> reverb_mem(kReverbBufferSize, 0.0f);

    Reverb rev;
    rev.Init(reverb_mem.data(), kReverbBufferSize, kSampleRate);
    rev.SetAmount(1.0f);
    rev.SetDecay(0.95f);

    float out_l, out_r;
    // Feed various inputs
    for (int i = 0; i < 10000; ++i) {
        float input = (i == 0) ? 1.0f : 0.0f;
        rev.Process(input, input, &out_l, &out_r);
        REQUIRE(std::isfinite(out_l));
        REQUIRE(std::isfinite(out_r));
    }
}

TEST_CASE("Saturation: HiFi hard clips at 1.0", "[saturation]") {
    Saturation sat;
    sat.Init();

    REQUIRE(sat.Process(0.5f, QualityMode::kHiFi) == Approx(0.5f));
    REQUIRE(sat.Process(1.5f, QualityMode::kHiFi) == Approx(1.0f));
    REQUIRE(sat.Process(-1.5f, QualityMode::kHiFi) == Approx(-1.0f));
}

TEST_CASE("Saturation: Clouds soft clips", "[saturation]") {
    Saturation sat;
    sat.Init();

    float result = sat.Process(2.0f, QualityMode::kClouds);
    // Should be less than 2.0 (saturated) but more than 0
    REQUIRE(result < 2.0f);
    REQUIRE(result > 0.5f);
}

TEST_CASE("Saturation: Feedback limiting prevents runaway", "[saturation]") {
    Saturation sat;
    sat.Init();

    for (int mode = 0; mode < 4; ++mode) {
        QualityMode qm = static_cast<QualityMode>(mode);
        float limited = sat.LimitFeedback(10.0f, qm);
        REQUIRE(std::abs(limited) <= 2.0f);
    }
}

TEST_CASE("QualityProcessor: HiFi passthrough", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    StereoFrame in = {0.5f, -0.3f};
    StereoFrame out = qp.ProcessInput(in, QualityMode::kHiFi);
    REQUIRE(out.l == Approx(0.5f));
    REQUIRE(out.r == Approx(-0.3f));
}

TEST_CASE("QualityProcessor: Tape mode pitch modulation", "[quality]") {
    QualityProcessor qp;
    qp.Init(kSampleRate);

    // Advance the LFO by calling GetPitchModulation many times
    // At 48kHz, wow LFO at 0.5Hz needs ~48000 samples for one cycle
    // After ~24000 samples (quarter cycle), modulation should be significant
    float mod = 1.0f;
    for (int i = 0; i < 24000; ++i) {
        mod = qp.GetPitchModulation(QualityMode::kTape);
    }
    // After advancing, modulation should deviate from 1.0
    // wow: 0.02 semitones peak = SemitonesToRatio(0.02) ≈ 1.0012
    REQUIRE(mod != Approx(1.0f).margin(0.001f));
    REQUIRE(mod > 0.95f);
    REQUIRE(mod < 1.05f);
}

TEST_CASE("Reverb: makeup gain scales the wet tail", "[reverb]") {
    auto tail_energy = [](float makeup) {
        std::vector<float> mem(kReverbBufferSize, 0.0f);
        Reverb rev;
        rev.Init(mem.data(), kReverbBufferSize, kSampleRate);
        rev.SetAmount(1.0f);
        rev.SetDecay(0.5f);
        rev.SetMakeupGain(makeup);
        float l, r, e = 0.0f;
        rev.Process(1.0f, 1.0f, &l, &r);              // impulse
        for (int i = 0; i < 8000; ++i) {
            rev.Process(0.0f, 0.0f, &l, &r);
            e += l * l + r * r;
        }
        return e;
    };
    // Tail energy scales with gain^2, so doubling makeup ~= 4x energy.
    REQUIRE(tail_energy(2.0f) > tail_energy(1.0f) * 3.0f);
}

TEST_CASE("Reverb: equal-power crossfade keeps dry level up at mid mix", "[reverb]") {
    std::vector<float> mem(kReverbBufferSize, 0.0f);
    Reverb rev;
    rev.Init(mem.data(), kReverbBufferSize, kSampleRate);
    rev.SetAmount(0.5f);
    rev.SetDecay(0.5f);
    rev.SetMakeupGain(1.0f);

    float l, r;
    rev.Process(1.0f, 1.0f, &l, &r);   // first sample: tank empty, wet ~ 0
    // Equal-power dry gain at 50% is cos(pi/4) ~= 0.707 (a linear blend gives 0.5).
    REQUIRE(l == Approx(0.70710678f).margin(0.02f));
}

TEST_CASE("Reverb: amount 0 is exact dry passthrough", "[reverb]") {
    std::vector<float> mem(kReverbBufferSize, 0.0f);
    Reverb rev;
    rev.Init(mem.data(), kReverbBufferSize, kSampleRate);
    rev.SetAmount(0.0f);
    rev.SetDecay(0.5f);
    rev.SetMakeupGain(2.0f);   // makeup must not leak through when fully dry

    float l, r;
    rev.Process(0.5f, -0.3f, &l, &r);
    REQUIRE(l == Approx(0.5f));
    REQUIRE(r == Approx(-0.3f));
}

TEST_CASE("Reverb: sleeps after amount sits at 0 and the tail decays", "[reverb][sleep]") {
    std::vector<float> buffer(beads::Reverb::kMinBufferSize + 1000, 0.0f);
    beads::Reverb rv;
    rv.Init(buffer.data(), buffer.size(), 48000.0f);
    rv.SetDecay(0.5f);
    rv.SetDiffusion(0.7f);
    rv.SetMakeupGain(1.5849f);

    // Build a tail.
    rv.SetAmount(0.5f);
    float l, r;
    rv.Process(1.0f, 1.0f, &l, &r);
    for (int i = 0; i < 4800; ++i) rv.Process(0.0f, 0.0f, &l, &r);

    // Turn amount to 0: not asleep yet (hold period).
    rv.SetAmount(0.0f);
    REQUIRE(!rv.IsAsleep());

    // A second of silence at amount 0: tail decays below -80 dBFS and the
    // 250 ms hold elapses.
    for (int i = 0; i < 96000; ++i) rv.Process(0.0f, 0.0f, &l, &r);
    REQUIRE(rv.IsAsleep());

    // Asleep passthrough is exact.
    rv.Process(0.3f, -0.2f, &l, &r);
    REQUIRE(l == 0.3f);
    REQUIRE(r == -0.2f);
    REQUIRE(rv.IsAsleep());   // input while asleep does not wake it
}

TEST_CASE("Reverb: brief amount=0 dip keeps the tail ringing", "[reverb][sleep]") {
    std::vector<float> buffer(beads::Reverb::kMinBufferSize + 1000, 0.0f);
    beads::Reverb rv;
    rv.Init(buffer.data(), buffer.size(), 48000.0f);
    rv.SetDecay(0.9f);
    rv.SetDiffusion(0.7f);
    rv.SetMakeupGain(1.5849f);

    rv.SetAmount(0.8f);
    float l, r;
    rv.Process(1.0f, 1.0f, &l, &r);
    for (int i = 0; i < 2400; ++i) rv.Process(0.0f, 0.0f, &l, &r);

    // 50 ms at amount 0 — well inside the 250 ms hold.
    rv.SetAmount(0.0f);
    for (int i = 0; i < 2400; ++i) rv.Process(0.0f, 0.0f, &l, &r);
    REQUIRE(!rv.IsAsleep());

    // Back up: the tail is still in the tank.
    rv.SetAmount(0.8f);
    float energy = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        rv.Process(0.0f, 0.0f, &l, &r);
        energy += l * l + r * r;
    }
    REQUIRE(energy > 1e-6f);
}

TEST_CASE("Reverb: wake from sleep is clean and functional", "[reverb][sleep]") {
    std::vector<float> buffer(beads::Reverb::kMinBufferSize + 1000, 0.0f);
    beads::Reverb rv;
    rv.Init(buffer.data(), buffer.size(), 48000.0f);
    rv.SetDecay(0.5f);
    rv.SetDiffusion(0.7f);
    rv.SetMakeupGain(1.5849f);

    // Sleep it.
    rv.SetAmount(0.5f);
    float l, r;
    rv.Process(1.0f, 1.0f, &l, &r);
    rv.SetAmount(0.0f);
    for (int i = 0; i < 96000; ++i) rv.Process(0.0f, 0.0f, &l, &r);
    REQUIRE(rv.IsAsleep());

    // Wake: no stale tail (state was flushed), no NaN.
    rv.SetAmount(0.7f);
    REQUIRE(!rv.IsAsleep());
    for (int i = 0; i < 4800; ++i) {
        rv.Process(0.0f, 0.0f, &l, &r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        REQUIRE(std::fabs(l) < 1e-5f);
        REQUIRE(std::fabs(r) < 1e-5f);
    }

    // And it still reverberates.
    rv.Process(1.0f, 1.0f, &l, &r);
    float energy = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        rv.Process(0.0f, 0.0f, &l, &r);
        energy += l * l + r * r;
    }
    REQUIRE(energy > 1e-6f);
}
