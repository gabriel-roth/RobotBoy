# Minimum Audible Loop Size Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Size 0 produce the shortest potentially audible moving loop instead of a stationary one-sample window.

**Architecture:** Keep the public 0–1 Size mapping unchanged and enforce a sample-rate-derived lower bound inside the shared `LoopEngine::windowBounds()` method. Use a 20 kHz maximum repeat frequency at 1× playback, calculated as `ceil(sampleRate / 20000)`, capped by the recorded loop length.

**Tech Stack:** C++20, repository-native test harness, VCV Rack SDK, 4ms MetaModule SDK, GNU Make, CMake.

---

### Task 1: Add the failing minimum-window regression

**Files:**
- Modify: `tests/loooop/test_loop_engine.cpp`
- Test: `tests/loooop/test_loop_engine.cpp`

- [ ] **Step 1: Add a helper and regression test**

Add a helper that records a ramp at a requested sample rate, disables seam crossfading, sets Size to 0, processes samples, and measures the displayed window length in samples. Test:

```cpp
static void test_minimum_audible_window() {
    LoopEngine at48k(1);
    at48k.reset(48000.f, 1.f);
    at48k.setCrossfade(false);
    at48k.toggleRecord();
    for (int i = 0; i < 16; ++i) at48k.process(static_cast<float>(i + 1));
    at48k.toggleRecord();
    at48k.setSize(0, 0.f);
    float first = at48k.process(0.f);
    auto snap48 = at48k.displaySnapshot();
    float second = at48k.process(0.f);
    float win48 = (snap48.winEnd01[0] - snap48.winStart01[0]) * 16.f;
    check(near(win48, 3.f), "min_size: 48k window is 3 samples");
    check(!near(first, second), "min_size: playhead advances and output changes");

    LoopEngine at96k(1);
    at96k.reset(96000.f, 1.f);
    at96k.setCrossfade(false);
    at96k.toggleRecord();
    for (int i = 0; i < 16; ++i) at96k.process(static_cast<float>(i + 1));
    at96k.toggleRecord();
    at96k.setSize(0, 0.f);
    at96k.process(0.f);
    auto snap96 = at96k.displaySnapshot();
    float win96 = (snap96.winEnd01[0] - snap96.winStart01[0]) * 16.f;
    check(near(win96, 5.f), "min_size: 96k window is 5 samples");

    LoopEngine shortLoop(1);
    shortLoop.reset(96000.f, 1.f);
    shortLoop.setCrossfade(false);
    shortLoop.toggleRecord();
    shortLoop.process(1.f);
    shortLoop.process(2.f);
    shortLoop.toggleRecord();
    shortLoop.setSize(0, 0.f);
    shortLoop.process(0.f);
    auto shortSnap = shortLoop.displaySnapshot();
    float shortWin = shortSnap.winEnd01[0] - shortSnap.winStart01[0];
    check(near(shortWin, 1.f), "min_size: short loop uses full available length");
}
```

Call it from `main()`.

- [ ] **Step 2: Run the looper test and verify RED**

Run: `./tests/run.sh`

Expected: the new 48 kHz and 96 kHz window-size assertions fail because the current minimum is one sample.

### Task 2: Implement the sample-rate-derived minimum

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp`
- Modify: `src/loooop/dsp/LoopEngine.hpp`
- Test: `tests/loooop/test_loop_engine.cpp`

- [ ] **Step 1: Define the maximum minimum-window repeat frequency**

Add a private engine constant:

```cpp
static constexpr float MAX_MINIMUM_LOOP_HZ = 20000.f;
```

- [ ] **Step 2: Replace the one-sample floor**

In `windowBounds()`, calculate:

```cpp
const double minWinLen = std::ceil(
    static_cast<double>(sampleRate_) / static_cast<double>(MAX_MINIMUM_LOOP_HZ));
winLen = static_cast<double>(h.size) * L;
if (winLen < minWinLen) winLen = minWinLen;
if (winLen > L) winLen = L;
```

- [ ] **Step 3: Run the looper test and verify GREEN**

Run: `./tests/run.sh`

Expected: all repository-native tests pass, including 3 samples at 48 kHz, 5 at 96 kHz, advancing output, and the short-loop cap.

- [ ] **Step 4: Commit implementation and test**

```bash
git add src/loooop/dsp/LoopEngine.cpp src/loooop/dsp/LoopEngine.hpp tests/loooop/test_loop_engine.cpp
git commit -m "fix: keep minimum loop size audible"
```

### Task 3: Verify both hosts and install VCV Rack plugin

**Files:**
- Verify: `tests/**`
- Verify: `vcv/**`
- Verify: `metamodule/**`
- Install: VCV Rack user plugin directory

- [ ] **Step 1: Run complete regression suites**

Run:

```bash
./tests/run.sh
./tests/beads/run.sh
python3 -m unittest tests/test_robotboy_identity.py tests/test_no_delay_mode.py -v
```

Expected: all tests pass.

- [ ] **Step 2: Build VCV Rack plugin**

Run: `make -C vcv clean && make -C vcv -j`

Expected: `vcv/plugin.dylib` exists.

- [ ] **Step 3: Build MetaModule package**

Run:

```bash
cmake -S metamodule -B metamodule/build
cmake --build metamodule/build -j
```

Expected: `metamodule/metamodule-plugins/RobotBoy.mmplugin` exists.

- [ ] **Step 4: Install VCV Rack plugin**

Run: `make -C vcv install`

Expected: Rack installs/unpacks `RobotBoy` in its user plugins directory.

- [ ] **Step 5: Verify installed binary and branch scope**

Compare SHA-256 of `vcv/dist/RobotBoy/plugin.dylib` and the installed `RobotBoy/plugin.dylib`. Run `git diff --check` and `git status --short`.

Expected: binary hashes match, no whitespace errors, and the feature worktree is clean.
