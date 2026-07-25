#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>

#include "particules_dsp/types.h"
#include "particules_dsp/parameters.h"
#include "buffer/recording_buffer.h"
#include "grain/grain.h"
#include "grain/grain_engine.h"

using namespace particules_dsp;
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

    int64_t buf_size_q = static_cast<int64_t>(buffer.size()) << 32;
    auto ctx = buffer.MakeReadContext();
    float out_l, out_r;

    // Run well past the initial envelope ramp (phase clears the ~0.025
    // clipping threshold by sample ~50 at this grain size) into the flat
    // plateau, and capture an unfaded reference amplitude.
    for (int i = 0; i < 200; ++i) {
        REQUIRE(g.Process(ctx, buf_size_q, &out_l, &out_r));
    }
    float reference = out_l;
    REQUIRE(reference > 0.1f);  // sanity: plateau really is near full gain

    g.StartPendingKill();

    // Advance through the zero-crossing deadline (32 samples of "no
    // crossing"); the fallback fade should engage right after this.
    for (int i = 0; i < 32; ++i) {
        REQUIRE(g.Process(ctx, buf_size_q, &out_l, &out_r));
    }

    // The next kFallbackFadeSamples (4) samples are the fallback fade.
    std::vector<float> fade_ratios;
    for (int i = 0; i < 4; ++i) {
        bool alive = g.Process(ctx, buf_size_q, &out_l, &out_r);
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
    bool still_alive = g.Process(ctx, buf_size_q, &out_l, &out_r);
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

    ParticulesParameters params;
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

    // The pool is now genuinely full (30/30). Drive victim selection
    // directly via the test hook, which marks the true oldest grain for
    // pending-kill exactly as Process()'s steal path does at saturation —
    // isolating FindOldestActiveGrain's ordering logic from the trigger
    // machinery (the full steal path is covered by the steal test below).
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

// ── (c) Overflow trigger at a full pool steals the oldest, never vanishes ───
//
// Decided 2026-07-11: at saturation the newest events must always sound. A
// trigger arriving with the pool genuinely full (all kMaxGrains active) must
// hard-replace the oldest grain's slot rather than being dropped. This test
// drives the genuinely-full-pool path through the real Process() trigger
// loop (not the test hook): fill the pool, then fire one more trigger.

TEST_CASE("GrainEngine: trigger at a full pool steals the oldest grain instead of vanishing",
          "[engine][kill][steal]") {
    const size_t num_frames = 48000 * 8;  // 8s buffer: headroom so the
                                           // dynamic max-active cap reaches
                                           // kMaxGrains for the grain size used.
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
    params.trigger_mode = TriggerMode::kGated;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.density = 0.5f;  // noon = rate 0, only the rising-edge grain
                             // fires per gate pulse.
    params.gate = false;

    std::vector<StereoFrame> block(64);

    // Burn down the ~1s startup ramp before filling the pool.
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

    // Fill every slot with long grains so the pool is genuinely full when
    // the overflow trigger arrives.
    for (int i = 0; i < kMaxGrains; ++i) {
        fire_one_grain(0.2f);
    }
    REQUIRE(engine.ActiveGrainCount() == kMaxGrains);

    // Capture pool state before the overflow trigger.
    uint32_t oldest_serial = 0; int oldest_index = -1;
    uint32_t max_serial_before = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        REQUIRE(engine.ActiveAt(i));
        uint32_t s = engine.SpawnSerialAt(i);
        max_serial_before = std::max(max_serial_before, s);
        if (oldest_index < 0 || s < oldest_serial) { oldest_serial = s; oldest_index = i; }
    }

    // Fire one more trigger into the saturated engine.
    fire_one_grain(0.2f);

    // The new grain must exist: some slot now carries a serial newer than
    // everything that existed before the trigger.
    bool newest_sounds = false;
    for (int i = 0; i < kMaxGrains; ++i)
        if (engine.ActiveAt(i) && engine.SpawnSerialAt(i) > max_serial_before)
            newest_sounds = true;
    REQUIRE(newest_sounds);

    // The oldest grain was retired: its serial is gone from the pool.
    bool oldest_gone = true;
    for (int i = 0; i < kMaxGrains; ++i)
        if (engine.ActiveAt(i) && engine.SpawnSerialAt(i) == oldest_serial
            && !engine.PendingKillAt(i))
            oldest_gone = false;
    REQUIRE(oldest_gone);
}

// ── (d) CPU-cap saturation with pool headroom: click-free steal ─────────────
//
// The common production steal path: the dynamic max-active cap is hit while
// free slots remain. The oldest grain must be marked for the click-free
// pending-kill (NOT hard-replaced) and the new grain must start in a
// previously-free slot, with the active count transiently at cap+1.
//
// At SIZE = 1.0 the grain duration is ~the full buffer duration, so
// cached_max_active_ = buf_dur/grain_dur * 1.5 truncates to 1 and clamps to
// its floor of 2 — a deterministic cap far below kMaxGrains.

TEST_CASE("GrainEngine: CPU-cap saturation fades oldest grain and starts new one in a free slot",
          "[engine][kill][steal]") {
    const size_t num_frames = 48000 * 8;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> memory(bytes, 0);
    RecordingBuffer buffer;
    buffer.Init(reinterpret_cast<float*>(memory.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        buffer.Write(0.5f, 0.5f);  // DC: the mono sum never crosses zero, so
                                    // a pending kill cannot complete before
                                    // its 32-sample deadline — keeping the
                                    // victim observably pending below.
    }

    GrainEngine engine;
    engine.Init(kSampleRate, &buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kGated;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.density = 0.5f;  // noon = rate 0, only the rising-edge grain
                             // fires per gate pulse.
    params.gate = false;
    params.size = 1.0f;     // max SIZE from the start, so the cached cap
                             // settles at its floor of 2 during the burn.

    std::vector<StereoFrame> block(64);

    // Burn down the ~1s startup ramp.
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }

    auto fire_one_grain = [&] {
        params.gate = true;
        engine.Process(params, block.data(), 64);
        params.gate = false;
        engine.Process(params, block.data(), 64);
    };

    // Two ~8s grains reach the cap (max_active == 2) with 28 slots free.
    fire_one_grain();
    fire_one_grain();
    REQUIRE(engine.ActiveGrainCount() == 2);

    // Capture pool state before the overflow trigger.
    uint32_t oldest_serial = 0; int oldest_index = -1;
    uint32_t max_serial_before = 0;
    bool was_active[kMaxGrains];
    for (int i = 0; i < kMaxGrains; ++i) {
        was_active[i] = engine.ActiveAt(i);
        if (!was_active[i]) continue;
        uint32_t s = engine.SpawnSerialAt(i);
        max_serial_before = std::max(max_serial_before, s);
        if (oldest_index < 0 || s < oldest_serial) { oldest_serial = s; oldest_index = i; }
    }
    REQUIRE(oldest_index >= 0);

    // Overflow trigger in a 16-sample block — shorter than the 32-sample
    // zero-crossing deadline, so the victim's fade cannot have completed
    // and the cap+1 transient is still observable.
    params.gate = true;
    engine.Process(params, block.data(), 16);
    params.gate = false;

    // (a) The oldest grain took the click-free path: still active, marked
    //     pending-kill, serial unchanged (NOT hard-replaced) — and it is
    //     the only pending kill.
    REQUIRE(engine.ActiveAt(oldest_index));
    REQUIRE(engine.PendingKillAt(oldest_index));
    REQUIRE(engine.SpawnSerialAt(oldest_index) == oldest_serial);
    int pending_kill_count = 0;
    for (int i = 0; i < kMaxGrains; ++i)
        if (engine.PendingKillAt(i)) ++pending_kill_count;
    REQUIRE(pending_kill_count == 1);

    // (b) The new grain started in a previously-free slot with a serial
    //     newer than everything before the trigger.
    int new_grain_index = -1;
    for (int i = 0; i < kMaxGrains; ++i)
        if (engine.ActiveAt(i) && engine.SpawnSerialAt(i) > max_serial_before)
            new_grain_index = i;
    REQUIRE(new_grain_index >= 0);
    REQUIRE_FALSE(was_active[new_grain_index]);

    // (c) Active count is transiently cap+1.
    REQUIRE(engine.ActiveGrainCount() == 3);
}

// ── (d) NaN pitch CV must resolve to the exact unity pitch fallback ────────
//
// SemitonesToRatioFast (Exp2Fast, Task 13) requires a finite argument:
// unlike std::exp2, which propagates NaN straight through, Exp2Fast's
// `(int)y` truncation is UB on non-finite input and has been observed to
// produce small but FINITE garbage -- which would sail straight past the
// isfinite(gp.pitch_ratio) fence in ComputeGrainParams undetected.
// ComputeGrainParams now sanitizes mod_pitch to 0 semitones immediately
// before the fast-exp2 call, so a NaN pitch_cv (with pitch_ar engaged, so
// it actually reaches the engine) must produce a forward grain at exactly
// unity rate -- not merely "some finite value", which would pass even if
// the sanitize-before-fast-path fix regressed back to sanitizing only the
// (now ineffective) post-multiply gp.pitch_ratio fence.
TEST_CASE("GrainEngine: NaN pitch CV resolves to the exact unity pitch fallback",
          "[engine][nan]") {
    const size_t num_frames = 48000 * 2;
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
    params.trigger_mode = TriggerMode::kGated;
    params.time = 0.5f;
    params.size = 0.0f;      // short grain, keeps the test fast
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.pitch_ar = 0.5f;  // CW of noon + cv_connected: modulation = ar*cv,
                             // so a NaN pitch_cv actually reaches mod_pitch
                             // (Attenurandomizer::Process short-circuits to
                             // the unmodulated base when ar_amount == 0).
    params.pitch_cv = NAN;
    params.pitch_cv_connected = true;
    params.density = 0.5f;   // noon = only the gate rising edge fires
    params.gate = false;

    std::vector<StereoFrame> block(64);

    // Burn down the ~1s startup ramp; gate stays low so nothing fires yet.
    for (int i = 0; i < 800; ++i) {
        engine.Process(params, block.data(), 64);
    }
    REQUIRE(engine.ActiveGrainCount() == 0);

    // Rising edge: fire exactly one grain while pitch_cv is still NaN.
    params.gate = true;
    engine.Process(params, block.data(), 64);
    params.gate = false;

    int fired_index = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (engine.ActiveAt(i)) { fired_index = i; break; }
    }
    REQUIRE(fired_index >= 0);
    // Exact equality, not Approx/isfinite: pins that the unity fallback
    // actually engaged, rather than Exp2Fast's UB silently producing some
    // other finite ratio that would also pass a mere finiteness check.
    REQUIRE(engine.PhaseIncrementAt(fired_index) == 1.0f);
}
