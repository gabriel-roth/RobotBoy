#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

#include "particules_dsp/types.h"
#include "particules_dsp/parameters.h"
#include "buffer/recording_buffer.h"
#include "grain/grain.h"
#include "grain/grain_engine.h"

// Particules: drop automatic triggers at any saturated grain cap (originally
// only at the cap floor, later generalized to every cap).
//
// When a trigger finds the grain pool saturated (dynamic CPU cap, ramped
// startup cap, or full pool), automatic (droppable) triggers -- kLatched
// phasor ticks, kGated held-repeat ticks -- are silently dropped instead
// of stealing, at ANY cap value, so playing grains always finish their
// envelopes. Manual triggers (kGated rising edge, all kClocked ticks,
// kMidi) still steal the oldest grain so performed events always sound.

using namespace particules_dsp;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

// ── (a) At the cap floor, automatic (kLatched) ticks are dropped ──────────

TEST_CASE("GrainEngine: automatic latched triggers are dropped at the long-grain cap floor",
          "[engine][longgrain][drop]") {
    const size_t num_frames = 48000 * 8;  // 8s buffer, same recipe as the
                                           // existing cap-floor steal test.
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);  // DC: a pending kill (if one wrongly
                                    // occurred) could never resolve via the
                                    // zero-crossing path, keeping it
                                    // observably pending -- makes the
                                    // per-sample PendingKillAt check below
                                    // maximally sensitive.
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.size = 1.0f;     // cap floor: cached_max_active_ settles at 2.
    params.density = 0.5f;  // noon = silence, while burning the startup ramp.

    std::vector<StereoFrame> block(64);

    // Burn down the ~1s startup ramp with density silent so no grains spawn
    // yet. size=1.0 throughout means cached_max_active_ is already pinned
    // to its floor of 2 for the whole ramp (max_active = min(cached, ramp)
    // = min(2, >=2) = 2), so the ramp has no separate effect here.
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    // density=0.0: full CCW -> deterministic (non-randomized) latched
    // phasor at the max rate (~130.81 Hz, period ~367 samples @ 48kHz).
    params.density = 0.0f;

    // Run one sample at a time until exactly 2 grains are active -- the
    // ordinary, untouched allocation path (active_before < max_active).
    const int kSearchBudget = 48000;
    int s = 0;
    for (; s < kSearchBudget && engine.ActiveGrainCount() < 2; ++s) {
        engine.Process(params, block.data(), 1);
    }
    REQUIRE(engine.ActiveGrainCount() == 2);

    // Record which two slots hold the grains and their spawn serials.
    int idx_a = -1, idx_b = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i)) {
            if (idx_a < 0) idx_a = i; else idx_b = i;
        }
    }
    REQUIRE(idx_a >= 0);
    REQUIRE(idx_b >= 0);
    uint32_t serial_a = engine.SpawnSerialAt(idx_a);
    uint32_t serial_b = engine.SpawnSerialAt(idx_b);

    // Run several more trigger periods (~10) worth of samples, one sample
    // at a time. Every subsequent latched tick must be dropped: no
    // pending-kill is ever observed (checked after every single sample, so
    // even a transient click-free steal that resolves within one block
    // would be caught), no new spawn, no active-count change.
    const int kMoreSamples = 400 * 10;  // > 367-sample period * 10, margin
    for (int i = 0; i < kMoreSamples; ++i) {
        engine.Process(params, block.data(), 1);
        for (int g = 0; g < kMaxGrains; ++g) {
            REQUIRE_FALSE(engine.PendingKillAt(g));
        }
    }

    REQUIRE(engine.ActiveGrainCount() == 2);
    REQUIRE(engine.SpawnSerialAt(idx_a) == serial_a);
    REQUIRE(engine.SpawnSerialAt(idx_b) == serial_b);
}

// ── (b) Same cap-floor state, but a manual (kGated rising-edge) trigger
//        must still steal ────────────────────────────────────────────────

TEST_CASE("GrainEngine: a manual gate rising edge still steals at the long-grain cap floor",
          "[engine][longgrain][drop][steal]") {
    const size_t num_frames = 48000 * 8;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);  // DC: keeps the click-free fade
                                    // observably pending within the
                                    // 16-sample overflow block below.
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.size = 1.0f;     // cap floor.
    params.density = 0.5f;  // silent during the startup-ramp burn.

    std::vector<StereoFrame> block(64);
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    params.density = 0.0f;
    const int kSearchBudget = 48000;
    int s = 0;
    for (; s < kSearchBudget && engine.ActiveGrainCount() < 2; ++s) {
        engine.Process(params, block.data(), 1);
    }
    REQUIRE(engine.ActiveGrainCount() == 2);

    uint32_t oldest_serial = 0; int oldest_index = -1;
    uint32_t max_serial_before = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!engine.ActiveAt(i)) continue;
        uint32_t serial = engine.SpawnSerialAt(i);
        max_serial_before = std::max(max_serial_before, serial);
        if (oldest_index < 0 || serial < oldest_serial) {
            oldest_serial = serial;
            oldest_index = i;
        }
    }
    REQUIRE(oldest_index >= 0);

    // Switch to kGated and issue a rising-edge trigger. Manual triggers are
    // never droppable, so even at the cap floor this must steal exactly as
    // before the change. A 16-sample block (shorter than the 32-sample
    // zero-crossing deadline) catches the victim mid-fade, same technique
    // as the existing CPU-cap saturation steal test.
    params.trigger_mode = TriggerMode::kGated;
    params.gate = true;
    engine.Process(params, block.data(), 16);
    params.gate = false;

    REQUIRE(engine.ActiveAt(oldest_index));
    REQUIRE(engine.PendingKillAt(oldest_index));
    REQUIRE(engine.SpawnSerialAt(oldest_index) == oldest_serial);

    int pending_kill_count = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.PendingKillAt(i)) ++pending_kill_count;
    }
    REQUIRE(pending_kill_count == 1);

    int new_grain_index = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i) && engine.SpawnSerialAt(i) > max_serial_before) {
            new_grain_index = i;
        }
    }
    REQUIRE(new_grain_index >= 0);

    // Transient cap+1: the victim is still active (pending-kill) alongside
    // the surviving original grain and the new one.
    REQUIRE(engine.ActiveGrainCount() == 3);
}

// ── (c) Below the cap floor (mid size), saturated automatic ticks drop too ──

TEST_CASE("GrainEngine: automatic triggers are dropped when saturated above the cap floor",
          "[engine][longgrain][drop]") {
    const size_t num_frames = 48000 * 8;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);  // DC: a pending kill (if one wrongly
                                    // occurred) could never resolve via the
                                    // zero-crossing path, keeping the
                                    // per-sample PendingKillAt sweep below
                                    // maximally sensitive.
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.size = 0.5f;     // mid-size: on an 8s buffer the cap settles well
                             // above the floor of 2 and below kMaxGrains
                             // (~0.78s grains, cap ~15 -- see the cap
                             // formula in grain_engine.cpp).
    params.density = 0.5f;  // silent during the startup-ramp burn.

    std::vector<StereoFrame> block(64);
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    params.density = 0.0f;  // deterministic max-rate latched phasor.

    // Fill the pool to the dynamic cap through the ordinary allocation
    // path (active_before < max_active). 30000 samples is ~80 trigger
    // periods at the C3 rate -- far more than the cap -- and still well
    // short of this size's ~37440-sample grain duration, so nothing has
    // finished naturally by the end of the assert window below.
    const int kSettleSamples = 30000;
    for (int i = 0; i < kSettleSamples; ++i) {
        engine.Process(params, block.data(), 1);
    }
    int cap = engine.ActiveGrainCount();
    REQUIRE(cap > 2);           // NOT the long-grain cap floor
    REQUIRE(cap < kMaxGrains);  // the dynamic cap, not the full pool

    uint32_t max_serial_before = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i)) {
            max_serial_before = std::max(max_serial_before, engine.SpawnSerialAt(i));
        }
    }

    // A few more trigger periods: every saturated automatic tick must be
    // dropped -- no pending-kill ever observed (checked after every single
    // sample, so even a steal fade that resolves within one block would be
    // caught), no new spawn serial, active count pinned at cap.
    const int kMoreSamples = 400 * 5;
    for (int i = 0; i < kMoreSamples; ++i) {
        engine.Process(params, block.data(), 1);
        for (int g = 0; g < kMaxGrains; ++g) {
            REQUIRE_FALSE(engine.PendingKillAt(g));
        }
    }

    REQUIRE(engine.ActiveGrainCount() == cap);
    uint32_t max_serial_after = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i)) {
            max_serial_after = std::max(max_serial_after, engine.SpawnSerialAt(i));
        }
    }
    REQUIRE(max_serial_after == max_serial_before);
}

// ── (e) Startup ramp: saturated automatic ticks drop during the first
//        second too (the 2026-07-25 ramp carve-out is superseded) ─────────

TEST_CASE("GrainEngine: automatic triggers drop against the ramped cap during startup",
          "[engine][longgrain][drop][ramp]") {
    const size_t num_frames = 48000 * 8;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.size = 0.5f;     // mid-size: the settled cap is ~15, so right
                             // after Init the SLEWED cap (seeded at 2,
                             // rising 28 grains/s -- int value 2 until
                             // sample ~1714) is the binding limit --
                             // exactly the case the superseded 2026-07-25
                             // carve-out kept stealing.
    params.density = 0.0f;  // max-rate ticks from sample 0 -- no slew burn.

    std::vector<StereoFrame> block(64);

    // Ticks arrive every ~367 samples, so the pool saturates at 2 grains
    // around sample ~734 and a couple more ticks hit the saturated branch
    // inside the window. Stop at 1500 (margin below the slew's first step
    // to 3 at ~1714 = 48000/28). Grain duration ~37440 samples: nothing
    // dies naturally in the window.
    const int kWindow = 1500;
    uint32_t serial_a = 0, serial_b = 0;
    bool serials_recorded = false;
    for (int i = 0; i < kWindow; ++i) {
        engine.Process(params, block.data(), 1);
        for (int g = 0; g < kMaxGrains; ++g) {
            REQUIRE_FALSE(engine.PendingKillAt(g));  // never a steal
        }
        REQUIRE(engine.ActiveGrainCount() <= 2);     // never past the ramped cap
        if (!serials_recorded && engine.ActiveGrainCount() == 2) {
            int idx = 0;
            for (int g = 0; g < kMaxGrains; ++g) {
                if (engine.ActiveAt(g)) {
                    if (idx++ == 0) serial_a = engine.SpawnSerialAt(g);
                    else            serial_b = engine.SpawnSerialAt(g);
                }
            }
            serials_recorded = true;
        }
    }

    REQUIRE(serials_recorded);
    REQUIRE(engine.ActiveGrainCount() == 2);
    uint32_t max_serial = 0;
    for (int g = 0; g < kMaxGrains; ++g) {
        if (engine.ActiveAt(g)) max_serial = std::max(max_serial, engine.SpawnSerialAt(g));
    }
    REQUIRE(max_serial == std::max(serial_a, serial_b));
}

// ── (h) Upward cap slew: a fast Size drop refills the pool at ~28
//        grains/s, not instantly (2026-07-26 spec addendum) ──────────────

TEST_CASE("GrainEngine: cap slew bounds grain refill after a fast Size drop",
          "[engine][longgrain][slew]") {
    const size_t num_frames = 48000 * 8;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.size = 1.0f;     // cap floor: slew target (and value) settle at 2.
    params.density = 0.5f;  // silent while any startup transient passes.

    std::vector<StereoFrame> block(64);
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    // Snap the knob from the cap floor to mid-size (settled cap ~15 on an
    // 8s buffer) with max-rate ticks. The slew starts at 2 and rises 28/s,
    // so 0.25s after the snap the effective cap is ~2 + 7 = 9; without the
    // slew the pool refills to ~15 within ~5500 samples.
    params.size = 0.5f;
    params.density = 0.0f;
    for (int i = 0; i < 12000; ++i) {
        engine.Process(params, block.data(), 1);
    }
    REQUIRE(engine.ActiveGrainCount() <= 10);

    // After 1.5s total the slew has long since reached the ~15 target and
    // the pool sits at the settled cap (14 tolerates a just-died grain
    // awaiting its next refill tick).
    for (int i = 0; i < 60000; ++i) {
        engine.Process(params, block.data(), 1);
    }
    REQUIRE(engine.ActiveGrainCount() >= 14);
}

// ── (f) Manual triggers still steal at a saturated mid-size cap ───────────

TEST_CASE("GrainEngine: a gate rising edge still steals at a saturated mid-size cap",
          "[engine][longgrain][steal]") {
    const size_t num_frames = 48000 * 8;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);  // DC: keeps the click-free fade
                                    // observably pending within the
                                    // 16-sample block below.
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.size = 0.5f;     // mid-size cap ~15 (see test (c)).
    params.density = 0.5f;  // silent during the startup-ramp burn.

    std::vector<StereoFrame> block(64);
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    // Saturate the dynamic cap with automatic ticks (they drop once full).
    params.density = 0.0f;
    const int kSettleSamples = 30000;
    for (int i = 0; i < kSettleSamples; ++i) {
        engine.Process(params, block.data(), 1);
    }
    int cap = engine.ActiveGrainCount();
    REQUIRE(cap > 2);
    REQUIRE(cap < kMaxGrains);

    uint32_t oldest_serial = 0; int oldest_index = -1;
    uint32_t max_serial_before = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!engine.ActiveAt(i)) continue;
        uint32_t serial = engine.SpawnSerialAt(i);
        max_serial_before = std::max(max_serial_before, serial);
        if (oldest_index < 0 || serial < oldest_serial) {
            oldest_serial = serial;
            oldest_index = i;
        }
    }
    REQUIRE(oldest_index >= 0);

    // Manual rising edge: never droppable, must steal exactly as before.
    // 16-sample block (< the 32-sample zero-crossing deadline) catches the
    // victim mid-fade, same technique as the cap-floor steal test (b).
    params.trigger_mode = TriggerMode::kGated;
    params.gate = true;
    engine.Process(params, block.data(), 16);
    params.gate = false;

    REQUIRE(engine.ActiveAt(oldest_index));
    REQUIRE(engine.PendingKillAt(oldest_index));
    REQUIRE(engine.SpawnSerialAt(oldest_index) == oldest_serial);

    int pending_kill_count = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.PendingKillAt(i)) ++pending_kill_count;
    }
    REQUIRE(pending_kill_count == 1);

    int new_grain_index = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i) && engine.SpawnSerialAt(i) > max_serial_before) {
            new_grain_index = i;
        }
    }
    REQUIRE(new_grain_index >= 0);

    // Transient cap+1: victim still active (pending-kill) alongside the
    // new grain.
    REQUIRE(engine.ActiveGrainCount() == cap + 1);
}

// ── (g) Clock ticks are manual too: still steal at a saturated mid cap ────

TEST_CASE("GrainEngine: a clock tick still steals at a saturated mid-size cap",
          "[engine][longgrain][steal]") {
    const size_t num_frames = 48000 * 8;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.size = 0.5f;
    params.density = 0.5f;

    std::vector<StereoFrame> block(64);
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    params.density = 0.0f;
    const int kSettleSamples = 30000;
    for (int i = 0; i < kSettleSamples; ++i) {
        engine.Process(params, block.data(), 1);
    }
    int cap = engine.ActiveGrainCount();
    REQUIRE(cap > 2);
    REQUIRE(cap < kMaxGrains);

    uint32_t oldest_serial = 0; int oldest_index = -1;
    uint32_t max_serial_before = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!engine.ActiveAt(i)) continue;
        uint32_t serial = engine.SpawnSerialAt(i);
        max_serial_before = std::max(max_serial_before, serial);
        if (oldest_index < 0 || serial < oldest_serial) {
            oldest_serial = serial;
            oldest_index = i;
        }
    }
    REQUIRE(oldest_index >= 0);

    // kClocked at density 0.0 = /1 division: every clock tick fires, and
    // clocked ticks are always manual (dropping would skip beats).
    params.trigger_mode = TriggerMode::kClocked;
    params.gate = true;
    engine.Process(params, block.data(), 16);
    params.gate = false;

    REQUIRE(engine.ActiveAt(oldest_index));
    REQUIRE(engine.PendingKillAt(oldest_index));

    int new_grain_index = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i) && engine.SpawnSerialAt(i) > max_serial_before) {
            new_grain_index = i;
        }
    }
    REQUIRE(new_grain_index >= 0);
    REQUIRE(engine.ActiveGrainCount() == cap + 1);
}
