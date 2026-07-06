#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "beads/types.h"
#include "beads/parameters.h"
#include "grain/grain_scheduler.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

// Run the scheduler for the given number of samples with fixed params.
// Returns the number of triggers fired.
static int RunScheduler(GrainScheduler& sched, const BeadsParameters& params, int num_samples) {
    int total = 0;
    int trigger_buf[32];
    for (int i = 0; i < num_samples; ++i) {
        total += sched.Process(params, 1, trigger_buf, 32);
    }
    return total;
}

// ── DensityToRate contract ──────────────────────────────────────────────────

TEST_CASE("DensityToRate: full CCW density (0.0) triggers at the 80 Hz cap in latched mode", "[density_rate]") {
    // Extreme CCW density → maximum grain trigger rate, now capped at 80 Hz.
    GrainScheduler sched;
    sched.Init(kSampleRate);

    BeadsParameters params;
    params.density = 0.0f;          // full CCW
    params.density_cv = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;

    // 2 seconds of audio at 48kHz.  Expected triggers ≈ 80 * 2 = 160.
    int triggers = RunScheduler(sched, params, 96000);
    float measured_rate = static_cast<float>(triggers) / 2.0f;  // Hz

    REQUIRE(measured_rate == Approx(80.0f).margin(4.0f));
}

TEST_CASE("DensityToRate: full CW density (1.0) triggers near the 80 Hz cap average", "[density_rate]") {
    // CW side uses random inter-grain intervals but the average rate should
    // still be ~80 Hz.
    GrainScheduler sched;
    sched.Init(kSampleRate);

    BeadsParameters params;
    params.density = 1.0f;          // full CW
    params.density_cv = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;

    int triggers = RunScheduler(sched, params, 96000);
    float measured_rate = static_cast<float>(triggers) / 2.0f;

    REQUIRE(measured_rate == Approx(80.0f).margin(15.0f));
}

TEST_CASE("DensityToRate: noon density (0.5) produces silence in latched mode", "[density_rate]") {
    GrainScheduler sched;
    sched.Init(kSampleRate);

    BeadsParameters params;
    params.density = 0.5f;
    params.density_cv = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;

    int triggers = RunScheduler(sched, params, 48000);
    REQUIRE(triggers == 0);
}

TEST_CASE("DensityToRate: density CV cannot drive the rate above the 80 Hz cap", "[density_rate]") {
    // eff_density is clamped to [0,1] before the rate mapping, so even a large
    // connected density CV cannot exceed the cap.  This is the regression that
    // retires the old audio-rate/high-rate path.
    GrainScheduler sched;
    sched.Init(kSampleRate);

    BeadsParameters params;
    params.density = 0.5f;              // knob at noon (would be silent alone)
    params.density_cv = 5.0f;           // large CV drives eff_density to 1.0
    params.density_cv_connected = true;
    params.trigger_mode = TriggerMode::kLatched;

    int triggers = RunScheduler(sched, params, 96000);
    float measured_rate = static_cast<float>(triggers) / 2.0f;

    REQUIRE(measured_rate == Approx(80.0f).margin(15.0f));
}

TEST_CASE("DensityToRate: block-size changes preserve long-run trigger count", "[density_rate]") {
    GrainScheduler sched_small;
    GrainScheduler sched_large;
    sched_small.Init(kSampleRate);
    sched_large.Init(kSampleRate);

    BeadsParameters params;
    params.density = 0.010f;
    params.density_cv = 0.0f;
    params.trigger_mode = TriggerMode::kLatched;

    int triggers[32];
    int small_total = 0;
    int large_total = 0;

    for (int i = 0; i < 512; ++i) {
        small_total += sched_small.Process(params, 8, triggers, 32);
    }
    for (int i = 0; i < 64; ++i) {
        large_total += sched_large.Process(params, 64, triggers, 32);
    }

    REQUIRE(small_total > 0);
    REQUIRE(large_total > 0);
    REQUIRE(std::abs(small_total - large_total) <= 1);
}
