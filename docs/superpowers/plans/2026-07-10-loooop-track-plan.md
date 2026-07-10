# Loooop Track Implementation Plan (F1+F2, Q1, Q2, Q3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the Loooop track of the 2026-07-09 backlog: four-way write modes (F1), overdub start/stop ramps (F2), Catmull-Rom playback interpolation (Q1), one-shot fade-out (Q2), and smoothed level/pan/dry-wet (Q3).

**Architecture:** All DSP goes in the platform-agnostic `LoopEngine` (headless-testable, shared by VCV Loooop/Lop and the MetaModule cores); module files gain one new switch param + context-menu item (F1) and two/five one-pole smoothers (Q3). No panel art changes — the write mode is menu-only.

**Tech Stack:** C++20. Lanes: `tests/run.sh` (g++ headless + Python guards), `make -C vcv -j8` (Rack SDK at `\~/Dev/Rack-SDK`), `cmake --build metamodule/build -j8` (SDK default path is baked into `metamodule/CMakeLists.txt`).

**Spec:** `docs/superpowers/specs/2026-07-09-feature-and-perf-backlog-design.md` (F1, F2, Q1, Q2, Q3). Read-only — do not edit the spec.

## Global Constraints

- Work in the worktree `.worktrees/loooop-track` on branch `loooop-track`. All paths below are relative to the worktree root.
- One commit per task; messages one short sentence ≤15 words, NO Co-Authored-By or AI attribution.
- All lanes green after every task. First time in this worktree, configure the MetaModule build: `cmake -S metamodule -B metamodule/build` (generator: Unix Makefiles), then `cmake --build metamodule/build -j8`.
- Fast single-test loop (from `tests/`): `mkdir -p ../build/tests && g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules -I../src/vendor/beads_dsp/include -o ../build/tests/test_loop_engine loooop/test_loop_engine.cpp ../src/loooop/dsp/LoopEngine.cpp && ../build/tests/test_loop_engine`
- `LoopEngine` stays Rack-free (no `rack::` / `plugin.hpp` includes) — it must keep compiling in the headless lane and MetaModule build.
- No GUI/simulator/listening steps in tasks — anything needing eyes/ears goes in the USER CHECK section at the end.
- Never weaken an existing test's assertions, EXCEPT the three Q1-sanctioned expectation changes listed in Task 4 (seam-adjacent interpolation values change by design; each new value is derived in the task).
- Sanctioned behavior changes only: overdub write path per mode (F1); recording stop becomes \~5 ms soft on overdub passes (F2); interpolated read values near loop/window seams (Q1); one-shot final \~5 ms attenuated (Q2); level/pan/mix steps glide over \~2 ms (Q3). Everything else behavior-preserving.

---

### Task 1: F1 — engine write modes (Replace / Add / Layer / Decay)

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp`
- Modify: `src/loooop/dsp/LoopEngine.cpp` (overdub branch of `process`, `toggleRecord`, `reset`, `setSampleRate`)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Produces: `enum class LoopEngine::WriteMode { Add = 0, Replace = 1, Layer = 2, Decay = 3 }`; `void setWriteMode(WriteMode m)`; `WriteMode writeMode() const`; `static constexpr float LAYER_FEEDBACK = 0.9f` (public — tests and by-ear tuning reference it). Task 3 wires modules to `setWriteMode`; Task 2 folds this write expression into the ramp formula.
- **`Add` must stay index 0**: the MetaModule patch loader zero-inits unset alt-params, so patches saved before this feature must load in Add (the current overdub behavior).

- [ ] **Step 1: Write the failing tests** — append to `tests/loooop/test_loop_engine.cpp` (before `main`), and register each in `main()` after `test_overdub_gate();`:

```cpp
static void test_write_mode_replace() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);    // loop = 1,1,1,1
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Replace);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(2.f);    // full destructive pass
    e.toggleRecord();
    check(near(e.process(0.f), 2.f), "replace: out[0]==2 (old content gone)");
    check(near(e.process(0.f), 2.f), "replace: out[1]==2");
}

static void test_write_mode_layer() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Layer);
    e.setLevel(0, 0.f);                            // mute so overdub input is isolated
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(0.5f);   // buf -> 1*FB + 0.5
    e.toggleRecord();
    e.setLevel(0, 1.f);
    const float expected = LoopEngine::LAYER_FEEDBACK + 0.5f;
    check(near(e.process(0.f), expected), "layer: buf == old*FB + new");
    // an idle loop never fades: play 20 more samples, value unchanged
    float v = 0.f; for (int i = 0; i < 19; ++i) v = e.process(0.f);
    check(near(v, expected), "layer: idle playback does not decay");
}

static void test_write_mode_decay_at_low_rate_matches_layer() {
    // At the 10 Hz test rate the Decay LP coefficient saturates to 1
    // (passthrough), so Decay == Layer sample-exactly there.
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Decay);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(0.5f);
    e.toggleRecord();
    check(near(e.process(0.f), LoopEngine::LAYER_FEEDBACK + 0.5f),
          "decay@10Hz: identical to layer (LP is passthrough)");
}

static void test_write_mode_decay_rolls_off_highs() {
    // Same per-pass feedback as Layer, plus a one-pole LP in the write path:
    // a Nyquist-rate square must decay much faster in Decay than in Layer.
    // Loop is 4800 samples; only the region past sample 1000 is measured so
    // the Task-2 write-gain ramps (240 samples at each edge) can't touch it.
    auto passRms = [](LoopEngine::WriteMode m) {
        LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
        e.toggleRecord();
        for (int i = 0; i < 4800; ++i) e.process((i & 1) ? -1.f : 1.f);
        e.toggleRecord();
        e.setWriteMode(m);
        e.toggleRecord();
        for (int i = 0; i < 4800; ++i) e.process(0.f);   // one silent overdub pass
        e.toggleRecord();
        for (int i = 0; i < 300; ++i) e.process(0.f);    // let any stop ramp finish
        e.restartHead(0);
        double acc = 0.0; int n = 0;
        for (int i = 0; i < 4800; ++i) {
            float v = e.process(0.f);
            if (i >= 1000) { acc += double(v) * v; ++n; }
        }
        return std::sqrt(acc / n);
    };
    float rLayer = (float)passRms(LoopEngine::WriteMode::Layer);
    float rDecay = (float)passRms(LoopEngine::WriteMode::Decay);
    check(near(rLayer, LoopEngine::LAYER_FEEDBACK, 0.02f),
          "decay_hf: layer pass keeps FB*amplitude at Nyquist");
    check(rDecay < 0.5f * rLayer, "decay_hf: Decay kills HF much faster than Layer");
}

static void test_write_mode_peaks_track_decay() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);   // peakBinSize == 1
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Layer);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(0.f);          // silent pass: buf *= FB
    e.toggleRecord();
    check(near(e.peakMaxs(0)[0], LoopEngine::LAYER_FEEDBACK),
          "peaks: display peaks track decayed content");
}
```

- [ ] **Step 2: Run to verify failure.** Fast single-test loop from Global Constraints. Expected: compile FAILURE (`WriteMode` not a member of `LoopEngine`).

- [ ] **Step 3: Implement.** In `LoopEngine.hpp`, public section (after `setCrossfade`):

```cpp
    // Overdub write mode (F1). Add must stay index 0: the MetaModule patch
    // loader zero-inits unset alt-params, so pre-existing patches must land
    // on the legacy sum-into-buffer behavior.
    enum class WriteMode { Add = 0, Replace = 1, Layer = 2, Decay = 3 };
    void setWriteMode(WriteMode m) { writeMode_ = m; }
    WriteMode writeMode() const { return writeMode_; }
    // Fixed sound-on-sound decay per overdub pass (Layer/Decay). No user
    // control by design; tune by ear on the simulator.
    static constexpr float LAYER_FEEDBACK = 0.9f;
```

Private members (near `overdubEnabled_`):

```cpp
    WriteMode writeMode_ = WriteMode::Add;
    // Decay-mode one-pole LP along the write path (HF rolloff per pass).
    // Corner is fixed (tune by ear); coefficient set from the sample rate.
    static constexpr float DECAY_LP_HZ = 6000.f;
    float decayLpL_ = 0.f, decayLpR_ = 0.f;
    float decayLpA_ = 1.f;
    float writeFeedback() const {
        switch (writeMode_) {
            case WriteMode::Replace: return 0.f;
            case WriteMode::Layer:
            case WriteMode::Decay:   return LAYER_FEEDBACK;
            default:                 return 1.f;
        }
    }
```

In `LoopEngine.cpp` `reset()` AND `setSampleRate()` (both places that recompute `xfadeSamples_`):

```cpp
    // Underflows to exactly 1.0 (passthrough) at very low test rates.
    decayLpA_ = 1.f - std::exp(-2.f * 3.14159265f * DECAY_LP_HZ / sampleRate);
```

In `reset()` also re-zero the LP state: `decayLpL_ = decayLpR_ = 0.f;`

In `toggleRecord()`, overdub-start branch (inside `if (!recording_)`, when `loopLen_ > 0`) seed the LP so the first Decay write doesn't fade in from zero:

```cpp
        if (loopLen_ > 0 && writeMode_ == WriteMode::Decay) {
            decayLpL_ = bufL_[0];
            decayLpR_ = bufR_[0];
        }
```

Replace the overdub write in `process()` (currently `bufL_[writeIdx_] += inL;` etc., `LoopEngine.cpp:332-333`):

```cpp
        } else {                             // overdub: mode-dependent write, wrap at loop length
            const float fb = writeFeedback();
            const float oldL = bufL_[writeIdx_], oldR = bufR_[writeIdx_];
            float fbL = oldL, fbR = oldR;
            if (writeMode_ == WriteMode::Decay) {
                decayLpL_ += decayLpA_ * (oldL - decayLpL_); fbL = decayLpL_;
                decayLpR_ += decayLpA_ * (oldR - decayLpR_); fbR = decayLpR_;
            }
            // Add (fb=1) reduces to old + in — the legacy sum, bit-exact.
            bufL_[writeIdx_] = oldL + (fb * fbL - oldL) + inL;
            bufR_[writeIdx_] = oldR + (fb * fbR - oldR) + inR;
            writePeak(writeIdx_, bufL_[writeIdx_], bufR_[writeIdx_]);
            ++writeIdx_;
            if (writeIdx_ >= loopLen_) writeIdx_ = 0;
        }
```

(Task 2 inserts `odGain_` into this expression; keep the `old + (…) + in` shape now so that diff is minimal.)

- [ ] **Step 4: Run tests.** Fast loop: all new tests pass; every pre-existing test (esp. `test_overdub_sums`, `test_peaks_overdub_and_clear`) still passes.
- [ ] **Step 5: Full lanes:** `tests/run.sh`, `make -C vcv -j8`, `cmake --build metamodule/build -j8`.
- [ ] **Step 6: Commit:** `feat: four-way overdub write modes in LoopEngine`

---

### Task 2: F2 — overdub start/stop write-gain ramps

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp`, `src/loooop/dsp/LoopEngine.cpp` (`toggleRecord`, `clear`, `reset`, overdub branch of `process`)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Consumes: Task 1's `writeFeedback()` / write expression.
- Produces: overdub write becomes `buf = old + odGain·(fb·fbSrc − old) + odGain·in`, `odGain` ramping linearly 0→1 on overdub start and 1→0 on stop over `xfadeSamples_` (\~5 ms; instant when `xfadeSamples_ == 0`, which preserves every 10 Hz exact-value test). `isRecording()` stays true until the stop ramp completes. No public API change.

- [ ] **Step 1: Write the failing tests** (append + register in `main()`):

```cpp
static void test_overdub_ramps_declick() {
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);  // xfade = 240
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(0.f);   // silent 1000-sample loop
    e.toggleRecord();
    e.toggleRecord();                                 // overdub start: gain ramps 0 -> 1
    for (int i = 0; i < 500; ++i) e.process(1.f);
    e.toggleRecord();                                 // stop: gain ramps 1 -> 0
    check(e.isRecording(), "ramps: still recording right after stop toggle");
    for (int i = 0; i < 239; ++i) e.process(1.f);
    check(e.isRecording(), "ramps: still recording 239 samples into stop ramp");
    for (int i = 0; i < 3; ++i) e.process(1.f);
    check(!e.isRecording(), "ramps: recording ends when the ramp completes");
    // The buffer holds the write-gain envelope (input was constant 1).
    e.restartHead(0);
    static float buf[1000];
    for (int i = 0; i < 1000; ++i) buf[i] = e.process(0.f);
    check(near(buf[0],   0.f,  0.01f),  "ramps: first overdub sample at gain 0");
    check(near(buf[120], 0.5f, 0.01f),  "ramps: up-ramp midpoint");
    check(near(buf[300], 1.f,  0.001f), "ramps: full gain after up-ramp");
    check(near(buf[620], 0.5f, 0.01f),  "ramps: down-ramp midpoint");
    check(near(buf[745], 0.f,  0.01f),  "ramps: zero at ramp end");
    bool mono = true;
    for (int i = 501; i < 745; ++i) if (buf[i] > buf[i-1] + 1e-4f) mono = false;
    check(mono, "ramps: stop ramp is monotonic");
}

static void test_stop_ramp_rearm() {
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(0.f);
    e.toggleRecord();
    e.toggleRecord();
    for (int i = 0; i < 500; ++i) e.process(1.f);    // gain settled at 1
    e.toggleRecord();                                 // stop ramp begins
    for (int i = 0; i < 120; ++i) e.process(1.f);     // gain ~0.5
    e.toggleRecord();                                 // re-arm: ramp back up from 0.5
    check(e.isRecording(), "rearm: still recording");
    for (int i = 0; i < 130; ++i) e.process(1.f);     // gain back to 1 by ~write 740
    e.toggleRecord();
    for (int i = 0; i < 250; ++i) e.process(1.f);     // final stop completes
    check(!e.isRecording(), "rearm: final stop completes");
    e.restartHead(0);
    static float buf[1000];
    for (int i = 0; i < 1000; ++i) buf[i] = e.process(0.f);
    check(near(buf[618], 0.5f, 0.02f), "rearm: dip bottoms out ~0.5, no snap to 0");
    // gain reached 1 at write ~740; writes 740..749 land before the final stop toggle
    check(near(buf[745], 1.f,  0.01f), "rearm: recovered to full gain");
    bool noSnap = true;
    for (int i = 501; i < 999; ++i)
        if (std::fabs(buf[i] - buf[i-1]) > 0.006f) noSnap = false;   // step is 1/240
    check(noSnap, "rearm: per-sample write-gain delta never exceeds the ramp step");
}

static void test_ramp_layer_combined_expression() {
    // Mid-ramp the write must follow buf = old + g·(fb·old − old) + g·in.
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);   // loop of constant 1
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Layer);
    e.toggleRecord();
    for (int i = 0; i < 500; ++i) e.process(0.5f);
    e.toggleRecord();
    for (int i = 0; i < 250; ++i) e.process(0.5f);   // stop ramp completes
    e.restartHead(0);
    static float buf[1000];
    for (int i = 0; i < 1000; ++i) buf[i] = e.process(0.f);
    auto expect = [](float g) {
        const float fb = LoopEngine::LAYER_FEEDBACK;   // old = 1, in = 0.5
        return 1.f + g * (fb - 1.f) + g * 0.5f;
    };
    check(near(buf[120], expect(0.5f), 0.005f), "combined: mid-up-ramp expression");
    check(near(buf[300], expect(1.f),  0.005f), "combined: full-gain expression");
    check(near(buf[620], expect(0.5f), 0.005f), "combined: mid-stop-ramp expression");
}
```

- [ ] **Step 2: Run to verify failure.** Expected: `ramps: first overdub sample at gain 0` FAILS (buffer holds full-gain writes; `isRecording` checks fail too).

- [ ] **Step 3: Implement.** `LoopEngine.hpp` private members:

```cpp
    // F2 overdub declick: write gain ramps 0->1 on overdub start, 1->0 on
    // stop, over xfadeSamples_. Gain is applied write-then-advance, so the
    // first sample of an overdub pass is written at gain 0.
    float odGain_ = 1.f;
    float odGainStep_ = 0.f;   // per-sample increment; 0 when xfadeSamples_==0
    bool stopPending_ = false;
```

`LoopEngine.cpp` — `toggleRecord()` becomes:

```cpp
void LoopEngine::toggleRecord() {
    // Overdub gate: with a loop already frozen, a new record pass is an
    // overdub — ignore the toggle when overdub is disabled. Stopping an
    // in-progress recording and the initial record pass are never gated.
    if (!recording_ && loopLen_ > 0 && !overdubEnabled_) return;
    if (!recording_) {
        recording_ = true;
        writeIdx_ = 0;                  // record/overdub always starts at loop start (v1)
        lastPeakBin_ = UINT32_MAX;          // first write re-seeds its bin
        if (loopLen_ == 0) {
            odGain_ = 1.f; odGainStep_ = 0.f;   // initial pass: overwrite, no ramp
        } else {
            odGain_ = xfadeSamples_ ? 0.f : 1.f;
            odGainStep_ = xfadeSamples_ ? 1.f / static_cast<float>(xfadeSamples_) : 0.f;
            stopPending_ = false;
            if (writeMode_ == WriteMode::Decay) {
                decayLpL_ = bufL_[0];
                decayLpR_ = bufR_[0];
            }
        }
        dispRecording_.store(true, std::memory_order_relaxed);
        dispRecLen_.store(0, std::memory_order_relaxed);
    } else if (loopLen_ == 0) {
        // Stopping the initial pass freezes immediately: there is no prior
        // content to blend with, and the seam crossfade declicks the join.
        recording_ = false;
        loopLen_ = writeIdx_;
        dispRecording_.store(false, std::memory_order_relaxed);
        dispLoopLen_.store(static_cast<std::uint32_t>(loopLen_), std::memory_order_relaxed);
    } else if (odGainStep_ > 0.f) {
        // Overdub stop request: ramp the write gain down while continuing to
        // write; recording_ clears when the gain reaches 0 (in process()).
        // A second toggle during the stop ramp re-arms the up-ramp from the
        // current gain — no snap.
        stopPending_ = !stopPending_;
    } else {
        recording_ = false;   // xfadeSamples_ == 0: legacy step behavior
        dispRecording_.store(false, std::memory_order_relaxed);
        dispLoopLen_.store(static_cast<std::uint32_t>(loopLen_), std::memory_order_relaxed);
    }
}
```

In `process()`, extend the Task-1 overdub write with the gain and ramp advance (write-then-advance):

```cpp
        } else {                             // overdub: mode-dependent write, wrap at loop length
            const float fb = writeFeedback();
            const float oldL = bufL_[writeIdx_], oldR = bufR_[writeIdx_];
            float fbL = oldL, fbR = oldR;
            if (writeMode_ == WriteMode::Decay) {
                decayLpL_ += decayLpA_ * (oldL - decayLpL_); fbL = decayLpL_;
                decayLpR_ += decayLpA_ * (oldR - decayLpR_); fbR = decayLpR_;
            }
            // odGain_ == 1 reduces to old + (fb·fbSrc − old) + in; Add (fb=1)
            // further reduces to the legacy old + in, bit-exact.
            bufL_[writeIdx_] = oldL + odGain_ * (fb * fbL - oldL) + odGain_ * inL;
            bufR_[writeIdx_] = oldR + odGain_ * (fb * fbR - oldR) + odGain_ * inR;
            writePeak(writeIdx_, bufL_[writeIdx_], bufR_[writeIdx_]);
            ++writeIdx_;
            if (writeIdx_ >= loopLen_) writeIdx_ = 0;
            if (stopPending_) {
                odGain_ -= odGainStep_;
                if (odGain_ <= 0.f) {
                    odGain_ = 0.f;
                    stopPending_ = false;
                    recording_ = false;
                    dispRecording_.store(false, std::memory_order_relaxed);
                    dispLoopLen_.store(static_cast<std::uint32_t>(loopLen_), std::memory_order_relaxed);
                }
            } else if (odGain_ < 1.f) {
                odGain_ += odGainStep_;
                if (odGain_ > 1.f) odGain_ = 1.f;
            }
        }
```

In `clear()` and `reset()`, add: `stopPending_ = false; odGain_ = 1.f; odGainStep_ = 0.f;` (clear/reset must kill a pending stop ramp along with `recording_`).

- [ ] **Step 4: Run tests.** All new + all existing (the 10 Hz overdub tests are unaffected: `xfadeSamples_ == 0` keeps the legacy instant toggle path).
- [ ] **Step 5: Full lanes.**
- [ ] **Step 6: Commit:** `feat: overdub start/stop write-gain ramps declick punch-in/out`

---

### Task 3: F1 wiring — write-mode param, menus, MetaModule alt-param

**Files:**
- Modify: `src/loooop/Loooop.cpp` (ParamId enum, constructor, `process`, `appendContextMenu`)
- Modify: `src/loooop/Lop.cpp` (same four places)
- Modify: `metamodule/loooop/QlpElements.hh` (new alt-param element type)
- Modify: `metamodule/loooop/Loooop_info.hh`, `metamodule/loooop/Lop_info.hh` (element + enum entries)
- Modify: `metamodule/loooop/LoooopCore.cc`, `metamodule/loooop/LopCore.cc` (read the state)

**Interfaces:**
- Consumes: `LoopEngine::WriteMode`, `setWriteMode` (Task 1).
- Produces: VCV `WRITE_MODE_PARAM` (switch 0–3, default 0 = Add, appended LAST in each ParamId enum, before `PARAMS_LEN`); MetaModule `WriteModeAlt` Elem. Params auto-persist in both hosts — no `dataToJson` needed (that's why this is a param, per spec).
- Menu/choice order everywhere is `{"Add", "Replace", "Layer", "Decay"}` — index 0 must be Add for MetaModule zero-init compatibility (same reasoning as the inverted `CROSSFADE_PARAM`, `Loooop.cpp:70-72`).

- [ ] **Step 1: VCV modules.** In `src/loooop/Loooop.cpp`:
  - ParamId: `... OVERDUB_PARAM, CROSSFADE_PARAM, WRITE_MODE_PARAM, PARAMS_LEN };`
  - Constructor, after the `CROSSFADE_PARAM` configSwitch:
    ```cpp
    configSwitch(WRITE_MODE_PARAM, 0.f, 3.f, 0.f, "Write mode",
        {"Add", "Replace", "Layer", "Decay"});
    ```
  - `process()`, next to `engine.setOverdub(...)`:
    ```cpp
    engine.setWriteMode(static_cast<LoopEngine::WriteMode>(
        (int)std::round(params[WRITE_MODE_PARAM].getValue())));
    ```
  - `appendContextMenu()`, after the "Crossfade loop seams" item:
    ```cpp
    static const std::vector<std::string> kWriteModes = {"Add", "Replace", "Layer", "Decay"};
    menu->addChild(createIndexSubmenuItem("Write mode", kWriteModes,
        [m] { return (int)std::round(m->params[Loooop::WRITE_MODE_PARAM].getValue()); },
        [m](int v) { m->paramQuantities[Loooop::WRITE_MODE_PARAM]->setValue((float)v); }));
    ```
  Repeat all four edits in `src/loooop/Lop.cpp` (`Lop::WRITE_MODE_PARAM` after its `CROSSFADE_PARAM`).

- [ ] **Step 2: MetaModule element type.** In `metamodule/loooop/QlpElements.hh` after `QlpCrossfadeAlt`:
    ```cpp
    // Choice order pins Add to index 0: the MM patch loader zero-inits unset
    // alt-params, so patches saved before this param must load in Add (the
    // legacy overdub-sum behavior).
    struct QlpWriteModeAlt : AltParamChoiceLabeled {
        constexpr QlpWriteModeAlt(BaseElement b)
            : AltParamChoiceLabeled{{{b}, 4, 0}, {"Add", "Replace", "Layer", "Decay"}} {}
    };
    ```

- [ ] **Step 3: Info files.** Both files keep Elements array and Elem enum in the SAME order as the VCV enums, so the new param goes right after the Crossfade element (last param, before the input jacks):
  - `metamodule/loooop/Loooop_info.hh`: bump `std::array<Element, 85>` → `86`; after the `QlpCrossfadeAlt{...}` line add
    `QlpWriteModeAlt{{0.f, 0.f, Center, "Write mode", ""}},`
    and in `enum class Elem` add `WriteModeAlt,` immediately after `CrossfadeSwitch,`.
  - `metamodule/loooop/Lop_info.hh`: bump `std::array<Element, 25>` → `26`; after its `QlpCrossfadeAlt{...}` line add
    `QlpWriteModeAlt{{0.f, 0.f, Center, "Write mode", "", 0.f, 0.f}},`
    (this file's BaseElement form carries the two trailing size floats) and add `WriteModeAlt,` after `CrossfadeSwitch,` in its Elem enum.
  - `bypass_routes` need no change (they index jacks per-type; a param insertion doesn't shift them).

- [ ] **Step 4: Cores.** In `metamodule/loooop/LoooopCore.cc` and `LopCore.cc`, next to the existing `engine_.setCrossfade(...)` call in `update()`:
    ```cpp
    engine_.setWriteMode(static_cast<LoopEngine::WriteMode>(
        (int)getState<WriteModeAlt>()));
    ```

- [ ] **Step 5: Full lanes.** `tests/run.sh` (includes the Python identity guards), `make -C vcv -j8`, `cmake --build metamodule/build -j8`. All green — this task is wiring; compile + guards are the verification.
- [ ] **Step 6: Commit:** `feat: write-mode menu param on Loooop and Lop, both hosts`

---

### Task 4: Q1 — Catmull-Rom playback interpolation

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (declare `tapWrapped`), `src/loooop/dsp/LoopEngine.cpp` (`readInterpolated`)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Consumes: existing `readRaw(double, buf)` (clamped fractional linear read — stays linear by design; it feeds the 5 ms seam crossfade where quality is irrelevant).
- Produces: `readInterpolated` becomes 4-tap Catmull-Rom. Private helper `float tapWrapped(double x, double winStart, double winLen, const std::vector<float>& buf) const`. No API change; always on (no menu option — YAGNI per spec).

**Tap rules (the whole design):** taps sit on the integer grid `floor(p) + k`, k = −1…+2. The base tap (k=0) reads clamped and never wraps (identical to the old `i0` read). Any tap outside `[winStart, winStart+winLen)` wraps by whole window lengths (`± winLen`, direction-preserving) and is then read via `readRaw` — a fractional `winStart` therefore gets a position-preserving interpolated read, keeping tap spacing uniform across the seam (uniform Catmull-Rom assumes equidistant taps). This is a deliberate refinement of the old wrap rule, which snapped the wrap target to exactly `winStart`; the two sanctioned expectation changes below follow from it.

- [ ] **Step 1: Update the three sanctioned expectations** in `tests/loooop/test_loop_engine.cpp`:
  - `test_subloop_window`: window [1.5, 3.5) over the ramp 1..8. At pos 1.5 (frac 0.5) the k=−1 tap at 0 wraps to 0+2=2 → buf[2]=3, so taps are (3, 2, 3, 4):
    `CR(0.5) = 0.5·(2·2 + (3−3)·0.5 + (6−10+12−4)·0.25 + (6−9+4−3)·0.125) = 2.375`.
    out[0] and out[2] (the wrapped repeat of pos 1.5) change 2.5 → 2.375; out[1] (pos 2.5, taps (4,3,4,3)) stays 3.5. Same window-seam phenomenon as the other two changes — a sub-window loops with period winLen, and its content isn't a ramp across that seam.
    ```cpp
    // Cubic: k=-1 tap at 0 wraps to 2 (window period 2), taps (3,2,3,4) -> 2.375
    check(near(e.process(0.f), 2.375f), "subloop: out[0]==2.375 (window start, cubic)");
    // advance by 1 -> pos 2.5 -> taps (4,3,4,3) -> 3.5 (unchanged from linear)
    check(near(e.process(0.f), 3.5f), "subloop: out[1]==3.5");
    // advance -> pos 3.5 >= winEnd 3.5 -> wraps to 1.5 -> 2.375 again
    check(near(e.process(0.f), 2.375f), "subloop: out[2]==2.375 (wrapped in window)");
    ```
  - `test_half_speed`: at pos 0.5 the k=−1 tap wraps to buf[3]=4 (a loop's sample "before" 0 is its last), so taps are (4,1,2,3):
    `CR(0.5) = 0.5·(2·1 + (2−4)·0.5 + (2·4−5·1+4·2−3)·0.25 + (3·1−3·2+3−4)·0.125) = 1.25`.
    Change the `out[1]` line to:
    ```cpp
    // Catmull-Rom: taps (buf[3],buf[0],buf[1],buf[2]) = (4,1,2,3), t=0.5 -> 1.25
    check(near(e.process(0.f), 1.25f), "half_speed: out[1]==1.25 (cubic, seam tap wraps)");
    ```
    (out[0], out[2] are integer positions — unchanged; out[3] at pos 1.5 has all-interior ramp taps → 2.5, unchanged.)
  - `test_fractional_winstart_wrap_target`: window [2.25, 7.75), last read at pos 7.25, frac 0.25. Taps: k=−1 → buf[6]=7; k=0 → buf[7]=8; k=+1 → 8 ≥ 7.75 wraps to 8−5.5=2.5 → readRaw(2.5)=3.5; k=+2 → 9−5.5=3.5 → readRaw(3.5)=4.5. `CR(0.25, [7, 8, 3.5, 4.5]) = 0.5·(16 − 0.875 − 1.03125 + 0.171875) = 7.1328125`. Update the final check and its comment:
    ```cpp
    // Cubic taps (7, 8, readRaw(2.5)=3.5, readRaw(3.5)=4.5), t=0.25 -> 7.1328125.
    // The wrapped taps read at winStart + (tap − winEnd) — position-preserving —
    // so a truncated (floor'd) winStart would give readRaw(2.0)=3.0 / readRaw(3.0)=4.0
    // and a different value; the fractional-winStart property this test guards survives.
    check(near(out, 7.1328125f), "fractional winStart: wrap taps interpolate at frac positions");
    ```

- [ ] **Step 2: Write the new failing tests** (append + register in `main()`):

```cpp
static void test_cubic_exact_on_interior_ramp() {
    // Catmull-Rom reproduces linear ramps exactly when all 4 taps are interior.
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 8; ++i) e.process(static_cast<float>(i + 1));  // 1..8
    e.toggleRecord();
    e.setCrossfade(false);
    e.setSpeed(0, 0.5f);
    e.jumpHead(0, 0.f);
    e.process(0.f);                       // pos 0 (seam-adjacent; skip)
    float a = e.process(0.f);             // pos 0.5 (k=-1 wraps; skip exactness)
    (void)a;
    check(near(e.process(0.f), 2.0f), "cubic ramp: pos 1.0 exact");
    check(near(e.process(0.f), 2.5f), "cubic ramp: pos 1.5 exact (interior taps)");
    check(near(e.process(0.f), 3.0f), "cubic ramp: pos 2.0 exact");
    check(near(e.process(0.f), 3.5f), "cubic ramp: pos 2.5 exact (interior taps)");
}

static void test_cubic_beats_linear_on_sine() {
    // At speed 0.5 a sampled sine reconstructed with Catmull-Rom must be
    // closer to the analytic sine than the linear baseline (computed here
    // from the same buffer).
    const int N = 512;                    // 8 whole cycles -> continuous seam
    const double w = 2.0 * M_PI * 8.0 / N;
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    static float buf[N];
    for (int i = 0; i < N; ++i) {
        buf[i] = (float)std::sin(w * i);
        e.process(buf[i]);
    }
    e.toggleRecord();
    e.setSpeed(0, 0.5f);
    e.jumpHead(0, 0.f);
    double errCubic = 0.0, errLinear = 0.0;
    for (int k = 0; k < 2 * N; ++k) {
        const double pos = 0.5 * k - std::floor(0.5 * k / N) * N;   // pos of THIS read
        float out = e.process(0.f);
        double ideal = std::sin(w * pos);
        errCubic += (out - ideal) * (out - ideal);
        int i0 = (int)pos; int i1 = (i0 + 1) % N; double fr = pos - i0;
        double lin = (1.0 - fr) * buf[i0] + fr * buf[i1];
        errLinear += (lin - ideal) * (lin - ideal);
    }
    char msg[96];
    std::snprintf(msg, sizeof(msg),
        "cubic sine: rmsErr %.3g < 0.5 * linear %.3g", std::sqrt(errCubic), std::sqrt(errLinear));
    check(errCubic < 0.25 * errLinear, msg);   // RMS at least 2x better
}

static void test_cubic_tiny_loop_taps_bounded() {
    // 1- and 2-sample loops: the k=-1/+2 taps must wrap/clamp inside the
    // window, never index outside [0, loopLen). Output stays within the
    // recorded sample range.
    for (int n : {1, 2}) {
        LoopEngine e(1); e.reset(10.f, 100.f); e.setCrossfade(false);
        e.toggleRecord();
        for (int i = 0; i < n; ++i) e.process(i ? -1.f : 1.f);
        e.toggleRecord();
        e.setSpeed(0, 0.7f);              // fractional positions
        bool ok = true;
        for (int i = 0; i < 64; ++i) {
            float v = e.process(0.f);
            if (!std::isfinite(v) || v < -2.5f || v > 2.5f) ok = false;
        }
        check(ok, n == 1 ? "cubic tiny loop: 1-sample loop bounded"
                         : "cubic tiny loop: 2-sample loop bounded");
    }
}

static void test_cubic_reverse_matches_forward_at_position() {
    // Tap selection depends only on position, not direction: reading the same
    // fractional position forward and reverse gives the same value.
    LoopEngine f(1), r(1);
    for (LoopEngine* e : {&f, &r}) {
        e->reset(10.f, 100.f);
        e->setCrossfade(false);
        e->toggleRecord();
        for (int i = 0; i < 8; ++i) e->process((float)((i * 37) % 11));  // non-ramp content
        e->toggleRecord();
    }
    f.setSpeed(0, 0.5f);  f.jumpHead(0, 2.f / 7.f);    // pos 2.0, then 2.5, 3.0...
    r.setSpeed(0, -0.5f); r.jumpHead(0, 3.f / 7.f);    // pos 3.0, then 2.5, 2.0...
    f.process(0.f); f.process(0.f);                    // consume pos 2.0 -> next read 2.5... 
    float fwd = f.process(0.f);                        // reads pos 3.0
    r.process(0.f);                                    // reads pos 3.0
    float rev  = r.process(0.f);                       // reads pos 2.5
    float fwd25 = 0.f;
    { LoopEngine g(1); g.reset(10.f, 100.f); g.setCrossfade(false);
      g.toggleRecord();
      for (int i = 0; i < 8; ++i) g.process((float)((i * 37) % 11));
      g.toggleRecord();
      g.setSpeed(0, 0.5f); g.jumpHead(0, 2.f / 7.f);
      g.process(0.f);
      fwd25 = g.process(0.f); }                        // reads pos 2.5
    check(near(rev, fwd25), "cubic reverse: same position -> same value as forward");
    (void)fwd;
}
```

- [ ] **Step 3: Run to verify failure.** The two updated expectations from Step 1 FAIL against the linear implementation (1.5 ≠ 1.25, 6.8125 ≠ 7.1328125), as do `test_cubic_beats_linear_on_sine`. Expected.

- [ ] **Step 4: Implement.** `LoopEngine.hpp`, next to the `readRaw` declaration:

```cpp
    float tapWrapped(double x, double winStart, double winLen,
                     const std::vector<float>& buf) const;
```

`LoopEngine.cpp` — replace the body of `readInterpolated` (`:184-208`) with:

```cpp
// Wrap a tap position into [winStart, winStart + winLen) by whole window
// lengths (taps sit at most 2 samples outside; bounded iterations since
// winLen >= 1), then read it. A fractional winStart lands the wrapped tap on
// a fractional position — readRaw's lerp handles it, keeping tap spacing
// uniform across the seam.
float LoopEngine::tapWrapped(double x, double winStart, double winLen,
                             const std::vector<float>& buf) const {
    while (x >= winStart + winLen) x -= winLen;
    while (x < winStart) x += winLen;
    return readRaw(x, buf);
}

// 4-point Catmull-Rom read for one head, window-aware (Q1). The base tap
// keeps the old linear reader's clamped semantics; neighbors wrap via
// tapWrapped. Reproduces linear ramps exactly when all taps are interior,
// so exact-value ramp tests hold away from seams. readRaw (the seam-
// crossfade feeder) deliberately stays linear.
float LoopEngine::readInterpolated(const PlayHead& h, const std::vector<float>& buf,
                                   double winStart, double winLen) const {
    double p = h.pos;
    if (p < winStart || p >= winStart + winLen) p = winStart;   // honor a just-moved window on this read (advanceHead snaps h.pos after)
    const double ip = std::floor(p);
    const float frac = static_cast<float>(p - ip);
    const float t0 = tapWrapped(ip - 1.0, winStart, winLen, buf);
    const float t1 = readRaw(ip, buf);                    // base tap: in-window by construction
    const float t2 = tapWrapped(ip + 1.0, winStart, winLen, buf);
    const float t3 = tapWrapped(ip + 2.0, winStart, winLen, buf);
    const float c1 = 0.5f * (t2 - t0);
    const float c2 = t0 - 2.5f * t1 + 2.f * t2 - 0.5f * t3;
    const float c3 = 1.5f * (t1 - t2) + 0.5f * (t3 - t0);
    return ((c3 * frac + c2) * frac + c1) * frac + t1;
}
```

- [ ] **Step 5: Run the full loooop test binary.** Everything passes, including the untouched seam/crossfade suite (`test_crossfade_declicks_seam`, `test_jitter_crossfade_continuity`). Any OTHER existing test failing means the tap rules were implemented differently than specified — fix the code, not the test.
- [ ] **Step 6: Full lanes.**
- [ ] **Step 7: Commit:** `feat: Catmull-Rom playback interpolation in LoopEngine`

---

### Task 5: Q2 — one-shot fade-out

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (PlayHead member, helper decl, `osRampStep_`), `src/loooop/dsp/LoopEngine.cpp` (`readHead`, `advanceHead`, `triggerOneShot`, `reset`, `setSampleRate`)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Consumes: existing smoothstep curve shape (`t·t·(3−2t)`), `xfadeSamples_`.
- Produces: one-shot passes end in a smoothstep fade-to-zero over `min(xfadeSamples_, half-pass)` output samples; retrigger during the fade ramps back to unity over \~1 ms instead of snapping. Gated on `crossfade_` (the existing "Crossfade" declick option) and degrades to today's hard stop when `xfadeSamples_ == 0`, keeping every 10 Hz one-shot test exact.

- [ ] **Step 1: Write the failing tests** (append + register in `main()`):

```cpp
static void test_one_shot_fade_out() {
    LoopEngine e(1); e.reset(48000.f, 1.f);            // crossfade on (default)
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);      // constant loop
    e.toggleRecord();
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    static float out[2000];
    for (int i = 0; i < 2000; ++i) out[i] = e.process(0.f);
    check(near(out[1000], 1.f, 0.01f), "osfade: full level mid-pass");
    check(std::fabs(out[1999]) < 0.05f, "osfade: last sample faded to ~0");
    bool mono = true, smooth = true;
    for (int i = 1761; i < 2000; ++i) {
        if (out[i] > out[i-1] + 1e-3f) mono = false;
        if (std::fabs(out[i] - out[i-1]) > 0.05f) smooth = false;
    }
    check(mono,   "osfade: fade is monotonic");
    check(smooth, "osfade: no step at the end");
    check(near(e.process(0.f), 0.f), "osfade: silent after the pass");
}

static void test_one_shot_fade_out_reverse() {
    LoopEngine e(1); e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);
    e.toggleRecord();
    e.setSpeed(0, -1.f);
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    static float out[2000];
    for (int i = 0; i < 2000; ++i) out[i] = e.process(0.f);
    check(near(out[1000], 1.f, 0.01f),  "osfade_rev: full level mid-pass");
    check(std::fabs(out[1999]) < 0.05f, "osfade_rev: fades to ~0 at window start");
    bool smooth = true;
    for (int i = 1761; i < 2000; ++i)
        if (std::fabs(out[i] - out[i-1]) > 0.05f) smooth = false;
    check(smooth, "osfade_rev: no step at the end");
}

static void test_one_shot_retrigger_mid_fade() {
    LoopEngine e(1); e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);
    e.toggleRecord();
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    for (int i = 0; i < 1880; ++i) e.process(0.f);      // ~120 samples into the fade
    float before = e.process(0.f);
    e.triggerOneShot(0);                                 // retrigger during the fade
    float maxDelta = 0.f, prev = before;
    for (int i = 0; i < 100; ++i) {
        float v = e.process(0.f);
        maxDelta = std::max(maxDelta, std::fabs(v - prev));
        prev = v;
    }
    check(before < 0.7f,             "osretrig: was mid-fade before retrigger");
    check(maxDelta < 0.1f,           "osretrig: no gain snap (ramps back in)");
    check(near(prev, 1.f, 0.05f),    "osretrig: back to full level after ~1 ms ramp");
}
```

- [ ] **Step 2: Run to verify failure.** `osfade: last sample faded to ~0` FAILS (currently a hard stop at full level: out[1999] == 1).

- [ ] **Step 3: Implement.** `LoopEngine.hpp`: in `struct PlayHead` add `float osRamp = 1.f;` (retrigger ramp-in gain). Private members/decl:

```cpp
    float osRampStep_ = 1.f;   // ~1 ms retrigger ramp-in; set with xfadeSamples_
    int oneShotFadeLen(const PlayHead& h, double winLen) const;
    float oneShotFadeGain(const PlayHead& h, double winStart, double winLen, int F) const;
```

`LoopEngine.cpp`: wherever `xfadeSamples_` is computed (`reset()` and `setSampleRate()`):

```cpp
    osRampStep_ = 1.f / std::max(1.f, 0.001f * sampleRate);
```

Helpers (near `fadeLen`):

```cpp
// One-shot end fade length: same sizing as the seam crossfade but WITHOUT
// the oneShot exclusion — one-shots skip the seam fade and instead fade to
// zero at the end of the pass (Q2). Gated on the same Crossfade option.
int LoopEngine::oneShotFadeLen(const PlayHead& h, double winLen) const {
    if (!crossfade_ || xfadeSamples_ == 0) return 0;
    const double sp = std::fabs(static_cast<double>(h.speed));
    if (sp < 1e-9) return 0;
    const int cap = static_cast<int>((winLen / sp) * 0.5);
    int F = static_cast<int>(xfadeSamples_);
    if (F > cap) F = cap;
    return F < 1 ? 0 : F;
}

// Positional fade gain: 1 outside the final F output-samples of the pass,
// smoothstep down to 0 at the endpoint (winEnd forward / winStart reverse).
float LoopEngine::oneShotFadeGain(const PlayHead& h, double winStart,
                                  double winLen, int F) const {
    const double sp = std::fabs(static_cast<double>(h.speed));
    const double outToEnd = (h.speed >= 0.f)
        ? (winStart + winLen - h.pos) / sp
        : (h.pos - winStart) / sp;
    if (outToEnd < 0.0) return 0.f;
    if (outToEnd >= static_cast<double>(F)) return 1.f;
    const float t = static_cast<float>(outToEnd / F);   // 0 at end -> 1 at fade start
    return t * t * (3.f - 2.f * t);
}
```

In `readHead`, after the existing seam-crossfade block (one-shots never enter it — `fadeLen` returns 0 for them):

```cpp
    if (h.oneShot) {
        const int Fo = oneShotFadeLen(h, winLen);
        float g = (Fo >= 1) ? oneShotFadeGain(h, winStart, winLen, Fo) : 1.f;
        if (h.osRamp < 1.f) g *= h.osRamp;   // retrigger ramp-in (~1 ms)
        if (g < 1.f) { outL *= g; outR *= g; }
    }
```

In `advanceHead`, at the top (it runs right after `readHead` each sample):

```cpp
    if (h.osRamp < 1.f) {
        h.osRamp += osRampStep_;
        if (h.osRamp > 1.f) h.osRamp = 1.f;
    }
```

In `triggerOneShot`, before `restartHead(head);`:

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
    }
```

- [ ] **Step 4: Run tests.** New tests pass; `test_one_shot`, `test_one_shot_reverse`, `test_one_shot_survives_clear`, `test_triggers_no_loop` unchanged (10 Hz → `xfadeSamples_ == 0` → no fade, `osRampStep_ = 1` → instant).
- [ ] **Step 5: Full lanes.**
- [ ] **Step 6: Commit:** `feat: one-shot passes fade to zero instead of hard-stopping`

---

### Task 6: Q3 — smoothed head level (engine) and pan / dry-wet (hosts)

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp`, `src/loooop/dsp/LoopEngine.cpp` (level smoothing)
- Modify: `src/loooop/LooperModuleDSP.hpp` (smoother utility)
- Modify: `src/loooop/Loooop.cpp` (pan ×4 + mix), `src/loooop/Lop.cpp` (mix)
- Modify: `metamodule/loooop/LoooopCore.cc` (pan ×4 + mix), `metamodule/loooop/LopCore.cc` (mix)
- Test: `tests/loooop/test_loop_engine.cpp`, `tests/loooop/test_module_dsp.cpp`

**Interfaces:**
- Produces: `loooop::OnePoleSmoother { float value, alpha; float process(float target); void reset(float v); }` and `loooop::smootherAlpha(float sampleRate, float tauSec)` in `LooperModuleDSP.hpp` (mirrors `src/mf20/dsp_utils.hpp` so the loooop lane stays self-contained). Engine smooths each head's level internally (\~2 ms); hosts smooth pan and dry-wet with the same tau.
- The pan LAW is untouched: center-unity linear balance is load-bearing for `test_four_heads_mix` (`Loooop.cpp:47-48`). Only the pan VALUE is smoothed before the existing `panLeftGain`/`panRightGain`.
- At the 10 Hz test rate `smootherAlpha` saturates to exactly 1.0f (passthrough), which is why every exact-value engine test survives.

- [ ] **Step 1: Write the failing tests.** Engine test (append + register in `test_loop_engine.cpp`):

```cpp
static void test_level_smoothing() {
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);   // constant loop
    e.toggleRecord();
    e.setLevel(0, 0.f);
    for (int i = 0; i < 3000; ++i) e.process(0.f);   // settle at 0
    e.setLevel(0, 1.f);                               // step 0 -> 1
    float first = e.process(0.f);
    check(first > 0.f && first < 0.05f, "smooth: no full level step in one sample");
    float last = 0.f;
    for (int i = 0; i < 3000; ++i) last = e.process(0.f);
    check(near(last, 1.f, 0.01f), "smooth: settles at target");
}
```

Helper tests (append inside `main()` of `tests/loooop/test_module_dsp.cpp`):

```cpp
    loooop::OnePoleSmoother s;
    s.reset(0.f);
    s.alpha = loooop::smootherAlpha(48000.f, 0.002f);
    float v1 = s.process(1.f);
    check(v1 > 0.f && v1 < 0.02f, "smoother: first step bounded by alpha");
    float v = 0.f;
    for (int i = 0; i < 48000; ++i) v = s.process(1.f);
    check(std::fabs(v - 1.f) < 1e-3f, "smoother: settles at target");
    check(std::fabs(loooop::smootherAlpha(10.f, 0.002f) - 1.f) < 1e-6f,
          "smoother: alpha saturates to 1 at low sample rates");
```

- [ ] **Step 2: Run both test binaries to verify failure** (compile failure on `OnePoleSmoother`; `smooth: no full level step` fails with a full step).

- [ ] **Step 3: Implement the helper.** `src/loooop/LooperModuleDSP.hpp`, inside `namespace loooop`:

```cpp
// One-pole smoother for zipper-noise suppression (mirrors mf20/dsp_utils.hpp
// so the loooop headless lane stays self-contained). alpha 1 = passthrough.
struct OnePoleSmoother {
    float value = 0.f;
    float alpha = 1.f;
    float process(float target) { value += alpha * (target - value); return value; }
    void reset(float v) { value = v; }
};

// One-pole alpha for a time constant (seconds); saturates to 1 at low rates.
inline float smootherAlpha(float sampleRate, float tauSec) {
    return 1.f - std::exp(-1.f / (tauSec * sampleRate));
}
```

- [ ] **Step 4: Implement engine level smoothing.** `LoopEngine.hpp`: in `struct PlayHead` add `float levelSm = 1.f;` (matches the 1.0 level default). Private member `float levelAlpha_ = 1.f;`. In `reset()` and `setSampleRate()`:

```cpp
    levelAlpha_ = 1.f - std::exp(-1.f / (0.002f * sampleRate));   // ~2 ms; ==1 at test rates
```

In `process()`, advance every head's smoother each sample regardless of loop/playing state (so levels are already settled when playback starts — no post-record bleed), then use the smoothed value:

```cpp
    for (auto& o : heads) o = HeadOut{};
    for (int i = 0; i < numHeads_; ++i) {
        PlayHead& h = heads_[i];
        h.levelSm += levelAlpha_ * (h.level - h.levelSm);
    }
    if (loopLen_ > 0) {
        for (int i = 0; i < numHeads_; ++i) {
            PlayHead& h = heads_[i];
            if (!h.playing) continue;
            double winStart, winLen;
            windowBounds(h, winStart, winLen);
            float l, r; readHead(h, winStart, winLen, l, r);
            heads[i].l = l * h.levelSm;
            heads[i].r = r * h.levelSm;
            advanceHead(h, i, winStart, winLen);
        }
    }
```

- [ ] **Step 5: Wire the hosts.** `src/loooop/Loooop.cpp` — members next to `lastJumpV`:

```cpp
    loooop::OnePoleSmoother panSm[LoopEngine::NUM_HEADS];
    loooop::OnePoleSmoother mixSm{1.f, 1.f};   // value matches DRYWET default
    float smootherRate = 0.f;
```

Top of `process()`:

```cpp
        if (args.sampleRate != smootherRate) {
            smootherRate = args.sampleRate;
            const float a = loooop::smootherAlpha(smootherRate, 0.002f);
            for (auto& s : panSm) s.alpha = a;
            mixSm.alpha = a;
        }
```

In the mix loop, smooth the pan value (law untouched):

```cpp
            const float pan = panSm[h].process(loooop::panControl(
                params[PAN1_PARAM + HEAD_PARAMS * h].getValue(),
                inputs[PAN1_CV_INPUT + HEAD_INPUTS * h].getVoltage()));
```

And the mix: `const float w = mixSm.process(loooop::normalizedControl(...));`

`src/loooop/Lop.cpp`: same `mixSm`/`smootherRate` members and alpha block (no pans); wrap its `w` the same way.

`metamodule/loooop/LoooopCore.cc` / `LopCore.cc`: same smoothing on their pan (Loooop only) and dry-wet values in `update()` — members initialized for 48 kHz, alpha recomputed in `set_samplerate`:

```cpp
    loooop::OnePoleSmoother mixSm_{1.f, loooop::smootherAlpha(48000.f, 0.002f)};
    // LoooopCore additionally: loooop::OnePoleSmoother panSm_[LoopEngine::NUM_HEADS]; (alpha set alongside)
```

```cpp
    void set_samplerate(float sr) override {
        engine_.reset(sr);
        mixSm_.alpha = loooop::smootherAlpha(sr, 0.002f);
        // LoooopCore: also update each panSm_[h].alpha
    }
```

- [ ] **Step 6: Run both test binaries, then full lanes.** Also confirm `test_four_heads_mix` and `test_crossfade_declicks_seam` still pass (steady-state unity and settled-level guarantees).
- [ ] **Step 7: Commit:** `feat: smooth head level, pan, and dry-wet over ~2 ms`

---

### Task 7: Final verification and report

- [ ] All lanes from a clean state: `tests/run.sh` && `make -C vcv -j8` && `cmake --build metamodule/build -j8`.
- [ ] Confirm `git log` shows one commit per task, messages ≤15 words, no attribution lines.
- [ ] If the RobotBoy plugin is tracked in yml-to-vcv's `param_ranges.json`, refresh it for the new `WRITE_MODE_PARAM` via the `check-metamodule-coverage` / `update-all-metamodule-params` skills (main session, not a subagent).
- [ ] Produce a report listing what landed per task and reproduce the USER CHECK section below verbatim at the end.

## USER CHECK (user-run, eyes/ears — not agent tasks)

1. **Write modes (VCV + simulator):** record a loop, overdub in each mode. Replace punches new audio over old; Add sums (legacy); Layer makes earlier layers sink \~1 dB/pass; Decay does the same plus audible dulling of highs per pass. Idle loops never fade.
2. **Tune the constants by ear** (simulator): `LoopEngine::LAYER_FEEDBACK` (0.9) and `DECAY_LP_HZ` (6000). Both are single constants in `LoopEngine.hpp`.
3. **Punch declick:** overdub with a hot input; start/stop no longer click on subsequent passes (\~5 ms soft edges).
4. **Cubic interpolation:** melodic V/oct material at fractional speeds sounds noticeably less dull/gritty than the previous build.
5. **One-shot ends:** the re-slicer patch no longer clicks at one-shot ends; retriggering during the tail doesn't pop.
6. **Smoothing:** square LFO into Level/Pan/Mix CV — steps become \~2 ms glides, no zipper.
7. **MetaModule menu:** Write mode appears with four options on both Loooop and Lop; old patches load in Add.
