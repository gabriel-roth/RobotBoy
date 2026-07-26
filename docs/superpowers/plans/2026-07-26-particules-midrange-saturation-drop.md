# Particules Midrange Saturation Drop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatic (droppable) grain triggers are dropped instead of stealing whenever the grain pool is saturated — at any cap value, not just the cap floor of 2.

**Architecture:** One-condition change in `GrainEngine::Process`'s spawn loop: the 2026-07-25 drop rule loses its `cached_max_active_ == 2` gate (and with it the startup-ramp carve-out). Manual triggers keep the full steal path. Everything else (scheduler tagging, cap formula, kill machinery) is untouched.

**Tech Stack:** C++ (shared VCV/MetaModule DSP core), Catch2 test suite at `tests/particules_dsp` (CMake, vendored, offline, run via `./run.sh`).

**Spec:** `docs/superpowers/specs/2026-07-26-particules-midrange-saturation-drop-design.md`

## Global Constraints

- Work on branch `worktree-particules-midrange-drop` in the worktree at `/Users/gabrielroth/Dev/RobotBoy/.claude/worktrees/particules-midrange-drop` (all paths below are relative to it).
- Commit messages: one short sentence, ≤15 words, no AI attribution lines.
- Test suite command: `cd tests/particules_dsp && ./run.sh` — must end 100% passed.
- Manual triggers (kGated rising edge, all kClocked ticks, kMidi) MUST keep today's steal behavior.
- The 2026-07-25 cap-floor tests (tests (a) and (b) in `test_longgrain_drop.cpp`) must pass UNMODIFIED.
- No new params, menu items, or patch-format changes.

---

### Task 1: Failing tests — saturated automatic ticks drop at any cap

**Files:**
- Modify: `tests/particules_dsp/test_longgrain_drop.cpp` (header comment lines 13–25; replace test (c) at lines 203–277; append test (e))

**Interfaces:**
- Consumes: existing test-only `GrainEngine` accessors `ActiveGrainCount()`, `ActiveAt(int)`, `SpawnSerialAt(int)`, `PendingKillAt(int)`; `RecordingBuffer::Init/Write`; `ParticulesParameters`.
- Produces: two red tests that Task 2 turns green. Test names: `"GrainEngine: automatic triggers are dropped when saturated above the cap floor"` and `"GrainEngine: automatic triggers drop against the ramped cap during startup"`.

- [ ] **Step 1: Rewrite the file-header comment** (lines 13–25) to describe the generalized rule:

```cpp
// Particules: drop automatic triggers at any saturated grain cap.
// Specs: docs/superpowers/specs/2026-07-25-particules-longgrain-trigger-drop-design.md
//        (original cap-floor-only rule), generalized by
//        docs/superpowers/specs/2026-07-26-particules-midrange-saturation-drop-design.md.
//
// When a trigger finds the grain pool saturated (dynamic CPU cap, ramped
// startup cap, or full pool), automatic (droppable) triggers -- kLatched
// phasor ticks, kGated held-repeat ticks -- are silently dropped instead
// of stealing, at ANY cap value, so playing grains always finish their
// envelopes. Manual triggers (kGated rising edge, all kClocked ticks,
// kMidi) still steal the oldest grain so performed events always sound.
```

- [ ] **Step 2: Replace test (c)** (the `"automatic triggers still steal when saturated above the cap floor"` TEST_CASE, lines 203–277) with the inverted expectation. Keep the same buffer/engine/ramp-burn recipe; the timing margins are load-bearing (grain duration at size 0.5 on an 8 s buffer is \~0.78 s = \~37440 samples, so nothing finishes naturally inside the 30000+2000-sample window):

```cpp
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
```

- [ ] **Step 3: Append test (e) — the startup-ramp window** (after the new test (c), at the end of the file):

```cpp
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
    params.size = 0.5f;     // mid-size: the unramped cap is ~15, so during
                             // the early ramp the RAMPED cap (2 until
                             // sample ~3692 = 48000/13) is the binding
                             // limit -- exactly the case the superseded
                             // 2026-07-25 carve-out kept stealing.
    params.density = 0.0f;  // max-rate ticks from sample 0 -- no ramp burn.

    std::vector<StereoFrame> block(64);

    // Ticks arrive every ~367 samples, so the pool saturates at 2 grains
    // around sample ~734 and ~7 further ticks hit the saturated branch
    // inside the window. Stop at 3500 (margin below the ramp's first step
    // to 3 at ~3692). Grain duration ~37440 samples: nothing dies
    // naturally in the window.
    const int kWindow = 3500;
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
```

- [ ] **Step 4: Run the suite to verify exactly these two tests fail**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL. The two new/rewritten test cases fail (pending-kills observed / serials advanced under the current steal behavior); tests (a), (b), and (d) plus the rest of the suite still pass. Do NOT commit yet — Task 2 commits tests and implementation together.

### Task 2: Implement the generalized drop rule

**Files:**
- Modify: `src/particules/dsp/src/grain/grain_engine.cpp:302-318` (spawn-loop policy comment + drop condition)
- Test: `tests/particules_dsp/test_longgrain_drop.cpp` (from Task 1)

**Interfaces:**
- Consumes: `trigger_droppable[]` from `GrainScheduler::Process` (already wired).
- Produces: the new saturation policy; no signature changes anywhere.

- [ ] **Step 1: Replace the spawn-loop header comment** (`grain_engine.cpp:302-304`):

Old:
```cpp
    // Start new grains at their trigger points. At saturation (CPU cap or
    // full pool), steal-and-replace: retire the oldest grain and start the
    // new one, so the newest events always sound (decided 2026-07-11).
```

New:
```cpp
    // Start new grains at their trigger points. At saturation (CPU cap or
    // full pool) the policy splits by trigger kind: manual triggers (gate
    // rising edges, clock ticks) steal-and-replace so performed events
    // always sound (decided 2026-07-11); automatic density ticks are
    // dropped so playing grains finish their envelopes instead of
    // churning (cap floor 2026-07-25, generalized to every cap
    // 2026-07-26). See docs/superpowers/specs/
    // 2026-07-26-particules-midrange-saturation-drop-design.md.
```

- [ ] **Step 2: Replace the drop condition and its comment** (inside the `if (!g)` branch):

Old:
```cpp
            // Long-grain cap floor: at cached_max_active_ == 2 (grain
            // duration > half the buffer), automatic (droppable) triggers
            // are dropped instead of stealing, so buffer-length grains play
            // out undisturbed rather than churning on every density tick.
            // Uses cached_max_active_, not the startup-ramped max_active,
            // so the 1-second startup ramp (which pins the effective cap to
            // 2 even with short grains) keeps today's steal behavior. See
            // docs/superpowers/specs/2026-07-25-particules-longgrain-trigger-drop-design.md.
            if (trigger_droppable[t] && cached_max_active_ == 2) continue;
```

New:
```cpp
            // Saturated (dynamic cap, startup-ramped cap, or full pool):
            // automatic (droppable) triggers are dropped -- no steal, no
            // spawn -- at ANY cap value. One rule everywhere; the
            // 2026-07-25 cap-floor-only gate and its startup-ramp
            // carve-out are deliberately superseded (see the 2026-07-26
            // spec).
            if (trigger_droppable[t]) continue;
```

- [ ] **Step 3: Run the suite to verify everything passes**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: PASS, 100% (Task 1's two tests now green; cap-floor tests (a)/(b) and full-pool/CPU-cap steal tests in `test_grain_kill.cpp` untouched and green — they all use manual gated rising edges).

- [ ] **Step 4: Commit tests + implementation together**

```bash
git add src/particules/dsp/src/grain/grain_engine.cpp tests/particules_dsp/test_longgrain_drop.cpp
git commit -m "Particules: drop automatic triggers at any saturated grain cap"
```

### Task 3: Manual-steal regression tests at a saturated mid-size cap

**Files:**
- Modify: `tests/particules_dsp/test_longgrain_drop.cpp` (append two TEST_CASEs after test (e))

**Interfaces:**
- Consumes: same accessors as Task 1.
- Produces: regression coverage only; expected green immediately.

- [ ] **Step 1: Append the gated-rising-edge steal test:**

```cpp
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
```

- [ ] **Step 2: Append the clocked-tick steal test** (same body as (f) with the trigger block replaced — full case below):

```cpp
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
```

- [ ] **Step 3: Run the suite — expect all green** (these are regression pins on unchanged behavior, so they pass immediately)

Run: `cd tests/particules_dsp && ./run.sh`
Expected: PASS, 100%.

- [ ] **Step 4: Commit**

```bash
git add tests/particules_dsp/test_longgrain_drop.cpp
git commit -m "Particules: manual triggers still steal at saturated mid-size caps, tested"
```

### Task 4: Documentation sweep

**Files:**
- Modify: `src/particules/dsp/src/grain/grain_scheduler.h:18-24` (droppable[] doc comment)
- Modify: `docs/superpowers/specs/2026-07-25-particules-longgrain-trigger-drop-design.md:3-6` (status addendum)

**Interfaces:** none (comments/docs only).

- [ ] **Step 1: Update the `droppable` doc comment in `grain_scheduler.h`.**

Old (within the `Process` declaration comment):
```cpp
    // If droppable is non-null, droppable[i] is written for each emitted
    // trigger_samples[i]: true for automatic triggers the engine's spawn
    // loop is allowed to silently drop at the long-grain cap floor instead
    // of stealing (kLatched phasor ticks, kGated held-repeat ticks); false
    // for manual triggers, which always steal (kGated rising edge, all
    // kClocked ticks, kMidi). See
    // docs/superpowers/specs/2026-07-25-particules-longgrain-trigger-drop-design.md.
```

New:
```cpp
    // If droppable is non-null, droppable[i] is written for each emitted
    // trigger_samples[i]: true for automatic triggers the engine's spawn
    // loop silently drops when the grain pool is saturated, instead of
    // stealing (kLatched phasor ticks, kGated held-repeat ticks); false
    // for manual triggers, which always steal (kGated rising edge, all
    // kClocked ticks, kMidi). See
    // docs/superpowers/specs/2026-07-26-particules-midrange-saturation-drop-design.md.
```

- [ ] **Step 2: Add a supersession note to the 2026-07-25 spec**, directly after its **Date/Status** paragraph (lines 3–6):

```markdown
> **Superseded 2026-07-26:** the cap-floor-only condition and its
> startup-ramp carve-out were generalized — automatic triggers now drop at
> ANY saturated cap. See
> `2026-07-26-particules-midrange-saturation-drop-design.md`.
```

- [ ] **Step 3: Run the suite once more (comment-only change, belt and suspenders)**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: PASS, 100%.

- [ ] **Step 4: Commit**

```bash
git add src/particules/dsp/src/grain/grain_scheduler.h docs/superpowers/specs/2026-07-25-particules-longgrain-trigger-drop-design.md
git commit -m "Particules: point scheduler docs and old spec at the generalized drop rule"
```

### Task 5: VCV build + install for the listening pass

**Files:** none modified — build artifacts only.

**Interfaces:**
- Consumes: the finished DSP change on this branch.
- Produces: an installed VCV Rack plugin build for the user's listening pass.

- [ ] **Step 1: Invoke the `build-robotboy-plugin` skill** (VCV target only — the MetaModule build is explicitly deferred until after the listening pass, per the spec). Build from THIS worktree (`make -C vcv` at the worktree root), then copy the dylib/plugin.json/res into the Rack2 plugins directory as the skill directs.

- [ ] **Step 2: Verify the install** — confirm the freshly built dylib's timestamp in the Rack2 plugins directory is newer than the build start.

- [ ] **Step 3: Report.** No GUI/simulator testing by agents — end with a user listening checklist (Size sweep 12→5 o'clock at max Density: level stays full, no crunch, no seam at ~4:10; clocked-Seed rhythmic-slicer patch still cuts tightly; low-Density behavior unchanged).
