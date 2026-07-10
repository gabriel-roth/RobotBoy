# Loooop Grid Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a global Grid setting (Off/4/8/16, default Off) to Loooop that snaps every playhead's window position and size to equal loop segments, with grid lines drawn on the display.

**Architecture:** All quantization happens in `LoopEngine::windowBounds()`, the single choke point for window geometry, so knobs, CV, jitter, trigger restarts, and the crossfade's next-window preview all obey the grid. Both hosts (VCV `Loooop.cpp`, MetaModule `LoooopCore.cc`) push the setting per-sample via a new `engine.setGrid(segments)`, mirroring the existing `setOverdub`/`setCrossfade` pattern. The shared `LoopWaveformRenderer` draws the grid bars on both platforms.

**Tech Stack:** C++20, VCV Rack SDK, MetaModule SDK, hand-rolled `check()` test binaries built by `tests/run.sh`.

**Spec:** `docs/superpowers/specs/2026-07-10-grid-mode-design.md`

## Global Constraints

- **ALL work happens in the `loooop-track` worktree:**
  `/Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track` (branch `loooop-track`).
  Every path below is relative to that worktree root; never touch the primary
  `main` checkout. The branch already carries unmerged Loooop work (write
  modes, one-shot fades, smoothing) that these same files depend on.
- Grid choices are exactly `{"Off", "4", "8", "16"}`, values/indices 0–3, default 0 (= Off). MetaModule's patch loader zero-inits unset alt-params, so index 0 MUST be Off.
- Param/element ids: VCV `GRID_PARAM` is appended immediately before `PARAMS_LEN` (after the branch's `WRITE_MODE_PARAM`); the MetaModule element goes at the end of the global-params block (right after `QlpWriteModeAlt`), so no existing param/jack/display index shifts.
- Grid quantization must leave grid-off behavior byte-for-byte unchanged.
- No AI attribution in commit messages; commit messages one short sentence.
- Tests run with `cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh` (test binaries build into the worktree's own `build/tests`), or compile the single test binary as shown per task.
- GUI/simulator checks are NOT automated — they go on the user-run checklist in Task 5.

---

### Task 1: Engine grid quantization

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (public API near `setCrossfade`, `DisplaySnapshot`, private state)
- Modify: `src/loooop/dsp/LoopEngine.cpp` (two-argument `windowBounds` at ~line 219, `displaySnapshot` at end of file)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Produces: `void LoopEngine::setGrid(int segments)` — 0 or <2 disables; `DisplaySnapshot::grid` (`std::uint32_t`, 0 = off). Tasks 2–4 rely on both.

- [x] **Step 1: Write the failing tests**

Add to `tests/loooop/test_loop_engine.cpp`, before `int main()` (the file already has `check`, `near`, `soloHead0`, and `record_ramp(e, n)` — reuse them). All tests run at the 10 Hz test rate where the 1 ms minimum window is 1 sample.

```cpp
static void test_grid_size_snaps_to_segments() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);                    // seg = 4 samples
    e.setSize(0, 0.3f);              // 4.8 samples -> rounds to 1 segment (4)
    // centre 0.5 -> continuous start 8-2=6 -> k = lround(6/4) = 2 -> window [8,12)
    check(near(e.process(0.f), 9.f),  "grid_size: out[0]==9");
    check(near(e.process(0.f), 10.f), "grid_size: out[1]==10");
    check(near(e.process(0.f), 11.f), "grid_size: out[2]==11");
    check(near(e.process(0.f), 12.f), "grid_size: out[3]==12");
    check(near(e.process(0.f), 9.f),  "grid_size: out[4]==9 (wrapped)");
    const auto s = e.displaySnapshot();
    check(s.grid == 4, "grid_size: snapshot reports grid");
    check(near(s.winStart01[0], 0.5f) && near(s.winEnd01[0], 0.75f),
          "grid_size: snapshot window on segment bounds");
}

static void test_grid_position_snaps_to_boundaries() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.25f);             // exactly 1 segment
    e.setPosition(0, 0.1f);          // continuous start -0.4 -> k=0 -> [0,4)
    check(near(e.process(0.f), 1.f), "grid_pos: low position snaps to segment 0");
    e.setPosition(0, 0.4f);          // continuous start 4.4 -> k=1 -> [4,8)
    check(near(e.process(0.f), 5.f), "grid_pos: position snaps to segment 1");
}

static void test_grid_window_clamped_inside_loop() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.6f);              // 9.6 -> 2 segments (8 samples)
    e.setPosition(0, 1.f);           // start 12 -> k clamped to 2 -> [8,16)
    check(near(e.process(0.f), 9.f), "grid_clamp: window pinned inside loop");
    const auto s = e.displaySnapshot();
    check(near(s.winStart01[0], 0.5f) && near(s.winEnd01[0], 1.f),
          "grid_clamp: snapshot [0.5,1.0]");
}

static void test_grid_min_one_segment() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.01f);             // under one segment -> grows to 1 segment
    e.process(0.f);
    const auto s = e.displaySnapshot();
    check(near(s.winEnd01[0] - s.winStart01[0], 0.25f),
          "grid_min: window grows to one segment");
}

static void test_grid_full_size_plays_whole_loop() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(8);
    e.setPosition(0, 0.9f);          // size 1 -> all 8 segments, position moot
    check(near(e.process(0.f), 1.f), "grid_full: out[0]==1");
    for (int i = 1; i < 16; ++i) e.process(0.f);
    check(near(e.process(0.f), 1.f), "grid_full: wraps at 16");
}

static void test_grid_off_matches_ungridded() {
    LoopEngine a; record_ramp(a, 16);
    LoopEngine b; record_ramp(b, 16);
    b.setGrid(4); b.setGrid(0);      // enable then disable
    a.setSize(0, 0.3f); a.setPosition(0, 0.37f);
    b.setSize(0, 0.3f); b.setPosition(0, 0.37f);
    bool same = true;
    for (int i = 0; i < 40; ++i) same = same && near(a.process(0.f), b.process(0.f));
    check(same, "grid_off: disabled grid matches ungridded engine");
    check(a.displaySnapshot().grid == 0 && b.displaySnapshot().grid == 0,
          "grid_off: snapshot reports off");
}

static void test_grid_invalid_values_mean_off() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(1);                    // <2 segments is meaningless -> off
    e.setSize(0, 0.3f); e.setPosition(0, 0.37f);
    e.process(0.f);
    check(e.displaySnapshot().grid == 0, "grid_invalid: 1 segment reads as off");
}

static void test_grid_jitter_lands_on_boundaries() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.25f);
    e.setJitter(0, 1.f);
    bool onGrid = true;
    for (int i = 0; i < 200; ++i) {
        e.process(0.f);
        const auto s = e.displaySnapshot();
        const float k = s.winStart01[0] * 4.f;
        onGrid = onGrid && near(k, std::round(k), 1e-3f);
    }
    check(onGrid, "grid_jitter: jittered windows stay on segment boundaries");
}

static void test_grid_respects_min_window() {
    LoopEngine e; record_ramp(e, 3);     // seg = 0.75 < the 1-sample minimum
    e.setGrid(4);
    e.setSize(0, 0.01f);
    e.process(0.f);
    const auto s = e.displaySnapshot();
    check(s.winEnd01[0] - s.winStart01[0] >= 1.f / 3.f - 1e-4f,
          "grid_minwin: window grew to cover the minimum window");
}
```

Add the calls at the end of `main()`'s test list (before the failure summary):

```cpp
    test_grid_size_snaps_to_segments();
    test_grid_position_snaps_to_boundaries();
    test_grid_window_clamped_inside_loop();
    test_grid_min_one_segment();
    test_grid_full_size_plays_whole_loop();
    test_grid_off_matches_ungridded();
    test_grid_invalid_values_mean_off();
    test_grid_jitter_lands_on_boundaries();
    test_grid_respects_min_window();
```

- [x] **Step 2: Run the test to verify it fails**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests
mkdir -p ../build/tests
g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_loop_engine \
    loooop/test_loop_engine.cpp ../src/loooop/dsp/LoopEngine.cpp && ../build/tests/test_loop_engine
```

Expected: compile FAILS — `setGrid` is not a member of `LoopEngine`.

- [x] **Step 3: Implement**

`src/loooop/dsp/LoopEngine.hpp` — after `void setCrossfade(bool on) ...`:

```cpp
    void setGrid(int segments);   // snap windows to N equal loop segments; <2 = off
```

In `DisplaySnapshot` (after the `recording` field):

```cpp
        std::uint32_t grid;         // grid segments; 0 = off
```

Private state — next to `bool crossfade_ = true;`:

```cpp
    int grid_ = 0;                     // window snap grid, segments; 0 = off
```

and with the display mirrors (next to `dispRecording_`):

```cpp
    std::atomic<std::uint32_t> dispGrid_{0};
```

`src/loooop/dsp/LoopEngine.cpp` — setter (place near `setJitter`):

```cpp
void LoopEngine::setGrid(int segments) {
    const int g = segments < 2 ? 0 : segments;
    if (g == grid_) return;
    grid_ = g;
    dispGrid_.store(static_cast<std::uint32_t>(g), std::memory_order_relaxed);
}
```

Replace the body of the two-argument `windowBounds` (~line 219) with:

```cpp
void LoopEngine::windowBounds(const PlayHead& h, float jitterOff,
                              double& winStart, double& winLen) const {
    const double L = static_cast<double>(loopLen_);
    const double minWinLen = minWinLen_;
    winLen = static_cast<double>(h.size) * L;
    if (winLen < minWinLen) winLen = minWinLen;
    if (winLen > L)   winLen = L;
    double centre = static_cast<double>(clamp01(h.centre + jitterOff)) * L;
    if (grid_ >= 2) {
        // Grid: quantize the window to whole segments of seg = L/grid_ —
        // length to a segment count (>= 1, grown to respect the minimum
        // window), start to the nearest boundary. k + m <= grid_ keeps the
        // window exactly inside the loop, so no edge correction is needed.
        const double seg = L / static_cast<double>(grid_);
        long m = std::lround(winLen / seg);
        if (m < 1) m = 1;
        if (m > grid_) m = grid_;
        while (static_cast<double>(m) * seg < minWinLen && m < grid_) ++m;
        winLen = static_cast<double>(m) * seg;
        long k = std::lround((centre - winLen / 2.0) / seg);
        if (k < 0) k = 0;
        if (k > grid_ - m) k = grid_ - m;
        winStart = static_cast<double>(k) * seg;
        return;
    }
    winStart = centre - winLen / 2.0;
    if (winStart < 0.0) winStart = 0.0;
    if (winStart + winLen > L) winStart = L - winLen;
    if (winStart < 0.0) winStart = 0.0;
}
```

In `displaySnapshot()` add:

```cpp
    s.grid = dispGrid_.load(std::memory_order_relaxed);
```

- [x] **Step 4: Run the test to verify it passes**

Same command as Step 2. Expected: `All ... tests passed` output, exit 0, including every new `grid_*` check.

- [x] **Step 5: Run the whole suite**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh
```

Expected: all C++ and python tests pass (exit 0). The pre-existing tests must be untouched by the grid-off path.

- [x] **Step 6: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track
git add src/loooop/dsp/LoopEngine.hpp src/loooop/dsp/LoopEngine.cpp tests/loooop/test_loop_engine.cpp
git commit -m "feat: grid quantization of playhead windows in LoopEngine"
```

---

### Task 2: Grid bars in the display renderer

**Files:**
- Modify: `src/loooop/display/LoopWaveformRenderer.hpp` (color constants ~line 18)
- Modify: `src/loooop/display/LoopWaveformRenderer.cpp` (`renderWaveform` end, `renderLanes` start, anonymous namespace)
- Test: `tests/loooop/test_display_renderer.cpp`

**Interfaces:**
- Consumes: `DisplaySnapshot::grid` and `LoopEngine::setGrid(int)` from Task 1.
- Produces: `LoopWaveformRenderer::GRID[3]` color constant (tests and future tweaks reference it). No signature changes — grid arrives via the snapshot.

- [x] **Step 1: Write the failing tests**

Add to `tests/loooop/test_display_renderer.cpp` before `main()` (reuse `check`, `px`, `C`, `countColor`, `laneDim`, `laneBright`; W=64, H=64, lane 0 rows 32–39 with gap row 39):

```cpp
static void test_grid_bars() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.f); e.process(0.f); e.process(1.f); e.process(0.f);
    e.toggleRecord();                        // loop of 4, heads at pos 0
    e.setGrid(4);
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    const uint32_t grid = C(LoopWaveformRenderer::GRID);
    const uint32_t bg   = C(LoopWaveformRenderer::BG);
    // Interior boundaries at x = k*64/4 = 16, 32, 48; bar width 64/300 -> 1 px.
    check(px(16, 0) == grid && px(32, 0) == grid && px(48, 0) == grid,
          "grid: bars at interior boundaries in wave region");
    check(px(0, 0) == bg && px(15, 0) == bg && px(63, 0) == bg,
          "grid: no bars at loop edges");
    // x=32 is the waveform peak column: with grid on the bar slices the peak.
    check(px(32, 2) == grid, "grid: bar drawn over the waveform");
    // Lane region: bars sit UNDER the head bars. The full default window
    // covers x=16 in dim head color; the gap row below shows the grid line.
    check(px(16, 32) == laneDim(0), "grid: lane bars stay on top of grid lines");
    check(px(16, 39) == grid,       "grid: gap row shows grid line");
    // Off again: no grid pixels anywhere.
    e.setGrid(0);
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    check(countColor(grid) == 0, "grid off: no grid pixels");
}

static void test_grid_hidden_while_recording() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.setGrid(8);
    e.toggleRecord();
    for (int i = 0; i < 10; ++i) e.process(0.8f);   // still recording, no loop
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    check(countColor(C(LoopWaveformRenderer::GRID)) == 0,
          "grid: hidden until the loop freezes");
}
```

Add to `main()`:

```cpp
    test_grid_bars();
    test_grid_hidden_while_recording();
```

- [x] **Step 2: Run the test to verify it fails**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests
mkdir -p ../build/tests
g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_display_renderer \
    loooop/test_display_renderer.cpp ../src/loooop/dsp/LoopEngine.cpp \
    ../src/loooop/display/LoopWaveformRenderer.cpp && ../build/tests/test_display_renderer
```

Expected: compile FAILS — `GRID` is not a member of `LoopWaveformRenderer`.

- [x] **Step 3: Implement**

`src/loooop/display/LoopWaveformRenderer.hpp` — after the `WAVE` constant:

```cpp
    static constexpr uint8_t GRID[3] = {0x2E, 0x3A, 0x46};   // segment bars: above BG, below WAVE
```

`src/loooop/display/LoopWaveformRenderer.cpp` — in the anonymous namespace, after `vline`:

```cpp
// Vertical bars at a grid's interior segment boundaries, across the full
// region height. Callers pick the z-order: over the waveform (slicing it into
// chunks), under the lane bars (so head markers stay prominent).
void drawGridBars(uint32_t* buf, int width, int height, unsigned grid,
                  LoopWaveformRenderer::PackFn pack) {
    const uint32_t c = pack(LoopWaveformRenderer::GRID[0],
                            LoopWaveformRenderer::GRID[1],
                            LoopWaveformRenderer::GRID[2], 0xFF);
    const int bw = std::max(1, width / 300);
    for (unsigned k = 1; k < grid; ++k) {
        const int x = int(std::uint64_t(k) * unsigned(width) / grid);
        for (int dx = 0; dx < bw; ++dx)
            vline(buf, width, height, x + dx, 0, height - 1, c);
    }
}
```

At the end of `renderWaveform` (after the band drawing, still inside the function — note the grid draws only for a frozen loop, not the growing initial recording):

```cpp
    if (s.grid >= 2 && s.loopLen > 0)
        drawGridBars(buf, width, height, s.grid, pack);
```

Careful with scope: `s` and the band code are inside `if (waveH > 1)`; place the grid call after that block but before the function's closing brace (it needs `s`, which is declared at function scope).

In `renderLanes`, right after the `if (s.loopLen == 0) return;` line:

```cpp
    if (s.grid >= 2)
        drawGridBars(buf, width, height, s.grid, pack);
```

- [x] **Step 4: Run the test to verify it passes**

Same command as Step 2. Expected: all checks pass, including the pre-existing `split renderer: composed pixels are byte-identical` (both split entry points draw the same bars).

- [x] **Step 5: Run the whole suite**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh
```

Expected: exit 0.

- [x] **Step 6: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track
git add src/loooop/display/LoopWaveformRenderer.hpp src/loooop/display/LoopWaveformRenderer.cpp tests/loooop/test_display_renderer.cpp
git commit -m "feat: draw grid segment bars on the loop display"
```

---

### Task 3: VCV host wiring (param, context menu, display cache)

**Files:**
- Modify: `src/loooop/LooperModuleDSP.hpp` (add `gridSegments` helper)
- Modify: `src/loooop/Loooop.cpp` (`ParamId` line 16–21, `config()` ~line 72, `process()` ~line 95, `appendContextMenu` ~line 280)
- Modify: `src/loooop/LoopDisplay.hpp` (waveform cache key, ~line 20 and ~line 59)
- Test: `tests/loooop/test_module_dsp.cpp`

**Interfaces:**
- Consumes: `engine.setGrid(int)` (Task 1), `DisplaySnapshot::grid` (Task 1).
- Produces: `loooop::gridSegments(int choiceIdx) -> int` in `LooperModuleDSP.hpp` — maps menu index 0–3 to `{0, 4, 8, 16}`, out-of-range to 0. Task 4 uses the same helper.

- [x] **Step 1: Write the failing test**

In `tests/loooop/test_module_dsp.cpp`, add inside `main()` before the failure summary:

```cpp
    // Grid menu index -> engine segment count; anything out of range is Off.
    check(loooop::gridSegments(0) == 0,  "grid choice 0 = off");
    check(loooop::gridSegments(1) == 4,  "grid choice 1 = 4");
    check(loooop::gridSegments(2) == 8,  "grid choice 2 = 8");
    check(loooop::gridSegments(3) == 16, "grid choice 3 = 16");
    check(loooop::gridSegments(-1) == 0 && loooop::gridSegments(4) == 0,
          "grid choice out of range = off");
```

- [x] **Step 2: Run the test to verify it fails**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests
mkdir -p ../build/tests
g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_module_dsp \
    loooop/test_module_dsp.cpp && ../build/tests/test_module_dsp
```

Expected: compile FAILS — `gridSegments` is not a member of namespace `loooop`.

- [x] **Step 3: Implement the helper**

`src/loooop/LooperModuleDSP.hpp`, after `panRightGain`:

```cpp
// Grid menu choice (0..3: Off/4/8/16) -> LoopEngine::setGrid segment count.
inline int gridSegments(int choiceIdx) {
    constexpr int kGridChoices[4] = {0, 4, 8, 16};
    return (choiceIdx < 0 || choiceIdx > 3) ? 0 : kGridChoices[choiceIdx];
}
```

Run the Step 2 command again. Expected: PASS.

- [x] **Step 4: Wire the VCV module**

`src/loooop/Loooop.cpp`:

1. `ParamId` — append `GRID_PARAM` at the end of the global group (after the branch's `WRITE_MODE_PARAM`), before `PARAMS_LEN`:

```cpp
                   DRYWET_PARAM, RECORD_PARAM, CLEAR_PARAM, OVERDUB_PARAM, CROSSFADE_PARAM, WRITE_MODE_PARAM, GRID_PARAM,
                   PARAMS_LEN };
```

2. In the constructor, after the `WRITE_MODE_PARAM` configSwitch (~line 76):

```cpp
        configSwitch(GRID_PARAM, 0.f, 3.f, 0.f, "Grid", {"Off", "4", "8", "16"});
```

3. In `process()`, next to the `setWriteMode` call (~line 108):

```cpp
        engine.setGrid(loooop::gridSegments(
            (int)std::round(params[GRID_PARAM].getValue())));
```

4. In `appendContextMenu`, after the "Write mode" submenu item (~line 306) and before the per-head submenus:

```cpp
        static const std::vector<std::string> kGridLabels = {"Off", "4", "8", "16"};
        menu->addChild(createIndexSubmenuItem("Grid", kGridLabels,
            [m] { return (int)std::round(m->params[Loooop::GRID_PARAM].getValue()); },
            [m](int v) { m->paramQuantities[Loooop::GRID_PARAM]->setValue((float)v); }));
```

5. `src/loooop/LoopDisplay.hpp` — add grid to the waveform cache key. Next to `cachedWaveRevision`:

```cpp
    std::uint32_t cachedWaveGrid = UINT32_MAX;
```

and change the re-render condition and bookkeeping in `drawLayer`:

```cpp
            const auto revision = eng.waveformRevision();
            const auto grid = eng.displaySnapshot().grid;
            if (waveImg < 0 || cachedWaveRevision != revision || cachedWaveGrid != grid) {
                LoopWaveformRenderer::renderWaveform(
                    wavePix.data(), w, waveH, eng, loopDisplayPackRGBA);
                if (waveImg < 0)
                    waveImg = nvgCreateImageRGBA(args.vg, w, waveH, 0,
                        (const unsigned char*)wavePix.data());
                else
                    nvgUpdateImage(args.vg, waveImg, (const unsigned char*)wavePix.data());
                cachedWaveRevision = revision;
                cachedWaveGrid = grid;
            }
```

- [x] **Step 5: Build the VCV plugin to verify it compiles**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/vcv && make -j8
```

Expected: builds `plugin.dylib` with no errors. (Full GUI verification is a user check in Task 5.)

- [x] **Step 6: Run the whole suite**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/tests && ./run.sh
```

Expected: exit 0.

- [x] **Step 7: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track
git add src/loooop/LooperModuleDSP.hpp src/loooop/Loooop.cpp src/loooop/LoopDisplay.hpp tests/loooop/test_module_dsp.cpp
git commit -m "feat: Grid context-menu setting in the VCV Loooop module"
```

---

### Task 4: MetaModule host wiring

**Files:**
- Modify: `metamodule/loooop/QlpElements.hh` (new alt-param struct)
- Modify: `metamodule/loooop/Loooop_info.hh` (Elements array line 34/76–77, Elem enum line 144)
- Modify: `metamodule/loooop/LoooopCore.cc` (`update()` line 34–35, display cache line 120)
- Modify: `metamodule/loooop/sync-map-loooop.yaml` (menu-only exception)

**Interfaces:**
- Consumes: `engine_.setGrid(int)` and `DisplaySnapshot::grid` (Task 1), `loooop::gridSegments(int)` (Task 3).
- Produces: `Elem::GridAlt` — the last param element (named `…Alt` like the branch's `WriteModeAlt`); existing param/jack indices unchanged.

- [x] **Step 1: Add the alt-param element type**

`metamodule/loooop/QlpElements.hh`, after `QlpWriteModeAlt`:

```cpp
// Index 0 = Off so patches saved before this param (loader zero-inits unset
// alt-params) keep the ungridded behavior.
struct QlpGridAlt : AltParamChoiceLabeled {
    constexpr QlpGridAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 4, 0}, {"Off", "4", "8", "16"}} {}
};
```

- [x] **Step 2: Add the element and enum entry**

`metamodule/loooop/Loooop_info.hh`:

1. Array size: `std::array<Element, 86>` → `std::array<Element, 87>`.
2. In the `── Global params ──` block, after the `QlpWriteModeAlt` line (~line 78):

```cpp
        QlpGridAlt{{0.f, 0.f, Center, "Grid", ""}},
```

3. In `enum class Elem`, extend the global-params line (~line 145):

```cpp
        DryWetKnob, RecordButton, ClearButton, OverdubSwitch, CrossfadeSwitch, WriteModeAlt, GridAlt,
```

4. Update the header comment listing menu-only params ("Trig-mode, Speed V/Oct, Overdub, and Crossfade are menu-only") to include Grid.

- [x] **Step 3: Wire the core**

`metamodule/loooop/LoooopCore.cc`:

1. `update()`, after the `setWriteMode` call (~line 40):

```cpp
        engine_.setGrid(loooop::gridSegments(int(getState<GridAlt>())));
```

2. Display cache: add a member next to `cachedWaveRevision_`:

```cpp
    std::uint32_t cachedWaveGrid_ = UINT32_MAX;
```

and in `draw_graphic_display`, extend the waveform re-render condition (the `snap` local already exists):

```cpp
        if (cachedWaveRevision_ != revision || cachedWaveWidth_ != width
            || cachedWaveHeight_ != waveH || cachedWaveGrid_ != snap.grid) {
            LoopWaveformRenderer::renderWaveform(
                dispBuf_.data(), width, waveH, engine_, packARGB);
            cachedWaveRevision_ = revision;
            cachedWaveWidth_ = width;
            cachedWaveHeight_ = waveH;
            cachedWaveGrid_ = snap.grid;
        }
```

- [x] **Step 4: Add the sync-map exception**

`metamodule/loooop/sync-map-loooop.yaml`, with the other menu-only nulls:

```yaml
GridAlt: null
```

Note: the branch's `WriteModeAlt` is missing from this file (pre-existing gap). If `sync_info_positions.py` in Step 5 flags it, add `WriteModeAlt: null` in the same commit; otherwise leave it alone.

- [x] **Step 5: Verify the MetaModule build compiles and positions stay in sync**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/metamodule/build && cmake --build . -j8 2>&1 | tail -5
python3 /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track/metamodule/loooop/sync_info_positions.py
```

Expected: the cmake build succeeds (if the worktree has no configured `metamodule/build` dir, note it and rely on the compile being covered by the shared-source tests plus the user checklist — do NOT reconfigure toolchains speculatively). `sync_info_positions.py` reports Loooop positions in sync (the new element is a null-mapped menu param and needs no SVG element). Run `tests/run.sh` too — the python guard tests must stay green.

- [x] **Step 6: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track
git add metamodule/loooop/QlpElements.hh metamodule/loooop/Loooop_info.hh metamodule/loooop/LoooopCore.cc metamodule/loooop/sync-map-loooop.yaml
git commit -m "feat: Grid alt-param in the MetaModule Loooop core"
```

---

### Task 5: Docs and user checklist

**Files:**
- Modify: `Loooop.md` (Context menu section, ~line 92)

**Interfaces:** none (documentation only).

- [x] **Step 1: Document the setting**

In `Loooop.md`'s "Context menu" bullet list, after the "Crossfade loop seams" bullet (which on this branch follows the multi-line "Write mode" bullet):

```markdown
- **Grid** — Off (the default), 4, 8, or 16. When set, the loop is divided into that many equal segments, shown as vertical bars on the display, and every head's window snaps to them: **Size** becomes a whole number of segments and **Position** (including CV and **Jitter** offsets) lands on segment boundaries. Record a drum loop, set Grid to 16, and heads slice it cleanly on the beat.
```

Also add a "Rhythmic" mention where it fits naturally in Patch ideas, e.g. extend the "Rhythmic re-slicer" bullet: after "rearrange the beat" append ", and turn on **Grid** so every slice lands exactly on a division".

- [x] **Step 2: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/.worktrees/loooop-track
git add Loooop.md
git commit -m "docs: describe the Grid context-menu setting"
```

- [x] **Step 3: Present the user-run GUI checklist**

GUI checks are user-run in this project (no agent-driven GUI/simulator tests). Present this checklist at the end of the work, do not automate it:

1. **VCV Rack**: open Loooop, record a loop, right-click → Grid → 8. Vertical bars appear on the display; Position/Size knobs snap windows to the bars; Grid → Off removes them.
2. **VCV patch save/load**: save the patch with Grid = 8, reload, confirm Grid is still 8.
3. **MetaModule simulator**: load Loooop, confirm "Grid" appears in the module's Options with choices Off/4/8/16 and the display shows the bars when set.
