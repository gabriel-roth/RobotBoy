# Code Review Round 4 (Small Fixes, Test Gaps, Refactors) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close every item in the "Fixes", "Refactors", and test-gap sections of `code-review-2026-07-08.md` — six small fixes, four test-gap areas, three refactor groups.

**Architecture:** Two independent work streams. **Stream A** (Tasks 1–4) touches Loooop and runs in the existing worktree `/Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track` on branch `loooop-track`. **Stream B** (Tasks 5–12) touches the particules_dsp library, the Particules wrapper, and build files, and runs in a new worktree `/Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4` on branch `review-round-4` off `main`. Tasks within a stream are sequential; the two streams are independent and may run concurrently.

**Tech Stack:** C++20. Loooop tests are hand-rolled (`check()`/`near()` helpers, run via `tests/run.sh`). particules_dsp tests are Catch2 v3 (vendored), run via `tests/particules_dsp/run.sh`. VCV build via `make -C vcv`; MetaModule build via CMake in `metamodule/`.

## Global Constraints

- Commit messages: short, one sentence, ≤15 words. **No `Co-Authored-By` or AI attribution lines.**
- Stream A working dir: `/Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track` (branch `loooop-track`).
- Stream B working dir: `/Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4` (branch `review-round-4`, created from `main` at execution start).
- Loooop test lane: `cd <worktree>/tests && ./run.sh` (compiles with g++, runs binaries + python tests). Expected pass output ends with each suite's `N tests, 0 failures` and exit 0.
- particules_dsp test lane: `cd <worktree> && ./tests/particules_dsp/run.sh` (cmake + ctest). New `test_*.cpp` files are auto-globbed.
- TDD: write the failing test first wherever a behavior changes. Refactors that must be behavior-preserving (Tasks 9, 10) rely on the existing suite passing unchanged.
- Do not touch anything in the "Verified correct (don't re-investigate)" sections of the review doc.
- kMidi is out of scope; never flag or modify it.

---

## Stream A — Loooop (worktree `.worktrees/loooop-track`)

### Task 1: Stale `osRamp` reset in `triggerOneShot`'s `!playing` path

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp:206-224` (`triggerOneShot`)
- Test: `tests/loooop/test_loop_engine.cpp`

**Context:** `PlayHead::osRamp` (LoopEngine.hpp:103, default 1.f) is the ~1 ms retrigger ramp-in gain. It is *lowered* only in `triggerOneShot`'s `if (h.playing)` branch (`if (g < 1.f) h.osRamp = g;`) and *raised* back to 1 only in `advanceHead` while the head plays. If a pass ends (or `clear()` runs — `clear()` never touches `osRamp`) while `osRamp < 1`, the stale value survives and attenuates the attack of the next one-shot trigger, which takes the `!playing` path where `osRamp` is never written.

**Do NOT reset `osRamp` unconditionally at the top of `triggerOneShot`:** the `h.playing` branch deliberately leaves a mid-ramp `osRamp` in place when retriggering outside the fade region (gain continuity — snapping to 1 mid-ramp would click). The fix belongs only on the `!playing` side, where the head is silent and the snap is inaudible.

**Interfaces:**
- Produces: no API change. `triggerOneShot(int head)` now guarantees `osRamp == 1.f` when starting a pass from a non-playing (armed/ended) state.

- [ ] **Step 1: Write two failing tests**

Add to `tests/loooop/test_loop_engine.cpp` (place near `test_one_shot_retrigger_mid_fade`, line ~556). These use the existing `check`/`near` helpers and follow the setup pattern of `test_one_shot_retrigger_mid_fade` (line 537).

Test A — stale ramp survives a pass end, reference-engine comparison. Run the *same* trigger sequence on a reference engine except for the mid-fade retrigger, and compare the first output samples of the final fresh trigger:

```cpp
// Q: a retrigger ramp value left behind by an ended pass must not
// attenuate the next trigger (the !playing path never reset osRamp).
static void test_one_shot_stale_ramp_cleared_on_fresh_trigger() {
    auto runSequence = [](bool retriggerMidFade) {
        LoopEngine e(1);
        e.reset(48000.f, 1.f);
        e.toggleRecord();
        for (int i = 0; i < 2000; ++i) e.process(1.f);
        e.toggleRecord();
        e.setOneShot(0, true);
        e.setSpeed(0, 50.f);              // pass ends in ~40 samples, inside the ~48-sample ramp
        e.triggerOneShot(0);
        for (int i = 0; i < 36; ++i) e.process(0.f);   // ~90% through the pass, into the fade
        if (retriggerMidFade)
            e.triggerOneShot(0);          // sets osRamp = fade gain < 1
        for (int i = 0; i < 200; ++i) e.process(0.f);  // pass ends; head stops
        // Fresh trigger from the armed, non-playing state:
        e.triggerOneShot(0);
        float first = e.process(0.f);
        return first;
    };
    float withStaleRamp = runSequence(true);
    float clean         = runSequence(false);
    check(near(withStaleRamp, clean, 1e-4f),
          "stale_osramp: fresh trigger opens at the same level as a clean trigger");
}
```

Test B — the very-short/fast one-shot window corner from the review's test-gap list (retrigger ramp-in compounding with end fade must stay finite and snap-free):

```cpp
static void test_one_shot_short_fast_window_ramp_and_fade() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);
    e.toggleRecord();
    e.setOneShot(0, true);
    e.setSpeed(0, 50.f);
    bool allFinite = true;
    float maxDelta = 0.f, prev = 0.f;
    for (int pass = 0; pass < 8; ++pass) {
        e.triggerOneShot(0);
        for (int i = 0; i < 20; ++i) {     // retrigger every 20 samples, mid-pass
            float v = e.process(0.f);
            if (!std::isfinite(v)) allFinite = false;
            maxDelta = std::max(maxDelta, std::fabs(v - prev));
            prev = v;
        }
    }
    check(allFinite, "os_short_window: output stays finite under rapid retrigger");
    check(maxDelta < 0.15f, "os_short_window: no hard gain snaps");
}
```

Register both in `main()` alongside the other one-shot tests.

**Adaptation note:** if Test A's timing constants don't reproduce a stale sub-unity `osRamp` (verify by observing `withStaleRamp < clean - 1e-3` *before* the fix), adjust the sample counts (36 / 200) so the retrigger lands inside the end fade and the pass ends before the ~48-sample ramp completes. The test must FAIL before the fix.

- [ ] **Step 2: Run tests to verify Test A fails**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh`
Expected: `stale_osramp: fresh trigger opens at the same level as a clean trigger` FAILS; Test B may pass or fail.

- [ ] **Step 3: Implement the fix**

In `src/loooop/dsp/LoopEngine.cpp`, `triggerOneShot`, add an `else` branch:

```cpp
    if (h.playing) {
        // Retrigger during the tail fade: start the new pass from the current
        // fade gain and ramp back to unity instead of snapping (Q2).
        double winStart, winLen;
        windowBounds(h, winStart, winLen);
        const int Fo = oneShotFadeLen(h, winLen);
        if (Fo >= 1) {
            const float g = oneShotFadeGain(h, winStart, winLen, Fo);
            if (g < 1.f) h.osRamp = g;
        }
    } else {
        // A ramp value left behind by an ended pass must not attenuate a
        // fresh trigger; the head is silent here, so the snap is inaudible.
        h.osRamp = 1.f;
    }
```

- [ ] **Step 4: Run tests to verify all pass**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh`
Expected: all suites pass, exit 0 (including `test_one_shot_retrigger_mid_fade` — the playing-branch behavior is unchanged).

- [ ] **Step 5: Commit**

```bash
git add src/loooop/dsp/LoopEngine.cpp tests/loooop/test_loop_engine.cpp
git commit -m "fix: reset stale one-shot retrigger ramp on fresh trigger"
```

---

### Task 2: Mid-pass `setWriteMode(Decay)` seeds the Decay low-pass

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp:39-41` (`setWriteMode` becomes out-of-line)
- Modify: `src/loooop/dsp/LoopEngine.cpp` (new `setWriteMode` definition)
- Test: `tests/loooop/test_loop_engine.cpp`

**Context:** The Decay write mode's one-pole LP state (`decayLpL_`/`decayLpR_`, LoopEngine.hpp:148) is seeded from `bufL_[0]`/`bufR_[0]` only in `toggleRecord` when an overdub pass *starts* in Decay mode (LoopEngine.cpp:78-81). Switching to Decay mid-pass via `setWriteMode` (currently a header-inline one-liner: `void setWriteMode(WriteMode m) { writeMode_ = m; }`) leaves the LP at whatever a previous pass (or `reset()`'s zeroing) left, so the first Decay-filtered feedback samples are wrong until the next pass.

**Interfaces:**
- Produces: `void setWriteMode(WriteMode m);` — same signature, now declared in the header and defined in the .cpp. When switching *into* Decay during an active overdub pass (`recording_ && loopLen_ != 0`), it seeds `decayLpL_/R_` from `bufL_/R_[writeIdx_]` (the sample the filter will process next).

- [ ] **Step 1: Write the failing test**

Reference-engine comparison: engine A starts its overdub pass already in Decay mode (seeded by `toggleRecord`); engine B starts in Layer mode and switches to Decay mid-pass. Both record the same constant-1.0 loop, so at the switch point both LPs should sit at 1.0 — the samples written *after* the switch index must match engine A's within the odGain envelope (identical in both, since both overdubs started at the same sample). With the bug, engine B's LP is 0 (from `reset()`), so its post-switch writes differ.

```cpp
// Switching write mode to Decay mid-pass must seed the tone filter from
// current buffer content, not stale state (previously only toggleRecord seeded).
static void test_write_mode_decay_midpass_switch_seeds_lp() {
    auto record = [](LoopEngine& e) {
        e.reset(48000.f, 1.f);
        soloHead0(e);
        e.toggleRecord();
        for (int i = 0; i < 1000; ++i) e.process(1.f);
        e.toggleRecord();
    };
    LoopEngine a(1), b(1);
    record(a); record(b);
    a.setWriteMode(LoopEngine::WriteMode::Decay);
    b.setWriteMode(LoopEngine::WriteMode::Layer);
    a.toggleRecord(); b.toggleRecord();          // start overdub pass on both
    for (int i = 0; i < 400; ++i) { a.process(0.f); b.process(0.f); }  // past the 240-sample up-ramp
    b.setWriteMode(LoopEngine::WriteMode::Decay);  // mid-pass switch
    // Post-switch writes hit not-yet-rewritten indices (old == original 1.0
    // in both engines) with the same odGain; only the LP state can differ.
    bool matched = true;
    for (int i = 0; i < 100; ++i) {
        float va = a.process(0.f), vb = b.process(0.f);
        (void)va; (void)vb;
    }
    a.toggleRecord(); b.toggleRecord();          // stop; ramps run out
    for (int i = 0; i < 400; ++i) { a.process(0.f); b.process(0.f); }
    // Compare the loop region written after the switch on the next playback pass.
    for (int i = 0; i < 1000; ++i) {
        float va = a.process(0.f), vb = b.process(0.f);
        if (i >= 400 && i < 500 && std::fabs(va - vb) > 1e-3f) matched = false;
    }
    check(matched, "decay_midpass: post-switch writes match a pass-start-seeded engine");
}
```

Register in `main()` near the other write-mode tests (`test_write_mode_decay_at_low_rate_matches_layer`, line ~945). Note `soloHead0` is an existing helper in this file.

**Adaptation note:** engine A and B write different feedback content *before* the switch (Layer vs Decay fb differ), so only compare indices written after sample 400 of the pass. If playback index alignment is off (heads restart at pass end / stop-ramp consumes samples), align by locating the comparison window from `loopLength()` and the known write index rather than hard-coding 400–500; the requirement is: compare only samples whose write happened after the switch. The test must FAIL before the fix (expect a mismatch on the order of decayLpA_ ≈ 0.5, far above 1e-3).

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh`
Expected: `decay_midpass: post-switch writes match a pass-start-seeded engine` FAILS.

- [ ] **Step 3: Implement**

In `src/loooop/dsp/LoopEngine.hpp` replace the inline definition:

```cpp
    void setWriteMode(WriteMode m);
    WriteMode writeMode() const { return writeMode_; }
```

In `src/loooop/dsp/LoopEngine.cpp` add (near `toggleRecord`):

```cpp
void LoopEngine::setWriteMode(WriteMode m) {
    if (m == WriteMode::Decay && writeMode_ != WriteMode::Decay
        && recording_ && loopLen_ != 0) {
        // Mid-pass switch into Decay: seed the tone filter from the sample
        // it will process next, matching the pass-start seed in toggleRecord.
        decayLpL_ = bufL_[writeIdx_];
        decayLpR_ = bufR_[writeIdx_];
    }
    writeMode_ = m;
}
```

(`recording_ && loopLen_ != 0` is exactly the overdub condition in `process()`; a first recording pass with `loopLen_ == 0` doesn't use the LP.)

- [ ] **Step 4: Run tests to verify all pass**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh`
Expected: all pass, exit 0 — including the four existing write-mode tests.

- [ ] **Step 5: Commit**

```bash
git add src/loooop/dsp/LoopEngine.hpp src/loooop/dsp/LoopEngine.cpp tests/loooop/test_loop_engine.cpp
git commit -m "fix: seed Decay low-pass when switching write mode mid-pass"
```

---

### Task 3: MetaModule `set_samplerate` preserves the loop + SR-change breadth tests

**Files:**
- Modify: `metamodule/loooop/LoooopCore.cc:93-98`
- Modify: `metamodule/loooop/LopCore.cc:98-101`
- Test: `tests/loooop/test_loop_engine.cpp` (breadth tests), `tests/loooop/test_module_dsp.cpp` (core-level test **if** it already compiles the MM cores — check first)

**Context:** Both MM cores' `set_samplerate(float sr)` call `engine_.reset(sr)` — the full reallocating reset that erases a recorded loop. The VCV modules use `engine.setSampleRate(sr)` (LoopEngine.cpp:46-61), which preserves the loop and only retunes coefficients, falling back to `reset` when nothing is recorded. Align MM with VCV.

**Interfaces:**
- Consumes: `LoopEngine::setSampleRate(float)` — existing, tested.
- Produces: no API change.

- [ ] **Step 1: Write failing/new engine breadth tests**

The engine path itself is already covered for the single-head default case; the review's test-gap list asks for breadth. Add to `tests/loooop/test_loop_engine.cpp` near `test_sample_rate_change_preserves_loop` (line 769):

```cpp
static void test_sample_rate_change_multi_head_nondefault_speed() {
    LoopEngine e;                       // default 4 heads
    e.reset(10.f, 100.f);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f}) e.process(x);
    e.toggleRecord();
    // Non-default per-head state that must survive the retune:
    e.setSpeed(0, 2.f);
    e.setSpeed(1, -1.f);
    e.setLevel(2, 0.f);
    e.setLevel(3, 0.f);
    e.setSampleRate(20.f);
    check(e.loopLength() == 8, "sr_multi: loop length preserved");
    // Head 0 at speed 2 reads every other sample; head 1 reads in reverse.
    // Just assert content survived and output is finite and nonzero:
    bool finite = true; float energy = 0.f;
    for (int i = 0; i < 16; ++i) {
        float v = e.process(0.f);
        if (!std::isfinite(v)) finite = false;
        energy += std::fabs(v);
    }
    check(finite, "sr_multi: output finite after retune");
    check(energy > 0.1f, "sr_multi: loop content audible after retune");
}

static void test_sample_rate_change_redundant_same_rate() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f}) e.process(x);
    e.toggleRecord();
    e.setSampleRate(10.f);   // same rate — must be a no-op for the loop
    e.setSampleRate(10.f);   // and again
    check(e.loopLength() == 4, "sr_same: loop survives redundant same-rate calls");
    check(near(e.process(0.f), 1.f), "sr_same: out[0]==1");
    check(near(e.process(0.f), 2.f), "sr_same: out[1]==2");
}
```

Register both in `main()`. These should PASS already (they pin engine behavior the MM fix relies on); if either fails, stop and report — that's a real engine bug, not a test problem.

Then check whether `tests/loooop/test_module_dsp.cpp` compiles the MetaModule cores (look at its includes and its `.extra` file). **If it does**, add a core-level regression test there: construct the core, drive a short recording via its param/process interface, call `set_samplerate(96000.f)`, and assert the loop still plays (nonzero output). **If it does not** (cores need the MM SDK), skip the core-level test — the change is a two-line swap onto a tested engine path; note this in the commit message is not needed, just proceed.

- [ ] **Step 2: Run tests**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh`
Expected: new breadth tests PASS (engine path already correct). Core-level test, if written, FAILS (core still calls `reset`).

- [ ] **Step 3: Implement the core fix**

`metamodule/loooop/LoooopCore.cc`:

```cpp
    void set_samplerate(float sr) override {
        engine_.setSampleRate(sr);   // preserve a recorded loop (matches VCV onSampleRateChange)
        const float a = loooop::smootherAlpha(sr, 0.002f);
        mixSm_.alpha = a;
        for (auto& s : panSm_) s.alpha = a;
    }
```

`metamodule/loooop/LopCore.cc`:

```cpp
    void set_samplerate(float sr) override {
        engine_.setSampleRate(sr);   // preserve a recorded loop (matches VCV onSampleRateChange)
        mixSm_.alpha = loooop::smootherAlpha(sr, 0.002f);
    }
```

- [ ] **Step 4: Verify build + tests**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh` — expected: all pass.
If the MM cores are not covered by the test lane, at minimum syntax-check the two files compile: try `g++ -std=c++20 -fsyntax-only -I src metamodule/loooop/LoooopCore.cc` from the worktree root; if SDK headers are missing this will fail on includes — in that case rely on the test lane plus the fact that `setSampleRate(float)` is an existing public method (LoopEngine.hpp:25).

- [ ] **Step 5: Commit**

```bash
git add metamodule/loooop/LoooopCore.cc metamodule/loooop/LopCore.cc tests/loooop/test_loop_engine.cpp
git commit -m "fix: MetaModule sample-rate change preserves recorded loop"
```

(Include `tests/loooop/test_module_dsp.cpp` in the add if a core-level test was written.)

---

### Task 4: Stop-ramp corner tests (test-only)

**Files:**
- Test: `tests/loooop/test_loop_engine.cpp`

**Context:** Review test-gap list: (a) `clear()`/`reset()` during a pending overdub stop-ramp; (b) toggling record off mid-up-ramp. Ramp state: `odGain_` (current write gain), `odGainStep_`, `stopPending_` (LoopEngine.hpp:155-157); up-ramp armed in `toggleRecord`, down-ramp via `stopPending_`; recording ends when `odGain_` hits 0 (LoopEngine.cpp:481-493). At 48 kHz, `xfadeSamples_` = 240. Existing neighbors: `test_overdub_ramps_declick` (:837), `test_stop_ramp_rearm` (:864).

These tests pin current behavior. If any reveals an actual bug (crash, non-finite output, stuck recording state), stop and report to the orchestrator rather than "fixing" the test.

- [ ] **Step 1: Write the tests**

```cpp
static void test_clear_during_stop_ramp() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);
    e.toggleRecord();                       // loop closed
    e.toggleRecord();                       // start overdub (up-ramp)
    for (int i = 0; i < 400; ++i) e.process(0.5f);
    e.toggleRecord();                       // request stop -> down-ramp pending
    for (int i = 0; i < 100; ++i) e.process(0.5f);   // mid down-ramp
    e.clear();                              // clear during the pending stop-ramp
    check(!e.isRecording(), "clear_stopramp: not recording after clear");
    check(e.loopLength() == 0, "clear_stopramp: loop erased");
    bool finite = true;
    for (int i = 0; i < 500; ++i)
        if (!std::isfinite(e.process(0.f))) finite = false;
    check(finite, "clear_stopramp: output finite after clear");
    // Engine must be able to record a fresh loop cleanly:
    e.toggleRecord();
    for (int i = 0; i < 500; ++i) e.process(1.f);
    e.toggleRecord();
    check(e.loopLength() == 500, "clear_stopramp: fresh loop records normally");
    check(near(e.process(0.f), 1.f), "clear_stopramp: fresh loop plays back");
}

static void test_reset_during_stop_ramp() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);
    e.toggleRecord();
    e.toggleRecord();                       // overdub
    for (int i = 0; i < 400; ++i) e.process(0.5f);
    e.toggleRecord();                       // stop pending
    for (int i = 0; i < 100; ++i) e.process(0.5f);
    e.reset(48000.f, 1.f);                  // full reset mid down-ramp
    check(!e.isRecording(), "reset_stopramp: not recording after reset");
    check(e.loopLength() == 0, "reset_stopramp: loop gone after reset");
    e.toggleRecord();
    for (int i = 0; i < 300; ++i) e.process(1.f);
    e.toggleRecord();
    check(e.loopLength() == 300, "reset_stopramp: records normally after reset");
}

static void test_record_off_mid_up_ramp() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);
    e.toggleRecord();                       // loop closed: all 1.0
    e.toggleRecord();                       // overdub: up-ramp starts (240 samples)
    for (int i = 0; i < 100; ++i) e.process(1.f);   // mid up-ramp
    e.toggleRecord();                       // stop while still ramping in
    bool finite = true;
    for (int i = 0; i < 2000; ++i)
        if (!std::isfinite(e.process(0.f))) finite = false;
    check(finite, "up_ramp_off: output finite");
    check(!e.isRecording(), "up_ramp_off: recording ended");
    // The partially-ramped overdub wrote at most ~2x content briefly; loop
    // content must stay bounded (constant-1 loop + up-to-unity ramped add of 1.0):
    float peak = 0.f;
    for (int i = 0; i < 1000; ++i) peak = std::max(peak, std::fabs(e.process(0.f)));
    check(peak <= 2.f + 1e-3f, "up_ramp_off: overdubbed content bounded");
}
```

Register all three in `main()` near `test_stop_ramp_rearm`.

**Adaptation note:** if `toggleRecord` semantics differ from the sequence above (e.g., stop consumes the pending ramp differently per `test_stop_ramp_rearm`), read that test first and match its call pattern; the behaviors to pin are exactly the review's: clear()/reset() during a pending stop-ramp, and record-off mid-up-ramp.

- [ ] **Step 2: Run tests**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh`
Expected: all pass. If a corner exposes a real defect, STOP and report it (with the failing output) instead of adjusting assertions.

- [ ] **Step 3: Commit**

```bash
git add tests/loooop/test_loop_engine.cpp
git commit -m "test: pin stop-ramp corners (clear/reset mid-ramp, record-off mid-up-ramp)"
```

---

## Stream B — particules_dsp, Particules wrapper, build (worktree `.worktrees/review-round-4`)

### Task 5: Grain-pool overflow steal-and-replace

**Files:**
- Modify: `src/particules/dsp/src/grain/grain_engine.cpp` (`Process` trigger loop, lines 271-283; `AllocateGrain`, lines 72-112)
- Modify: `src/particules/dsp/src/grain/grain_engine.h` (new private helper decl; test hook, lines 37-45)
- Test: `tests/particules_dsp/test_grain_kill.cpp`

**Context (decided July 11):** overflow must steal-and-replace — the newest events always sound at saturation. Today `Process()` breaks on `active_before >= max_active` *before* `AllocateGrain()`, so a saturated trigger simply vanishes; `AllocateGrain`'s full-pool path only marks the oldest for a click-free pending-kill and still returns nullptr. Two saturation cases:

1. **CPU cap** (`max_active` < pool size, free slots exist — the common case): mark the oldest grain for the existing click-free pending-kill AND start the new grain in a free slot. Active count transiently exceeds the cap by 1 for ≤36 samples (kZeroCrossDeadline 32 + 4-sample fallback fade) — acceptable.
2. **Genuinely full pool** (all `kMaxGrains` active): hard-replace the oldest grain's slot with the new grain (`Start()` overwrites it). The grain envelope starts at zero so the new grain opens silently; the victim's mid-window cut is the accepted tradeoff of the decision.

**Interfaces:**
- Produces: private `int FindOldestActiveGrain() const` (returns index of the active, non-pending-kill grain with the lowest `spawn_serial()`, or −1). Test hook `ForceAllocateGrainForTest()` keeps its name and its observable behavior (marks the true oldest for kill at full pool) but is reimplemented on the helper.
- Consumes: `Grain::StartPendingKill()`, `Grain::Start()`, `SetSpawnSerial`, test accessors `ActiveAt/PendingKillAt/SpawnSerialAt` (grain_engine.h:33-35).

- [ ] **Step 1: Write the failing test**

Add to `tests/particules_dsp/test_grain_kill.cpp`, reusing that file's full-pool setup machinery (the victim-selection test at :109-213 shows how to fill all `kMaxGrains` slots deterministically — burn the startup ramp, fire long grains until `ActiveGrainCount() == kMaxGrains`):

```cpp
TEST_CASE("GrainEngine: trigger at a full pool steals the oldest grain instead of vanishing",
          "[engine][kill][steal]") {
    // ... same buffer + engine setup and pool-filling sequence as the
    // victim-selection test above (8 s buffer, burn startup ramp, fire
    // kMaxGrains long grains). Capture state before the overflow trigger:
    uint32_t oldest_serial = 0; int oldest_index = -1;
    uint32_t max_serial_before = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        REQUIRE(engine.ActiveAt(i));
        uint32_t s = engine.SpawnSerialAt(i);
        max_serial_before = std::max(max_serial_before, s);
        if (oldest_index < 0 || s < oldest_serial) { oldest_serial = s; oldest_index = i; }
    }

    // Fire one more trigger into the saturated engine (same single-trigger
    // block mechanism the setup used).
    // ... fire trigger, run one Process block ...

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
```

Flesh out the elided setup by copying the working sequence from the existing victim test in the same file — same fixture, same trigger mechanism. The test must FAIL before the change (today the trigger is dropped: no serial > `max_serial_before` appears).

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh`
Expected: new test FAILS on `REQUIRE(newest_sounds)`; everything else passes.

- [ ] **Step 3: Implement**

In `grain_engine.h`, add private:

```cpp
    int FindOldestActiveGrain() const;
```

and reimplement the hook (keeping name and semantics — it exists so tests can drive victim selection deterministically):

```cpp
    // Test-only: marks the pool's true oldest grain for the click-free
    // pending-kill, exactly as Process()'s steal path does at saturation.
    void ForceAllocateGrainForTest() {
        int v = FindOldestActiveGrain();
        if (v >= 0) grains_[v].StartPendingKill();
    }
```

In `grain_engine.cpp`, simplify `AllocateGrain()` to the free-slot search only (move the victim-selection comment block onto the new helper):

```cpp
Grain* GrainEngine::AllocateGrain() {
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!grains_[i].active()) return &grains_[i];
    }
    return nullptr;
}

// Index of the active, not-yet-pending-kill grain with the lowest spawn
// serial. Array index does NOT track spawn order (slots are reused as
// grains finish), so pick by spawn_serial_.
// [keep the existing wraparound-analysis comment from the old AllocateGrain here]
int GrainEngine::FindOldestActiveGrain() const {
    int victim = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!grains_[i].active() || grains_[i].pending_kill()) continue;
        if (victim < 0 || grains_[i].spawn_serial() < grains_[victim].spawn_serial())
            victim = i;
    }
    return victim;
}
```

Replace the trigger loop in `Process()` (lines 271-283):

```cpp
    int active_before = ActiveGrainCount();

    // Start new grains at their trigger points. At saturation (CPU cap or
    // full pool), steal-and-replace: retire the oldest grain and start the
    // new one, so the newest events always sound (decided 2026-07-11).
    for (int t = 0; t < num_triggers; ++t) {
        Grain* g = nullptr;
        bool reused_active_slot = false;
        if (active_before < max_active) g = AllocateGrain();
        if (!g) {
            int victim = FindOldestActiveGrain();
            if (victim < 0) break;   // every active grain is already dying; drop the rest
            Grain* free_slot = AllocateGrain();
            if (free_slot) {
                // CPU-cap saturation with pool headroom: fade the victim out
                // click-free and start the new grain in a free slot (active
                // count exceeds the cap by 1 for <=36 samples).
                grains_[victim].StartPendingKill();
                g = free_slot;
            } else {
                // Pool truly full: hard-replace the victim. The new grain's
                // envelope opens at zero; the victim's cut is the accepted
                // cost of never dropping the newest event.
                g = &grains_[victim];
                reused_active_slot = true;
            }
        }
        auto gp = ComputeGrainParams(params, trigger_samples[t]);
        g->Start(gp);
        g->SetSpawnSerial(++spawn_serial_);
        if (!reused_active_slot) ++active_before;
    }
```

Check `Grain::Start()` (grain.cpp) clears `pending_kill_`/fallback state when reusing a slot — if it doesn't, clear them there so a hard-replaced grain doesn't inherit a pending kill. Update the now-stale comment at the old AllocateGrain site and the hook's comment in the header (the "the new trigger is simply dropped" language is no longer true).

- [ ] **Step 4: Run the full suite**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh`
Expected: all pass — including the existing victim-selection test (`pending_kill_count == 1`, victim is true oldest), which the reimplemented hook must keep green, and the kill-fade sequence test (0.75/0.5/0.25/0.0).

- [ ] **Step 5: Commit**

```bash
git add src/particules/dsp/src/grain/grain_engine.cpp src/particules/dsp/src/grain/grain_engine.h tests/particules_dsp/test_grain_kill.cpp
git commit -m "feat: grain-pool overflow steals oldest grain instead of dropping trigger"
```

---

### Task 6: Position fence ordering (harden the reverse-branch wrap)

**Files:**
- Modify: `src/particules/dsp/src/grain/grain_engine.cpp:185-202` (`ComputeGrainParams` tail)
- Test: `tests/particules_dsp/test_grain.cpp`

**Context:** The `isfinite(gp.position)` fence (line 202) currently runs *after* the reverse branch's `while (gp.position >= buf_size_f) gp.position -= buf_size_f;` (line 194). A `+Inf` position would spin that loop forever; NaN falls through. Safety today is incidental (upstream fences make non-finite unreachable); make it structural by moving the fence above the reverse branch.

- [ ] **Step 1: Write the test**

Add to `tests/particules_dsp/test_grain.cpp`, next to the time-NaN test (:364-400) — same fixture and params, plus reverse playback (negative pitch region; check how the existing tests/engine derive `reverse` — it comes from the pitch sign in `ComputeGrainParams`; set `params.pitch` to a negative value):

```cpp
TEST_CASE("GrainEngine: NaN TIME CV with reverse playback stays finite and terminates",
          "[engine][nan][reverse]") {
    TestBuffer tb(48000);
    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.1f;
    params.size = 0.5f;
    params.time = 0.5f;
    params.time_ar = 1.0f;
    params.time_cv = std::numeric_limits<float>::quiet_NaN();
    params.time_cv_connected = true;
    params.shape = 0.5f;
    params.pitch = -12.0f;   // reverse/downward pitch: exercises the reverse branch

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});
    bool all_finite = true;
    for (int block = 0; block < 200; ++block) {
        engine.Process(params, output.data(), 256);
        for (auto& f : output)
            if (!std::isfinite(f.l) || !std::isfinite(f.r)) all_finite = false;
    }
    REQUIRE(all_finite);
    REQUIRE(engine.ActiveGrainCount() > 0);
}
```

**Adaptation note:** confirm how reverse is selected (read `ComputeGrainParams` around line 166 — `reverse` already exists there). If `pitch = -12.0f` doesn't trigger the reverse path, use whatever parameter does. This test likely PASSES already (upstream fences); it exists to keep the hardening honest — the loop would hang the test (timeout) if a future change lets Inf through.

- [ ] **Step 2: Move the fence**

In `ComputeGrainParams`, move the fence block (with its comment) from after the reverse branch to before it:

```cpp
    float pos = static_cast<float>(buffer_->write_head()) - offset_frames;
    if (pos < 0.0f) pos += buf_size_f;
    gp.position = pos;

    // Guard against NaN/huge positions before they reach Grain::Start() —
    // and before the reverse wrap below, whose while-loop would spin
    // forever on +Inf and silently skip on NaN. Robust by construction,
    // not by upstream luck.
    if (!std::isfinite(gp.position)) gp.position = 0.0f;

    if (reverse) {
        // Offset start position to the END of the segment a forward grain
        // would play.  The reverse grain then reads backwards through the
        // same audio, producing true reversed playback of the intended
        // segment rather than reading into unrelated older audio.
        gp.position += span;
        while (gp.position >= buf_size_f) gp.position -= buf_size_f;
    }
```

(`span` is already fenced finite via the pitch_ratio fence at line 166, so `gp.position += span` cannot re-introduce non-finite values.)

- [ ] **Step 3: Run the full suite**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh`
Expected: all pass, including both NaN tests and the existing fmod-wrap-equivalence tests.

- [ ] **Step 4: Commit**

```bash
git add src/particules/dsp/src/grain/grain_engine.cpp tests/particules_dsp/test_grain.cpp
git commit -m "fix: move grain position fence above the reverse-branch wrap"
```

---

### Task 7: Dedicated pitch-NaN grain test (test-only)

**Files:**
- Test: `tests/particules_dsp/test_grain.cpp`

**Context:** The pitch_ratio fence (`grain_engine.cpp:166`: `if (!std::isfinite(gp.pitch_ratio)) gp.pitch_ratio = reverse ? -1.0f : 1.0f;`) is exercised only transitively today. Mirror the time-NaN test (:364-400) with a NaN pitch CV.

- [ ] **Step 1: Write the test**

```cpp
TEST_CASE("GrainEngine: NaN PITCH CV can't reach the grain as a NaN pitch ratio",
          "[engine][nan]") {
    // Mirror of the TIME-CV NaN test: a NaN pitch CV poisons pitch_ratio in
    // ComputeGrainParams; the isfinite fence must land it on ±1.0 so grains
    // keep firing and output stays finite (liveness AND recovery).
    TestBuffer tb(48000);
    GrainEngine engine;
    engine.Init(kSampleRate, &tb.buffer);

    ParticulesParameters params;
    params.trigger_mode = TriggerMode::kLatched;
    params.density = 0.1f;
    params.size = 0.5f;
    params.time = 0.5f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.pitch_ar = 1.0f;
    params.pitch_cv = std::numeric_limits<float>::quiet_NaN();
    params.pitch_cv_connected = true;

    std::vector<StereoFrame> output(256, {0.0f, 0.0f});
    bool all_finite = true;
    for (int block = 0; block < 200; ++block) {
        engine.Process(params, output.data(), 256);
        for (auto& f : output)
            if (!std::isfinite(f.l) || !std::isfinite(f.r)) all_finite = false;
    }
    REQUIRE(all_finite);
    REQUIRE(engine.ActiveGrainCount() > 0);

    // Recovery: clear the NaN and confirm grains still fire.
    params.pitch_cv = 0.0f;
    for (int block = 0; block < 50; ++block)
        engine.Process(params, output.data(), 256);
    REQUIRE(engine.ActiveGrainCount() > 0);
}
```

**Adaptation note:** verify the exact field names (`pitch_ar`, `pitch_cv`, `pitch_cv_connected`) against `ParticulesParameters` and the `ar_pitch_.Process` call at grain_engine.cpp:144-145; match whatever the time-NaN test's fields are named for pitch.

- [ ] **Step 2: Run and verify it passes (fence exists); then prove it bites**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh`
Expected: PASS. Then temporarily comment out the pitch fence at grain_engine.cpp:166, rerun, and confirm the new test FAILS (that proves it guards the right line). Restore the fence, rerun, all green.

- [ ] **Step 3: Commit**

```bash
git add tests/particules_dsp/test_grain.cpp
git commit -m "test: dedicated pitch-NaN grain fence test"
```

---

### Task 8: Interpolation-tail sync test (test-only)

**Files:**
- Test: `tests/particules_dsp/test_buffer.cpp`

**Context:** Tail-mirroring on write exists at `recording_buffer.cpp:53-64` (writes into the first `kInterpolationTail` frames also copy to `buffer_[size_ + write_head_]`), and `kInterpolationTail = 4` (`include/particules_dsp/types.h:70`). No test writes with `write_head_ < kInterpolationTail` and then reads fractionally across the `size_` boundary. Existing neighbors: the freeze tail test (:176-198), Hermite-exact-for-linear (:229). `InterpolateHermite` is directly callable from tests.

- [ ] **Step 1: Write the test**

```cpp
TEST_CASE("RecordingBuffer: fractional read across the size_ boundary sees post-wrap writes via the tail mirror",
          "[buffer][tail]") {
    // Small buffer; fill one full pass with a known ramp, then write 2 more
    // frames (write_head_ wraps to 0, then 1 -- both < kInterpolationTail),
    // overwriting frames 0 and 1 AND their tail mirrors.
    RecordingBuffer buf;
    // ... allocate/Init exactly as the neighboring tests do (match their
    //     Init/ownership pattern; use a size like 64 frames, stereo) ...
    const size_t N = 64;
    for (size_t i = 0; i < N; ++i)
        buf.Write(0.01f * static_cast<float>(i), 0.01f * static_cast<float>(i));
    // Overwrite frames 0 and 1 with sentinel values after the wrap:
    buf.Write(0.9f, 0.9f);   // frame 0
    buf.Write(0.8f, 0.8f);   // frame 1

    // Fractional Hermite read at position N - 0.5: taps are frames
    // N-2, N-1, N (mirror of frame 0), N+1 (mirror of frame 1).
    float l = 0.f, r = 0.f;
    buf.ReadHermiteStereoFast(static_cast<float>(N) - 0.5f, &l, &r);

    float expected = InterpolateHermite(
        0.01f * static_cast<float>(N - 2),   // xm1: frame N-2
        0.01f * static_cast<float>(N - 1),   // x0:  frame N-1
        0.9f,                                // x1:  frame 0 via tail mirror
        0.8f,                                // x2:  frame 1 via tail mirror
        0.5f);
    REQUIRE(l == Approx(expected).margin(1e-6f));
    REQUIRE(r == Approx(expected).margin(1e-6f));

    // The out-of-line reader must agree with the fast path here.
    float l2 = 0.f, r2 = 0.f;
    buf.ReadHermiteStereo(static_cast<float>(N) - 0.5f, &l2, &r2);
    REQUIRE(l2 == Approx(l).margin(1e-6f));
    REQUIRE(r2 == Approx(r).margin(1e-6f));
}
```

**Adaptation note:** match the construction/Init and `Write` signature to the neighboring tests in `test_buffer.cpp` (e.g. `Write and ReadLinear roundtrip` at :25) — the elided allocation lines must copy their pattern. Verify `InterpolateHermite`'s exact signature/tap order against the test at :229 and the header. Also confirm `ReadHermiteStereoFast`'s tap layout (`i_1 = i0-1, i0, i1, i2`) — at position N−0.5, `i0 = N−1`, so taps are N−2, N−1, N, N+1 as above.

- [ ] **Step 2: Run to verify it passes; then prove it bites**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh`
Expected: PASS (the mirror-on-write code exists). Prove it guards the right code: temporarily comment out the tail-sync block at recording_buffer.cpp:55-59, rerun, confirm the new test FAILS (reads stale 0.00/0.01 instead of 0.9/0.8). Restore, rerun, green.

- [ ] **Step 3: Commit**

```bash
git add tests/particules_dsp/test_buffer.cpp
git commit -m "test: tail mirror serves fractional reads across the buffer seam"
```

---

### Task 9: Quality-mode crossfade dedup (behavior-preserving refactor)

**Files:**
- Modify: `src/particules/dsp/src/quality/quality_processor.cpp:113-122, 183-189` (and the corresponding class header for the helper decl)

**Context:** `ProcessInput` and `ProcessOutput` contain byte-identical crossfade blocks differing only in the counter member. Extract one helper. Existing quality tests pin behavior; no new tests.

- [ ] **Step 1: Extract the helper**

In the QualityProcessor class (header), add private:

```cpp
    // Crossfade from the unprocessed input to the new mode's output for
    // kModeXfadeSamples after a mode switch, avoiding the abrupt timbral
    // jump (especially into/out of tape mode's mono sum + mu-law).
    static void ApplyModeXfade(int& counter, const StereoFrame& input, StereoFrame& result) {
        if (counter > 0) {
            float mix = static_cast<float>(counter) / static_cast<float>(kModeXfadeSamples);
            // mix goes from 1 (all old = raw input) to 0 (all new mode)
            result.l = input.l * mix + result.l * (1.0f - mix);
            result.r = input.r * mix + result.r * (1.0f - mix);
            counter--;
        }
    }
```

(If `kModeXfadeSamples` lives in the .cpp, define the helper in the .cpp instead — file-local function taking the constant implicitly. Match existing style.)

Replace block A (ProcessInput, :113-122) with:

```cpp
    ApplyModeXfade(input_xfade_counter_, input, result);
```

Replace block B (ProcessOutput, :183-189) with:

```cpp
    ApplyModeXfade(output_xfade_counter_, input, result);
```

The arithmetic must stay literally identical (same order of operations) so results are bit-exact.

- [ ] **Step 2: Run the full suite**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh`
Expected: all pass unchanged.

- [ ] **Step 3: Commit**

```bash
git add src/particules/dsp/src/quality/quality_processor.cpp src/particules/dsp/src/quality/quality_processor.h
git commit -m "refactor: share quality-mode crossfade between input and output paths"
```

---

### Task 10: RecordingBuffer read-path consolidation (behavior-preserving refactor)

**Files:**
- Modify: `src/particules/dsp/src/buffer/recording_buffer.h`, `recording_buffer.cpp`

**Context:** Only `ReadHermiteStereoFast` has production callers (grain.h:69). `ReadHermite` (.cpp:105), `ReadHermiteStereo` (.cpp:150), `ReadLinear` (.cpp:192) are test-only and repeat an identical guard+wrap prologue. `ReadLinearStereoFast` (header inline, :43) has **zero callers anywhere** — dead.

- [ ] **Step 1: Delete `ReadLinearStereoFast`**

Remove the inline definition from `recording_buffer.h`. Verify no callers: `grep -rn "ReadLinearStereoFast" src/ tests/ --include=*.cpp --include=*.h --include=*.hpp` (ignore hits under any `build/` dirs) — expected: no hits after removal.

- [ ] **Step 2: Extract the shared guard/wrap prologue**

Add a private method:

```cpp
    // Shared guard + wrap for the out-of-line readers: rejects an empty
    // buffer and non-finite positions (read yields silence), wraps position
    // into [0, size_) in O(1) via fmod, and splits into integer index +
    // fraction. Tap-index wrapping stays per-reader (Hermite vs linear).
    bool ResolveReadPosition(float position, size_t* i0, float* frac) const {
        if (size_ == 0 || !buffer_) return false;
        if (!std::isfinite(position)) return false;
        float size_f = static_cast<float>(size_);
        position = std::fmod(position, size_f);
        if (position < 0.0f) position += size_f;
        int pos_int = static_cast<int>(position);
        *frac = position - static_cast<float>(pos_int);
        *i0 = static_cast<size_t>(pos_int);
        return true;
    }
```

Rewrite `ReadHermite`, `ReadHermiteStereo`, `ReadLinear` to call it (returning 0.0f / writing zeros when it returns false, exactly as today — note `ReadHermiteStereo` additionally checks `channels_ < 2`; keep that check in the caller). Keep each reader's tap-index computation (`i_1`, `i1`, `i2` and their `size_ + kInterpolationTail` wraps) unchanged. Mark the three readers' doc comments as test-only reference implementations (production uses `ReadHermiteStereoFast`). Do NOT touch `ReadHermiteStereoFast` itself — it is the hot path and deliberately guard-free.

- [ ] **Step 3: Run the full suite**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh`
Expected: all pass unchanged — test_buffer.cpp exercises all three readers heavily, including the non-finite-position and huge-position-fmod tests.

- [ ] **Step 4: Commit**

```bash
git add src/particules/dsp/src/buffer/recording_buffer.h src/particules/dsp/src/buffer/recording_buffer.cpp
git commit -m "refactor: consolidate RecordingBuffer read guards; drop dead ReadLinearStereoFast"
```

---

### Task 11: Wrapper hygiene — density-control naming, control_conditioner include, deferred Clear drain

**Files:**
- Modify: `src/particules/particules_density_control.h`
- Move: `src/particules/dsp/src/util/control_conditioner.h` → `src/particules/dsp/include/particules_dsp/control_conditioner.h`
- Modify: `src/particules/Particules.cpp` (include path + onReset drain)

**Context:** three review items bundled (all tiny, same area):
1. `particules_density_control.h` takes RAW voltage but names the param `conditioned_density_cv`, and includes `<algorithm>`, `<cmath>`, `<cstddef>` — none used.
2. `Particules.cpp:3` includes `"dsp/src/util/control_conditioner.h"` — a reach into the dsp library's internals; promote the header to the public include dir.
3. A queued menu "Clear buffer" (`clear_requested_`, consumed only inside the `BlockReady()` branch, Particules.cpp:440-444) can be arbitrarily delayed while bypassed. Drain it in `onReset` (which already calls `processor_.ClearBuffer()` directly — the queued flag should be consumed there so a stale request doesn't fire after reset).

- [ ] **Step 1: Fix `particules_density_control.h`**

Replace the file body:

```cpp
#pragma once

namespace particules {

// Slow (non-audio-rate) density CV mapping: raw ±5 V → ±1.0 density offset.
inline float ComputeSlowDensityOffset(float density_cv_volts) {
    return density_cv_volts * 0.2f;
}

}  // namespace particules
```

(Parameter renamed to say what it actually receives — the sole production caller at Particules.cpp:337 passes `inputs[DENSITY_INPUT].getVoltage()` raw; the three unused includes dropped.)

- [ ] **Step 2: Promote `control_conditioner.h`**

```bash
git mv src/particules/dsp/src/util/control_conditioner.h src/particules/dsp/include/particules_dsp/control_conditioner.h
```

Then find every includer: `grep -rn "control_conditioner.h" src/ tests/ metamodule/ vcv/Makefile --include=*.cpp --include=*.h --include=*.hpp --include=*.cc` (ignore `build/` dirs). Update each:
- `Particules.cpp:3` → `#include <particules_dsp/control_conditioner.h>` (the vcv Makefile and metamodule CMake already put `src/particules/dsp/include` on the include path — verify with a build).
- Any dsp-internal includers → `#include <particules_dsp/control_conditioner.h>` too (the dsp sources compile with the same include dir; check `tests/particules_dsp/CMakeLists.txt` includes it).
- If the header's include guard or namespace references its old location in comments, update them.

- [ ] **Step 3: Drain the queued clear in `onReset`**

In `Particules.cpp` `onReset` (lines 236-256), next to the existing direct clear:

```cpp
		clear_requested_.store(false);   // reset clears now; drop any queued menu clear
		processor_.ClearBuffer();   // 4 s buffer, feedback path, reverb tail
```

(The bypass half of the review item is accepted-as-documented: VCV stops calling `process()` while bypassed, the flag survives via `exchange`, and the clear lands at the first block boundary after un-bypass. `onReset` is the one place a stale queued clear could fire surprisingly — after an intentional reset already cleared everything.)

- [ ] **Step 4: Verify builds and tests**

Run: `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4 && ./tests/particules_dsp/run.sh` — expected: all pass (the density-control test calls by position, unaffected by the rename).
Run: `make -C /Users/gabrielroth/Dev/RobotBoy/.worktrees/review-round-4/vcv -j8` — expected: plugin dylib builds cleanly with the new include path.

- [ ] **Step 5: Commit**

```bash
git add -A src/particules tests
git commit -m "refactor: density-control naming, promote control_conditioner header, drain queued clear on reset"
```

---

### Task 12: Build/metadata cleanups

**Files:**
- Modify: `plugin.json`
- Modify: `vcv/Makefile`
- Modify: `metamodule/CMakeLists.txt`
- Delete: `tests/beads/` (orphaned build tree), `presets/` (empty dir)
- Verify: `metamodule/plugin-mm.json`

**Context:** review §"Plugin glue" items 1-4. Facts from exploration: `plugin.json` (canonical at repo root; `vcv/plugin.json` is a git-ignored sync copy) has keys slug/name/version/license/brand/author/modules, version "2.0.1"; git remote is `https://github.com/gabriel-roth/RobotBoy.git`; `vcv/src/` contains ONLY escaped object files (live sources are at repo root — Rack's `compile.mk` turns `../src/foo.cpp` into `build/../src/foo.cpp.o` = `vcv/src/foo.cpp.o`); Rack SDK's `plugin.mk` clean target only removes `build`, `$(TARGET)`, `dist`; the parse-time sync at `vcv/Makefile:10` is `_sync := $(shell cp ../plugin.json plugin.json && rm -rf res && cp -R ../res res)` (exit status discarded); `metamodule/CMakeLists.txt:12` is `project(RobotBoy VERSION 2.0.1 LANGUAGES C CXX ASM)` and `:49` is `target_compile_options(RobotBoy PRIVATE -std=c++20)`; `DISTRIBUTABLES += res plugin.json` at `vcv/Makefile:24`; root `presets/` exists and is empty.

- [ ] **Step 1: plugin.json — add sourceUrl and manualUrl**

Add to the top level of `/…/review-round-4/plugin.json` (after `"license"`):

```json
  "sourceUrl": "https://github.com/gabriel-roth/RobotBoy",
  "manualUrl": "https://github.com/gabriel-roth/RobotBoy#readme",
```

Validate: `python3 -c "import json; json.load(open('plugin.json'))"` — no output, exit 0.

- [ ] **Step 2: vcv/Makefile — loud sync + clean up escaped objects**

Replace line 10's sync with a failure-checked version:

```make
_sync := $(shell (cp ../plugin.json plugin.json && rm -rf res && cp -R ../res res) || echo SYNC_FAILED)
ifneq (,$(findstring SYNC_FAILED,$(_sync)))
$(error failed to sync plugin.json/res from repo root)
endif
```

Extend clean (append at the end of `vcv/Makefile`, after the `include $(RACK_DIR)/plugin.mk` line — extra prerequisites merge with plugin.mk's clean rule):

```make
# Rack's compile.mk maps our ../src/... sources to build/../src/....o, which
# normalizes to vcv/src/ — outside build/, so plugin.mk's clean misses them.
clean: clean-escaped-objects
.PHONY: clean-escaped-objects
clean-escaped-objects:
	@if [ -d src ]; then find src \( -name '*.o' -o -name '*.d' \) -delete; find src -type d -empty -delete; fi
```

Verify: `make -C vcv clean && ls vcv/src 2>&1` — expected: `No such file or directory` (or a dir with no .o/.d files). Then `make -C vcv -j8` still builds.

- [ ] **Step 3: metamodule/CMakeLists.txt — single version source + CXX-scoped c++20**

Replace line 12's hardcoded version by deriving from plugin.json (before `project()`):

```cmake
file(READ ${CMAKE_CURRENT_LIST_DIR}/../plugin.json ROBOTBOY_PLUGIN_JSON)
string(JSON ROBOTBOY_VERSION GET ${ROBOTBOY_PLUGIN_JSON} version)
project(RobotBoy VERSION ${ROBOTBOY_VERSION} LANGUAGES C CXX ASM)
```

(`string(JSON …)` needs CMake ≥ 3.19; check the file's `cmake_minimum_required` and raise it to 3.19 if lower.)

Replace line 49:

```cmake
target_compile_options(RobotBoy PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>)
```

Verify: if the MetaModule toolchain is configured on this machine, run the cmake configure step (see `metamodule/README` or the existing build dir's CMakeCache for the exact command) and confirm it configures with version 2.0.1. If the toolchain isn't available, verify the JSON parse logic standalone:
`cmake -P` a scratch script in the scratchpad dir that does the same `file(READ)`+`string(JSON)` against `plugin.json` and `message()`s the result — expected output `2.0.1`.

- [ ] **Step 4: Delete orphans**

```bash
git rm -r --cached tests/beads 2>/dev/null; rm -rf tests/beads
rmdir presets
```

(`tests/beads/` contains only a stale CMake build tree referencing the pre-rename `beads_dsp` layout; live tests are `tests/particules_dsp/`. `presets/` is empty and untracked-by-content — if `git rm` reports nothing tracked, plain `rm -rf`/`rmdir` is fine. If the user's intent was to ship presets someday, an empty dir does nothing anyway — VCV only needs it listed in DISTRIBUTABLES when it has content; recreate it then.)

- [ ] **Step 5: Verify plugin-mm.json schema keys**

Find the MetaModule SDK locally: check `metamodule/CMakeLists.txt` for the SDK path it references (e.g. a `metamodule-plugin-sdk` checkout). Look for the SDK's plugin-mm.json documentation or an example plugin's plugin-mm.json and compare key names against ours (`MetaModuleBrandName`, `MetaModuleBrandSlug`, `MetaModulePluginMaintainer`, `MetaModuleDescription`, `MetaModuleIncludedModules[].{slug,name,displayName}`). Fix any wrong/unknown keys. If no SDK/docs are locally available, report that the check still can't be completed rather than guessing — leave the file untouched and say so in the task report.

- [ ] **Step 6: Full verification + commit**

Run all three: `./tests/particules_dsp/run.sh`, `cd tests && ./run.sh` (Lane 1), `make -C vcv -j8`.
Expected: all green, plugin builds.

```bash
git add plugin.json vcv/Makefile metamodule/CMakeLists.txt metamodule/plugin-mm.json
git rm -r tests/beads 2>/dev/null || true
git commit -m "build: sourceUrl/manualUrl, derived version, scoped c++20, clean escaped objects"
```

---

## Completion

After both streams finish:
1. Run the full verification set one final time in each worktree (Loooop lane in `loooop-track`; both lanes + `make -C vcv` in `review-round-4`).
2. Request code review per superpowers:requesting-code-review.
3. Update `code-review-2026-07-08.md` on `main`: strike through / mark done the implemented items (dsp lib #1, #3; Loooop follow-ups #1-3; Particules #1; all three refactor groups; the four test-gap bullets; plugin-glue items 1-4 as applicable), and note anything that couldn't be verified (e.g. plugin-mm.json schema if no SDK docs were found).
4. Do NOT merge `review-round-4` or `loooop-track` — merging is the user's call (loooop-track also gates on pending USER CHECKS).
