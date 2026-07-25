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

// Particules: drop automatic triggers at the long-grain cap floor.
// Spec: docs/superpowers/specs/2026-07-25-particules-longgrain-trigger-drop-design.md
//
// At SIZE = 1.0 the grain duration is ~the full buffer duration, so
// GrainEngine::Process's cached_max_active_ = buf_dur/grain_dur * 1.5
// truncates to 1 and clamps to its floor of 2 (see the existing CPU-cap
// saturation test in test_grain_kill.cpp). Below this change, every
// subsequent trigger at that cap steals the oldest grain and starts a new
// one -- for buffer-length grains this means constant mid-grain truncation
// churn. The fix: automatic (droppable) triggers -- kLatched phasor ticks,
// kGated held-repeat ticks -- are silently dropped instead of stealing once
// cached_max_active_ == 2. Manual triggers (kGated rising edge, all
// kClocked ticks, kMidi) are untouched and still steal.

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

// ── (c) Below the cap floor (mid size), saturation still steals exactly as
//        before -- no behavior change ─────────────────────────────────────

TEST_CASE("GrainEngine: automatic triggers still steal when saturated above the cap floor",
          "[engine][longgrain][drop]") {
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
    params.size = 0.5f;     // mid-size: on an 8s buffer this settles
                             // cached_max_active_ well above the long-grain
                             // floor of 2 (grain duration well under half
                             // the buffer -- see grain_engine.cpp's cap
                             // formula), so the drop guard never engages.
    params.density = 0.5f;  // silent during the startup-ramp burn.

    std::vector<StereoFrame> block(64);
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    params.density = 0.0f;  // deterministic max-rate latched phasor.

    // Run enough automatic ticks to saturate the dynamic cap. 30000 samples
    // is ~80 trigger periods at the C3 rate -- comfortably more than any
    // plausible cap for this size, and far short of this grain size's
    // duration (well under a second), so nothing finishes naturally yet.
    const int kSettleSamples = 30000;
    for (int i = 0; i < kSettleSamples; ++i) {
        engine.Process(params, block.data(), 1);
    }
    int cap = engine.ActiveGrainCount();
    REQUIRE(cap > 2);           // NOT at the long-grain cap floor
    REQUIRE(cap < kMaxGrains);  // the dynamic cap, not the full pool

    uint32_t max_serial_before = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i)) {
            max_serial_before = std::max(max_serial_before, engine.SpawnSerialAt(i));
        }
    }

    // A few more trigger periods: the saturation branch must still steal
    // (unchanged pre-existing behavior) -- active count stays pinned at
    // cap, but new, higher spawn serials keep appearing. This is the
    // opposite of test (a) above, where serials freeze once dropping
    // engages.
    const int kMoreSamples = 400 * 5;
    for (int i = 0; i < kMoreSamples; ++i) {
        engine.Process(params, block.data(), 1);
    }

    REQUIRE(engine.ActiveGrainCount() == cap);
    uint32_t max_serial_after = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i)) {
            max_serial_after = std::max(max_serial_after, engine.SpawnSerialAt(i));
        }
    }
    REQUIRE(max_serial_after > max_serial_before);
}
