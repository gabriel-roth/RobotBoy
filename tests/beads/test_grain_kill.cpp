#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "beads/types.h"
#include "beads/parameters.h"
#include "buffer/recording_buffer.h"
#include "grain/grain.h"
#include "grain/grain_engine.h"

using namespace beads;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;

// ── (a) Kill-fallback fade must reach exactly zero ─────────────────────────
//
// When a grain can't find a zero crossing within kZeroCrossDeadline (32
// samples), it falls back to a linear fade-out over kFallbackFadeSamples
// (4) samples. The old fade formula (fade = counter/kFallbackFadeSamples,
// applied before decrementing) emits 1.0, 0.75, 0.5, 0.25 and then hard-
// cuts to silence — a 0.25-amplitude step, audible as a click. The fixed
// formula must emit a sequence that actually reaches 0 as its last value.

TEST_CASE("Grain: kill fallback fade reaches exactly zero", "[grain][kill]") {
    // DC buffer: every read returns the same constant value, so the mono
    // (L+R) sum never changes sign — the zero-crossing kill can never
    // fire on its own, guaranteeing the fallback fade engages after
    // kZeroCrossDeadline samples of no crossing.
    const size_t num_frames = 4800;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(1.0f, 1.0f);
    }

    Grain g;
    g.Init();

    Grain::GrainParameters params;
    params.position = 100.0f;
    params.size = 5000.0f;  // long relative to the ~80-sample window we
                             // exercise below, so the envelope's flat
                             // plateau (steepness clips it to exactly 1.0
                             // once phase clears ~0.025) stays effectively
                             // constant across the whole window.
    params.pitch_ratio = 1.0f;
    params.shape = 0.0f;    // rectangle-ish: envelope clips to a flat
                             // plateau near full gain away from the very
                             // start/end of the grain.
    params.pan = 0.0f;
    params.pre_delay = 0;
    g.Start(params);

    float buf_size_f = static_cast<float>(buffer.size());
    float out_l, out_r;

    // Run well past the initial envelope ramp (phase clears the ~0.025
    // clipping threshold by sample ~50 at this grain size) into the flat
    // plateau, and capture an unfaded reference amplitude.
    for (int i = 0; i < 200; ++i) {
        REQUIRE(g.Process(buffer, buf_size_f, &out_l, &out_r));
    }
    float reference = out_l;
    REQUIRE(reference > 0.1f);  // sanity: plateau really is near full gain

    g.StartPendingKill();

    // Advance through the zero-crossing deadline (32 samples of "no
    // crossing"); the fallback fade should engage right after this.
    for (int i = 0; i < 32; ++i) {
        REQUIRE(g.Process(buffer, buf_size_f, &out_l, &out_r));
    }

    // The next kFallbackFadeSamples (4) samples are the fallback fade.
    std::vector<float> fade_ratios;
    for (int i = 0; i < 4; ++i) {
        bool alive = g.Process(buffer, buf_size_f, &out_l, &out_r);
        REQUIRE(alive);
        fade_ratios.push_back(out_l / reference);
    }

    REQUIRE(fade_ratios.size() == 4);
    REQUIRE(fade_ratios[0] == Approx(0.75f).margin(1e-4f));
    REQUIRE(fade_ratios[1] == Approx(0.5f).margin(1e-4f));
    REQUIRE(fade_ratios[2] == Approx(0.25f).margin(1e-4f));
    REQUIRE(fade_ratios[3] == Approx(0.0f).margin(1e-4f));

    // The grain must be dead on the very next sample — no further
    // discontinuity beyond the fade sequence above.
    bool still_alive = g.Process(buffer, buf_size_f, &out_l, &out_r);
    REQUIRE_FALSE(still_alive);
    REQUIRE(out_l == 0.0f);
    REQUIRE(out_r == 0.0f);
}

// ── (b) Kill-oldest must kill the true oldest grain, not array slot 0 ──────
//
// AllocateGrain's full-pool fallback used to mark the first non-pending
// grain *by array index*. Array index does not track spawn order: once an
// early grain finishes, its slot is freed and gets reused by a later
// (newer) grain, so a low array index can end up holding the newest grain
// in the pool. The fix tracks a monotonically increasing spawn_serial_
// per grain and picks the lowest-serial (truly oldest) active grain.

TEST_CASE("GrainEngine: kill-fallback victim is the true oldest grain, not array slot 0",
          "[engine][kill]") {
    const size_t num_frames = 48000 * 8;  // 8s buffer: plenty of headroom
                                           // so the dynamic max-active-grain
                                           // cap comfortably reaches kMaxGrains
                                           // for the "long" grain size used
                                           // below.
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    BeadsParameters params;
    params.trigger_mode = TriggerMode::kGated;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.density = 0.5f;  // noon = rate 0, only the rising-edge grain
                             // fires per gate pulse.
    params.gate = false;

    std::vector<StereoFrame> block(64);

    // Burn down the ~1s startup ramp (which otherwise caps active grain
    // count well below kMaxGrains) before doing anything else.
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }

    auto fire_one_grain = [&](float size) {
        params.size = size;
        params.gate = true;
        engine.Process(params, block.data(), 64);
        params.gate = false;
        engine.Process(params, block.data(), 64);
    };

    // Grain #0 (spawn_serial 1): short (~120ms ~= 5800 samples) — long
    // enough to still be active once all 30 grains have been fired
    // (fill phase = 30 * 128 = 3840 samples), but short enough to finish
    // during the subsequent 40-block wait below.
    fire_one_grain(0.1f);

    // Grains #1..#29 (serials 2..30): long grains, fill the remaining
    // slots while grain #0 is still active.
    for (int i = 0; i < kMaxGrains - 1; ++i) {
        fire_one_grain(0.2f);
    }
    REQUIRE(engine.ActiveGrainCount() == kMaxGrains);

    // Let grain #0 finish (30ms ~= 1440 samples) and free slot 0, while
    // the long grains (#1..#29) are still far from done.
    for (int i = 0; i < 40; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == kMaxGrains - 1);

    // Grain #30 (spawn_serial 31) reuses the now-free slot 0. It is the
    // *newest* grain in the pool but sits at the lowest array index —
    // exactly the mismatch the old array-index-based fallback got wrong.
    fire_one_grain(0.2f);
    REQUIRE(engine.ActiveGrainCount() == kMaxGrains);

    // Determine the true oldest active grain (lowest spawn_serial) before
    // forcing a kill, for comparison against whichever slot actually gets
    // marked pending_kill below.
    int true_oldest_index = -1;
    uint32_t true_oldest_serial = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!engine.ActiveAt(i)) continue;
        if (true_oldest_index < 0 || engine.SpawnSerialAt(i) < true_oldest_serial) {
            true_oldest_index = i;
            true_oldest_serial = engine.SpawnSerialAt(i);
        }
    }
    REQUIRE(true_oldest_index >= 0);
    // Slot 0 now holds the newest grain, not the oldest.
    REQUIRE(engine.SpawnSerialAt(0) > true_oldest_serial);

    // The pool is now genuinely full (30/30). Force the full-pool
    // fallback branch of AllocateGrain directly — GrainEngine::Process's
    // separate CPU-based max-active-grain cap would otherwise drop any
    // further trigger before AllocateGrain is even called once the pool
    // is at capacity, so this test-only hook drives the same branch a
    // real overflow trigger reaches.
    engine.ForceAllocateGrainForTest();

    int pending_kill_index = -1;
    int pending_kill_count = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.PendingKillAt(i)) {
            ++pending_kill_count;
            pending_kill_index = i;
        }
    }
    REQUIRE(pending_kill_count == 1);
    REQUIRE(pending_kill_index == true_oldest_index);
    REQUIRE(pending_kill_index != 0);  // the old array-index-based code picked slot 0
}
