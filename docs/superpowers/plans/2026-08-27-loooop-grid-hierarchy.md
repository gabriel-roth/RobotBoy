# Loooop/Löp Grid Bar Depth Hierarchy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the MetaModule bug where high Grid settings (32, 64) paint the Loooop/Löp waveform display solid with grid-colored bars, by replacing the flat "every boundary is a full-height, uniformly bright bar" renderer with a depth-ranked hierarchy: coarse divisions (shared with lower Grid settings) stay full-height and bright, while divisions that only exist at a finer setting draw as shorter, dimmer ticks anchored at the region's top/bottom edges.

**Architecture:** `LooperModuleDSP.hpp`'s Grid choice ladder (`kGridChoices = {0, 4, 8, 16, 32, 64}`) is a strict doubling sequence, so every boundary of a coarser setting is exactly reproduced at a finer one (e.g. grid=32's boundaries are a subset of grid=64's). This plan exploits that: for a boundary index `k` out of `grid` segments, its "depth" is the ladder rung that first introduces it, computed from the trailing zero bits of `k`. Depth 0 (coarsest) always renders as today's flat full-height bar; each deeper level renders dimmer and shorter. This is a single-function rewrite in the platform-shared `LoopWaveformRenderer.cpp` — no signature or call-site changes, so both VCV and MetaModule pick it up automatically. Root cause and the numeric derivation (Loooop's MetaModule display renders at ~74×42px, computed from `mm_to_px` in the MetaModule SDK) were confirmed by reading `LoopWaveformRenderer.cpp`/`.hpp`, `LoooopCore.cc`, and the MetaModule SDK's `units.hh` in this conversation — not re-derived here.

**Tech Stack:** C++20 (VCV Rack plugin `Makefile`, MetaModule `CMakeLists.txt`, and the plain-`g++` DSP test harness `tests/run.sh` all already build at `-std=c++20`). `<bit>`'s `std::countr_zero` is used here for the first time in this repo, but the same function is already used elsewhere in the MetaModule firmware tree (e.g. `cpputil/util/compact_binary_serializer.hh`) on the same `arm-none-eabi-gcc` 12.2+ toolchain, so it's known to be available.

**Spec:** No separate spec file — this is a bounded, single-function change; the design was agreed in conversation (see Architecture above for the full mechanism, confirmed against the code and numerically verified before this plan was written).

## Global Constraints

- Work happens directly on the main checkout (`/Users/gabrielroth/Dev/RobotBoy`) — there is no active Loooop track worktree right now (`git worktree list` shows only main; the grid-32/64 feature that this bug affects already merged to main in `LooperModuleDSP.hpp`).
- Only `src/loooop/display/LoopWaveformRenderer.cpp` and `tests/loooop/test_display_renderer.cpp` change. No signature changes to `LoopWaveformRenderer::render`/`renderWaveform`/`renderLanes`, so `LoooopCore.cc`, `LopCore.cc`, and `LoopDisplay.hpp` (VCV) need no edits.
- `LoopEngine`'s audio-affecting grid/window-snapping logic (`LoopEngine.cpp:359-374`, `windowBoundsUncached`) is untouched — this is a rendering-only fix.
- The existing tests `test_grid_bars` and `test_grid_hidden_while_recording` in `tests/loooop/test_display_renderer.cpp` must keep passing unmodified — grid=4 (the ladder's base rung, depth 0 only) must render pixel-identical to today's flat bars.
- Commit messages: one short sentence, no AI attribution / no Co-Authored-By lines.
- No GUI or simulator testing by the agent — visual/legibility checks go on the user-run checklist at the end.

---

### Task 1: Depth-ranked grid bar hierarchy

**Files:**
- Modify: `src/loooop/display/LoopWaveformRenderer.cpp:1-29` (the `vline`/`drawGridBars` block in the anonymous namespace)
- Test: `tests/loooop/test_display_renderer.cpp` (new tests after `test_grid_hidden_while_recording`, before `test_width_cap`; new calls in `main`)

**Interfaces:**
- Consumes: existing public API only — `LoopWaveformRenderer::renderWaveform(uint32_t*, int width, int height, const LoopEngine&, PackFn)`, `LoopEngine::setGrid(int)`, `LoopEngine::reset(float, float)`, `LoopEngine::toggleRecord()`, `LoopEngine::process(float)`. No new public symbols.
- Produces: nothing consumed by a later task — this is the complete fix in one task.

- [ ] **Step 1: Write the failing tests**

In `tests/loooop/test_display_renderer.cpp`, insert the following three new test functions between the closing brace of `test_grid_hidden_while_recording()` and `static void test_width_cap() {`:

```cpp
static void test_grid_hierarchy_depths() {
    // A silent loop (all-zero samples) draws a single flat line per band, at
    // a fixed known row, and BG everywhere else -- isolating the grid bars'
    // colors/coverage from any waveform content.
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.f); e.process(0.f); e.process(0.f); e.process(0.f);
    e.toggleRecord();                     // silent loop of 4
    e.setGrid(64);
    static constexpr int width = 64, height = 32;   // width=64 makes grid=64's
                                                     // boundaries land exactly
                                                     // at x=k (no aliasing)
    std::vector<uint32_t> local(std::size_t(width) * height);
    LoopWaveformRenderer::renderWaveform(local.data(), width, height, e, pack);
    auto at = [&](int x, int y) { return local[std::size_t(y) * width + x]; };
    const uint32_t bright = C(LoopWaveformRenderer::GRID);
    const uint32_t bg = C(LoopWaveformRenderer::BG);

    // x=16 (k=16, 4 trailing zero bits) is depth 0 -- shared with grid=4/8/16/32
    // -- so it must stay full-height and undimmed, same as the flat pre-hierarchy bars.
    check(at(16, 0) == bright && at(16, height - 1) == bright && at(16, height / 2) == bright,
          "grid hierarchy: depth-0 boundary (x=16) is full-height and undimmed");

    // x=1 (k=1, 0 trailing zero bits) is depth 4 -- new only at grid=64 -- so it
    // must be dimmer than pure GRID, and must NOT reach the middle row: the
    // waveform's center stays visible even at the densest setting.
    const uint32_t finest = at(1, 0);
    check(finest != bright && finest != bg,
          "grid hierarchy: depth-4 boundary (x=1) is dimmed, not full GRID or BG");
    check(at(1, height / 2) == bg,
          "grid hierarchy: depth-4 boundary (x=1) does not reach the middle row");
}

static void test_grid_hierarchy_zorder_coarse_wins_collision() {
    // At width=40, grid=64: k=1 (depth 4, x=0) is isolated and must stay dim.
    // k=16 (depth 0) and k=17 (depth 4) both round to x=10 -- the depth-0 bar
    // must win that shared column outright, not be partially overwritten by
    // the finer one drawn for k=17.
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.f); e.process(0.f); e.process(0.f); e.process(0.f);
    e.toggleRecord();
    e.setGrid(64);
    static constexpr int width = 40, height = 32;
    std::vector<uint32_t> local(std::size_t(width) * height);
    LoopWaveformRenderer::renderWaveform(local.data(), width, height, e, pack);
    auto at = [&](int x, int y) { return local[std::size_t(y) * width + x]; };
    const uint32_t bright = C(LoopWaveformRenderer::GRID);

    check(at(0, 0) != bright,
          "grid hierarchy: an isolated depth-4 boundary (x=0) is dimmed, not full GRID");
    check(at(10, 0) == bright && at(10, height - 1) == bright,
          "grid hierarchy: a depth-0 boundary fully wins a column shared with a finer one (x=10)");
}

static void test_grid_narrow_display_preserves_center() {
    // Regression test for the original bug: on MetaModule's actual Loooop
    // display (~74px wide), grid=64 used to paint every one of its 63
    // boundaries as a full-height bar, turning nearly the entire display --
    // including its vertical center -- solid GRID color. With the depth
    // hierarchy, only the coarsest (depth-0) boundaries reach the center
    // row; there are exactly 3 of them at this width/grid combination
    // (k=16, 32, 48, landing at x=18, 37, 55). (This test drives
    // renderWaveform directly with the full 74x42 as one wave region --
    // on real hardware geometry() splits off a lane slice, so 42 isn't
    // literally the on-device wave-region height, but the boundary math
    // this test checks is unaffected by that split.)
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.f); e.process(0.f); e.process(0.f); e.process(0.f);
    e.toggleRecord();
    e.setGrid(64);
    static constexpr int width = 74, height = 42;   // Loooop's real MetaModule display size
    std::vector<uint32_t> local(std::size_t(width) * height);
    LoopWaveformRenderer::renderWaveform(local.data(), width, height, e, pack);
    const uint32_t grid = C(LoopWaveformRenderer::GRID);
    const int midRow = height / 2;
    int gridPixelsAtMidRow = 0;
    for (int x = 0; x < width; ++x)
        if (local[std::size_t(midRow) * width + x] == grid) ++gridPixelsAtMidRow;
    check(gridPixelsAtMidRow == 3,
          "grid hierarchy: at grid=64 on a 74px display, only the 3 depth-0 boundaries reach the center row");
}
```

Then, in `main()`, change:

```cpp
    test_grid_bars();
    test_grid_hidden_while_recording();
    test_width_cap();
```

to:

```cpp
    test_grid_bars();
    test_grid_hidden_while_recording();
    test_grid_hierarchy_depths();
    test_grid_hierarchy_zorder_coarse_wins_collision();
    test_grid_narrow_display_preserves_center();
    test_width_cap();
```

- [ ] **Step 2: Run tests to verify the new checks fail**

Run: `cd tests && ./run.sh; cd ..`

Expected: FAIL on these specific lines (the current flat `drawGridBars` draws every boundary as a full-height, undimmed bar, so it can't tell depth-0 from depth-4 boundaries apart):
- `grid hierarchy: depth-4 boundary (x=1) is dimmed, not full GRID or BG`
- `grid hierarchy: depth-4 boundary (x=1) does not reach the middle row`
- `grid hierarchy: an isolated depth-4 boundary (x=0) is dimmed, not full GRID`
- `grid hierarchy: at grid=64 on a 74px display, only the 3 depth-0 boundaries reach the center row`

The remaining new assertions (the depth-0 "stays bright/full-height" and "wins the collision" checks) are expected to already pass, since the current code always draws full-height bright bars — that's fine, they become meaningful regression guards once Step 3 is done. All pre-existing tests (including `test_grid_bars`) must still pass. `run.sh` exits non-zero due to the four failures above.

- [ ] **Step 3: Implement the depth hierarchy**

In `src/loooop/display/LoopWaveformRenderer.cpp`, add `<bit>` to the includes — change:

```cpp
#include "LoopWaveformRenderer.hpp"
#include <algorithm>
#include <cmath>
```

to:

```cpp
#include "LoopWaveformRenderer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
```

Then replace the comment and `drawGridBars` function (currently):

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

with:

```cpp
// Vertical bars at a grid's interior segment boundaries. Callers pick the
// z-order: over the waveform (slicing it into chunks), under the lane bars
// (so head markers stay prominent).
//
// The Grid choice ladder (loooop::gridSegments' kGridChoices, in
// LooperModuleDSP.hpp) is a strict doubling sequence: 4, 8, 16, 32, 64. That
// means every boundary of a coarser setting is exactly reproduced at a finer
// one -- grid=32's boundaries are a subset of grid=64's, and so on down to
// grid=4. So boundary k (of `grid` segments) has a "depth": the ladder rung
// that first introduces it, found from k's trailing zero bits. Depth 0 (the
// rung shared by every setting) always draws full-height and brightest.
// Each deeper level draws a shorter tick anchored at the region's top/bottom
// edges, and dimmer, so a dense setting on a narrow display (MetaModule's
// ~74px-wide Loooop display at grid=64) shades in from the edges instead of
// painting over the waveform's middle rows.
void drawGridBars(uint32_t* buf, int width, int height, unsigned grid,
                  LoopWaveformRenderer::PackFn pack) {
    const int n = std::clamp(
        int(std::lround(std::log2(double(std::max(grid, 4u)) / 4.0))), 0, 4);
    const int bw = std::max(1, width / 300);
    constexpr float kMaxDim = 0.6f;   // deepest level blends 60% of the way toward BG

    // Draw dimmest depths first, brightest (depth 0) last, so a bright
    // boundary is never partially overwritten by a dim one sharing a column.
    for (int depth = n; depth >= 0; --depth) {
        const float t = n > 0 ? float(depth) / float(n) : 0.f;
        const auto blend = [t](uint8_t from, uint8_t to) {
            return uint8_t(std::lround(from + (float(to) - float(from)) * t * kMaxDim));
        };
        const uint32_t c = pack(blend(LoopWaveformRenderer::GRID[0], LoopWaveformRenderer::BG[0]),
                                blend(LoopWaveformRenderer::GRID[1], LoopWaveformRenderer::BG[1]),
                                blend(LoopWaveformRenderer::GRID[2], LoopWaveformRenderer::BG[2]),
                                0xFF);
        // depth 0 always spans the full height (both "ticks" cover 0..height-1,
        // drawn twice harmlessly) -- this keeps grid=4 pixel-identical to the
        // pre-hierarchy renderer regardless of height parity.
        const int half = (depth == 0) ? height : std::max(1, height / (2 * (depth + 1)));
        for (unsigned k = 1; k < grid; ++k) {
            const int kDepth = n - std::min(int(std::countr_zero(k)), n);
            if (kDepth != depth) continue;
            const int x = int(std::uint64_t(k) * unsigned(width) / grid);
            for (int dx = 0; dx < bw; ++dx) {
                vline(buf, width, height, x + dx, 0, half - 1, c);
                vline(buf, width, height, x + dx, height - half, height - 1, c);
            }
        }
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd tests && ./run.sh; cd ..`

Expected: every assertion passes, including all new `grid hierarchy:` lines, and the pre-existing `grid:` lines from `test_grid_bars`/`test_grid_hidden_while_recording`. `run.sh`'s `mf20`, `particules`, `onbetap`, and `vespid` test binaries are unaffected by this change but re-run as part of the same script -- confirm none of them regressed either. Exit code 0.

- [ ] **Step 5: Compile both hosts to catch anything the test harness can't**

VCV (compile + link only, no install):

Run: `make -C vcv -j8`
Expected: builds `plugin.dylib` with no errors (the `<bit>` include and `std::countr_zero` must compile under the VCV toolchain's `-std=c++20`, same as the test harness).

MetaModule (needs the ARM cross-compiler on PATH):

Run: `PATH="/Users/gabrielroth/Dev/opt/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin:$PATH" cmake --build metamodule/build 2>&1 | tail -20`
Expected: build completes with no errors on `arm-none-eabi-g++`.

- [ ] **Step 6: Commit**

```bash
git add src/loooop/display/LoopWaveformRenderer.cpp tests/loooop/test_display_renderer.cpp
git commit -m "fix: Loooop/Löp grid bars dim and shorten by depth so dense settings don't erase the waveform"
```

---

## User-run checklist (after implementation)

Not agent work — for the user to verify by hand (per this repo's convention of no agent-driven GUI/simulator checks):

- [ ] MetaModule (simulator or hardware): Loooop and Löp displays at Grid=64 with a frozen loop — confirm the waveform's shape is visible again, with a light dusting of fine grid ticks near the top/bottom edges and a few bold full-height dividers.
- [ ] MetaModule: Grid=32 — confirm it now reads as visibly sparser than Grid=64 (fewer/dimmer fine ticks), rather than looking identical.
- [ ] MetaModule: Grid=4 — confirm pixel-identical to pre-fix (depth-0-only setting, no visual change expected).
- [ ] MetaModule: Grid=8/16 — confirm they now show depth-hierarchy shading from the edges (sparse/dim fine ticks tapering top/bottom, bold full-height dividers in the middle); this is the intended fix, not a regression — flag only if they still look like solid painted-over bars or visibly broken.
- [ ] VCV: same checks as above on the wider VCV panel widget, to confirm the shared renderer still looks good there (bars should look slightly nicer/hierarchical, not broken).
- [ ] Löp: same Grid checks (Löp shares `LoopWaveformRenderer` with Loooop).
- [ ] `renderLanes` also calls `drawGridBars`, so the lane-gap rows will now show dim short ticks at fine grid settings too — confirm this reads fine there, not distracting.
