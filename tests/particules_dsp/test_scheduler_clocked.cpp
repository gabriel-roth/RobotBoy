#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "particules_dsp/types.h"
#include "particules_dsp/parameters.h"
#include "grain/grain_scheduler.h"

using namespace particules_dsp;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

// Feeds one clock rising-then-falling pulse (2 samples: high then low) and
// returns the number of triggers produced across both samples.
static int FeedClockEdge(GrainScheduler& sched, ParticulesParameters& params,
                          int* trigger_buf, int max_triggers) {
    params.gate = true;
    int count = sched.Process(params, 1, trigger_buf, max_triggers);
    params.gate = false;
    count += sched.Process(params, 1, trigger_buf, max_triggers);
    return count;
}

// ── Basic kClocked behavior: grains land on clock edges ────────────────────

TEST_CASE("GrainScheduler: kClocked at noon (eff_density=0.5) is silent",
          "[scheduler][clocked]") {
    // Noon = silence, matching Particules.md and the free-running knob's
    // "12 o'clock = silence" identity. The default patch (density 0.5, no CV)
    // must not spawn a grain on every clock.
    GrainScheduler sched;
    sched.Init(kSampleRate);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kClocked;
    params.density = 0.5f;  // exactly noon: silence

    int triggers[32];
    int total = 0;
    for (int edge = 0; edge < 10; ++edge) {
        total += FeedClockEdge(sched, params, triggers, 32);
    }

    REQUIRE(total == 0);
}

TEST_CASE("GrainScheduler: kClocked just CCW of noon divides by 16",
          "[scheduler][clocked]") {
    // eff_density = 0.49 -> distance = (0.5-0.49)*2 = 0.02 -> level 0 -> /16.
    // The sparsest division sits immediately CCW of noon (manual: "from 1/16
    // up to 1/1").
    GrainScheduler sched;
    sched.Init(kSampleRate);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kClocked;
    params.density = 0.49f;

    int triggers[32];
    int total = 0;
    int fired_on = 0;
    for (int edge = 1; edge <= 16; ++edge) {
        int fired = FeedClockEdge(sched, params, triggers, 32);
        total += fired;
        if (fired > 0) fired_on = edge;
    }

    REQUIRE(total == 1);       // one grain per 16 clocks
    REQUIRE(fired_on == 16);   // landing on the 16th edge
}

TEST_CASE("GrainScheduler: kClocked fully CCW (eff_density=0.0) fires every clock",
          "[scheduler][clocked]") {
    // distance = 1.0 -> level 4 -> /1: the densest division sits at full CCW.
    GrainScheduler sched;
    sched.Init(kSampleRate);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kClocked;
    params.density = 0.0f;

    int triggers[32];
    int total = 0;
    for (int edge = 0; edge < 10; ++edge) {
        total += FeedClockEdge(sched, params, triggers, 32);
    }

    REQUIRE(total == 10);
}

TEST_CASE("GrainScheduler: kClocked fully CW (eff_density=1.0) fires every clock",
          "[scheduler][clocked]") {
    // Probability branch: (1.0-0.5)*2 = 100% -> every clock spawns a grain.
    GrainScheduler sched;
    sched.Init(kSampleRate);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kClocked;
    params.density = 1.0f;

    int triggers[32];
    int total = 0;
    for (int edge = 0; edge < 10; ++edge) {
        total += FeedClockEdge(sched, params, triggers, 32);
    }

    REQUIRE(total == 10);
}

TEST_CASE("GrainScheduler: kClocked division-by-4 fires every 4th clock edge",
          "[scheduler][clocked]") {
    GrainScheduler sched;
    sched.Init(kSampleRate);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kClocked;
    // eff_density = 0.25 -> distance = (0.5-0.25)*2 = 0.5 -> level = int(2.5) = 2
    // -> division = 1 << (4-2) = 4.
    params.density = 0.25f;

    int triggers[32];
    int total_triggers = 0;
    int edges_fired_on = 0;
    for (int edge = 1; edge <= 8; ++edge) {
        int fired = FeedClockEdge(sched, params, triggers, 32);
        total_triggers += fired;
        if (fired > 0) edges_fired_on = edge;
    }

    // Exactly 2 triggers over 8 clocks (division 4), the first landing
    // exactly on the 4th clock edge.
    REQUIRE(total_triggers == 2);
    REQUIRE(edges_fired_on == 8);
}

// ── Mode-change off-by-one: gated -> clocked must not inherit stale state ──
//
// gate_phase_ is reused across trigger modes: a continuous 0..1 phasor in
// kGated (which the CW-density random branch can re-latch to a value as
// low as -2.0), and an integer clock-division counter in kClocked. Without
// resetting it on a mode change, leftover kGated state corrupts the first
// clock division's count after switching to kClocked.

TEST_CASE("GrainScheduler: switching gated -> clocked resets the division counter",
          "[scheduler][clocked][mode_switch]") {
    GrainScheduler sched;
    sched.Init(kSampleRate);

    // Drive kGated with CW density (> 0.5) so the randomized inter-grain
    // re-latch branch runs and leaves gate_phase_ at a stale, non-zero
    // (possibly negative) value — run long enough that grains actually
    // fire and the re-latch branch executes at least once.
    ParticulesParameters gated;
    gated.trigger_mode = TriggerMode::kGated;
    gated.gate = true;
    gated.density = 0.9f;

    int triggers[32];
    int gated_triggers = 0;
    for (int i = 0; i < 20000; ++i) {
        gated_triggers += sched.Process(gated, 1, triggers, 32);
    }
    REQUIRE(gated_triggers > 0);  // sanity: the re-latch branch actually ran

    // Switch to kClocked with a division of 4 (eff_density = 0.25) and
    // feed a clean clock train. Regardless of whatever gate_phase_ was
    // left at by kGated, the first division-of-4 group of clock edges
    // after the switch must produce exactly one trigger, landing on the
    // 4th edge -- matching a scheduler that started this division fresh.
    ParticulesParameters clocked;
    clocked.trigger_mode = TriggerMode::kClocked;
    clocked.density = 0.25f;

    int total_triggers = 0;
    int edges_fired_on = 0;
    for (int edge = 1; edge <= 8; ++edge) {
        int fired = FeedClockEdge(sched, clocked, triggers, 32);
        total_triggers += fired;
        if (fired > 0 && edges_fired_on == 0) edges_fired_on = edge;
    }

    REQUIRE(edges_fired_on == 4);
    REQUIRE(total_triggers == 2);  // clocks 4 and 8
}
