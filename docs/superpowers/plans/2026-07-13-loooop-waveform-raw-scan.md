# Loooop Raw-Sample Waveform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw the Loooop/Löp loop-display waveform directly from raw audio samples (constant resolution at any loop length) instead of the coarse 4096-bin peak summary, and retire the now-unused peak-bin machinery.

**Architecture:** The shared, Rack-free `LoopWaveformRenderer::renderWaveform` currently reads a precomputed min/max summary (`LoopEngine::peakMins/peakMaxs`, bin size derived from the 60 s max buffer). We add a raw-sample accessor to `LoopEngine`, port `renderWaveform` to compute each column's min/max from the raw loop buffer, then delete the peak arrays / `writePeak` and relocate the cache-invalidation revision bump. A VCV-side width cap bounds the offscreen texture cost.

**Tech Stack:** C++20, header-only renderer + `LoopEngine.cpp`, hand-rolled test harness compiled by `tests/run.sh` (g++ `-std=c++20 -O2`). VCV Rack widget (`LoopDisplay.hpp`) and MetaModule core (`LoooopCore.cc`) both consume the shared renderer.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-13-loooop-waveform-raw-scan-design.md`.
- The renderer is the **single source of truth for both platforms** (VCV widget + MetaModule core). Do not fork per-platform rendering logic.
- **Byte-identical guard:** every test in `tests/loooop/test_display_renderer.cpp` runs at `reset(10.f, 100.f)` (→ old `peakBinSize == 1`, i.e. per-sample) or asserts only lanes/geometry — so the raw-sample port MUST leave every one of those tests passing unchanged. Treat any renderer-test diff as a regression.
- Only the **waveform min/max source** changes. Do NOT change: lane/playhead rendering, `geometry()`, the dB-fullness height logic (`HEADROOM`/`LEVEL_REF`/`LEVEL_FLOOR`/`LEVEL_DB_FLOOR`), `MIN_SPLIT_ROWS` split-vs-combined behavior, or grid bars.
- Thread-safety model is unchanged: the GUI reads the loop buffer unlatched while audio may write it (transient tear during overdub, corrected on next revision bump). Do not add locks.
- No AI attribution in commit messages. Keep messages ≤ ~15 words.
- Run the loooop test suite with: `bash tests/run.sh` (builds + runs mf20/loooop/particules C++ tests and the python guards). For fast single-test iteration use the g++ one-liners given in each task.
- Work stays on this `loooop-track` worktree.

---

### Task 1: Add `sampleData()` accessor to LoopEngine (additive, non-breaking)

Add the raw-buffer read accessor the renderer will use, with tests. The peak machinery stays in place for now, so the build and all existing tests remain green.

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (add accessor near the other display accessors, ~line 73)
- Test: `tests/loooop/test_loop_engine.cpp` (new test `test_sample_data`, registered in `main`)

**Interfaces:**
- Produces: `const float* LoopEngine::sampleData(int ch) const` — returns a pointer to the start of the loop buffer for channel `ch` (0 = left, non-zero = right). Valid to read indices `[0, loopLength())` once frozen, or `[0, recordedLength())` while recording. Never null.

- [ ] **Step 1: Write the failing test**

Add to `tests/loooop/test_loop_engine.cpp` (place it right after `test_buffer_ceiling_autoend`, before `test_peaks_record`):

```cpp
static void test_sample_data() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.5f); e.process(-0.25f); e.process(1.f); e.process(0.f);
    e.toggleRecord();                     // loop = {0.5, -0.25, 1, 0}, mono -> mirrored
    check(e.loopLength() == 4,            "sampleData: loop length == 4");
    check(near(e.sampleData(0)[0], 0.5f), "sampleData: L[0] == 0.5");
    check(near(e.sampleData(0)[1], -0.25f), "sampleData: L[1] == -0.25");
    check(near(e.sampleData(0)[2], 1.f),  "sampleData: L[2] == 1");
    check(near(e.sampleData(1)[1], -0.25f), "sampleData: mono mirrors into R");
    // Overdub sums into the existing buffer at loop start.
    e.toggleRecord();                     // start overdub
    e.process(-2.f);                      // buf[0] = 0.5 + (-2) = -1.5
    e.toggleRecord();
    check(near(e.sampleData(0)[0], -1.5f), "sampleData: overdub sums into buffer");
}
```

Register it in `main` (add the call immediately before the `test_peaks_record();` line):

```cpp
    test_sample_data();
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cd tests && g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_loop_engine \
  loooop/test_loop_engine.cpp ../src/loooop/dsp/LoopEngine.cpp && ../build/tests/test_loop_engine
```
Expected: compile error — `'sampleData' is not a member of 'LoopEngine'`.

- [ ] **Step 3: Add the accessor**

In `src/loooop/dsp/LoopEngine.hpp`, immediately after the `displaySnapshot()` declaration / `waveformRevision()` accessor block (around line 73, next to `peakMaxs`), add:

```cpp
    // Raw loop buffer, one channel. The display renderer reads [0, axisLen)
    // (loopLength() frozen, recordedLength() while recording) to draw the
    // waveform per-sample. Read unlatched from the GUI thread — same model as
    // the display snapshot: a torn read during overdub self-corrects on the
    // next waveformRevision() bump.
    const float* sampleData(int ch = 0) const { return (ch ? bufR_ : bufL_).data(); }
```

- [ ] **Step 4: Run test to verify it passes**

Run the Step 2 command.
Expected: `ok: sampleData: ...` lines, no `FAIL:`, and the existing peak tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/loooop/dsp/LoopEngine.hpp tests/loooop/test_loop_engine.cpp
git commit -m "feat: add LoopEngine::sampleData raw-buffer accessor"
```

---

### Task 2: Port `renderWaveform` to read raw samples

Switch the waveform min/max source from peak bins to `sampleData`. The peak arrays remain in `LoopEngine` (unused by the renderer after this task). The existing renderer suite is the guard: it must stay byte-identical.

**Files:**
- Modify: `src/loooop/display/LoopWaveformRenderer.cpp:31-122` (`renderWaveform` body)
- Test: `tests/loooop/test_display_renderer.cpp` (no new assertions; existing suite must pass unchanged)

**Interfaces:**
- Consumes: `LoopEngine::sampleData(int)` (Task 1); `engine.displaySnapshot()`, `engine.numHeads()` (existing).
- Produces: no signature change — `renderWaveform(uint32_t* buf, int width, int height, const LoopEngine&, PackFn)` still fills exactly `width` columns.

- [ ] **Step 1: Run the existing renderer suite to establish the green baseline**

Run:
```bash
cd tests && g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_display_renderer \
  loooop/test_display_renderer.cpp ../src/loooop/dsp/LoopEngine.cpp \
  ../src/loooop/display/LoopWaveformRenderer.cpp && ../build/tests/test_display_renderer
```
Expected: `All display renderer tests passed.`

- [ ] **Step 2: Replace the peak-bin reads with raw-sample reads**

In `src/loooop/display/LoopWaveformRenderer.cpp`, inside `renderWaveform`, replace the block that currently reads (the four `peakMins/peakMaxs` lines plus `binSize` and the `lastBin`/peak loop):

```cpp
        const auto& minsL = engine.peakMins(0);
        const auto& maxsL = engine.peakMaxs(0);
        const auto& minsR = engine.peakMins(1);
        const auto& maxsR = engine.peakMaxs(1);
        const uint64_t binSize = engine.peakBinSize();

        // Level-aware height (same dB-fullness logic as before), from one
        // shared peak across both channels so the bands keep their relative
        // levels — a loop louder on the left draws taller on the left.
        const std::size_t lastBin =
            std::min(std::size_t((axisLen - 1) / binSize), std::size_t(LoopEngine::PEAK_BINS - 1));
        float peak = 0.f;
        for (std::size_t b = 0; b <= lastBin; ++b) {
            peak = std::max(peak, std::max(std::abs(minsL[b]), std::abs(maxsL[b])));
            peak = std::max(peak, std::max(std::abs(minsR[b]), std::abs(maxsR[b])));
        }
```

with:

```cpp
        const float* sampL = engine.sampleData(0);
        const float* sampR = engine.sampleData(1);

        // Level-aware height (same dB-fullness logic as before), from one
        // shared peak across both channels so the bands keep their relative
        // levels — a loop louder on the left draws taller on the left.
        float peak = 0.f;
        for (uint64_t i = 0; i < axisLen; ++i) {
            peak = std::max(peak, std::abs(sampL[i]));
            peak = std::max(peak, std::abs(sampR[i]));
        }
```

- [ ] **Step 3: Rewrite `drawBand` to scan samples instead of bins**

Still inside `renderWaveform`, replace the entire `drawBand` lambda AND its call sites. Replace this current lambda:

```cpp
        auto drawBand = [&](const float* mins, const float* maxs,
                            const float* mins2, const float* maxs2,
                            int bandTop, int bandH) {
            const float midY = bandTop + (bandH - 1) * 0.5f;
            const float yScale = (peak > 1e-6f)
                ? (bandH - 1) * 0.5f * fullness / peak : 0.f;
            for (int x = 0; x < width; ++x) {
                const uint64_t s0 = uint64_t(x) * axisLen / width;
                uint64_t s1 = uint64_t(x + 1) * axisLen / width;
                if (s1 <= s0) s1 = s0 + 1;
                auto b0 = std::size_t(s0 / binSize);
                auto b1 = std::size_t((s1 - 1) / binSize);
                b0 = std::min(b0, std::size_t(LoopEngine::PEAK_BINS - 1));
                b1 = std::min(b1, std::size_t(LoopEngine::PEAK_BINS - 1));
                float lo = mins[b0], hi = maxs[b0];
                for (std::size_t b = b0 + 1; b <= b1; ++b) {
                    lo = std::min(lo, mins[b]);
                    hi = std::max(hi, maxs[b]);
                }
                if (mins2) {
                    for (std::size_t b = b0; b <= b1; ++b) {
                        lo = std::min(lo, mins2[b]);
                        hi = std::max(hi, maxs2[b]);
                    }
                }
                int y0 = int(std::lround(midY - hi * yScale));
                int y1 = int(std::lround(midY - lo * yScale));
                y0 = std::max(y0, bandTop);
                y1 = std::min(y1, bandTop + bandH - 1);
                vline(buf, width, height, x, y0, y1, wave);
            }
        };

        if (waveH >= MIN_SPLIT_ROWS) {
            const int bandH = waveH / 2;      // odd waveH leaves a 1-row gap between bands
            drawBand(minsL.data(), maxsL.data(), nullptr, nullptr, 0, bandH);
            drawBand(minsR.data(), maxsR.data(), nullptr, nullptr, waveH - bandH, bandH);
        } else {
            drawBand(minsL.data(), maxsL.data(), minsR.data(), maxsR.data(), 0, waveH);
        }
```

with:

```cpp
        // One channel's band; a non-null second sample pointer widens each
        // column to the union of both channels (tiny-display fallback).
        auto drawBand = [&](const float* samp, const float* samp2,
                            int bandTop, int bandH) {
            const float midY = bandTop + (bandH - 1) * 0.5f;
            const float yScale = (peak > 1e-6f)
                ? (bandH - 1) * 0.5f * fullness / peak : 0.f;
            for (int x = 0; x < width; ++x) {
                const uint64_t s0 = uint64_t(x) * axisLen / width;
                uint64_t s1 = uint64_t(x + 1) * axisLen / width;
                if (s1 <= s0) s1 = s0 + 1;
                float lo = samp[s0], hi = samp[s0];
                for (uint64_t i = s0 + 1; i < s1; ++i) {
                    lo = std::min(lo, samp[i]);
                    hi = std::max(hi, samp[i]);
                }
                if (samp2) {
                    for (uint64_t i = s0; i < s1; ++i) {
                        lo = std::min(lo, samp2[i]);
                        hi = std::max(hi, samp2[i]);
                    }
                }
                int y0 = int(std::lround(midY - hi * yScale));
                int y1 = int(std::lround(midY - lo * yScale));
                y0 = std::max(y0, bandTop);
                y1 = std::min(y1, bandTop + bandH - 1);
                vline(buf, width, height, x, y0, y1, wave);
            }
        };

        if (waveH >= MIN_SPLIT_ROWS) {
            const int bandH = waveH / 2;      // odd waveH leaves a 1-row gap between bands
            drawBand(sampL, nullptr, 0, bandH);
            drawBand(sampR, nullptr, waveH - bandH, bandH);
        } else {
            drawBand(sampL, sampR, 0, waveH);
        }
```

Leave the surrounding code untouched: the `fullness` computation between the peak loop and `drawBand`, the `const uint32_t wave = ...` line, the grid-bar block at the end, and the early returns.

- [ ] **Step 4: Run the renderer suite — must be byte-identical (all pass)**

Run the Step 1 command.
Expected: `All display renderer tests passed.` (Rationale: at `binSize == 1` the old bin scan over `[s0, s1)` and the new sample scan over `[s0, s1)` are identical; tests at `reset(48000.f, 1.f)` only assert lane/geometry pixels, not wave shape.)

If any wave-pixel assertion now fails, STOP — the port diverged from per-sample semantics; do not "fix" the test. Re-check the `[s0, s1)` bounds against the original.

- [ ] **Step 5: Commit**

```bash
git add src/loooop/display/LoopWaveformRenderer.cpp
git commit -m "feat: draw loop waveform from raw samples, not peak bins"
```

---

### Task 3: Retire the peak-bin machinery and relocate the revision bump

With the renderer off the peak arrays, delete them and `writePeak`, and move the per-sample cache-invalidation bump to the sample-write sites. Update the engine tests that referenced peaks.

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (remove `PEAK_BINS`, `peakMins`/`peakMaxs`/`peakBinSize`, `writePeak` decl, `peakMin*_/peakMax*_`, `peakBinSize_`, `lastPeakBin_`; fix the non-atomic comment)
- Modify: `src/loooop/dsp/LoopEngine.cpp` (remove peak init in `reset()`/`clear()`, the `lastPeakBin_` line in `toggleRecord()`, `writePeak` definition; replace the two `writePeak(...)` calls in `process()` with `bumpWaveformRevision()`)
- Test: `tests/loooop/test_loop_engine.cpp` (delete the three `test_peaks_*` functions + their calls; rewrite `test_write_mode_peaks_track_decay` to read `sampleData`)

**Interfaces:**
- Consumes: `LoopEngine::bumpWaveformRevision()` (existing private helper), `LoopEngine::sampleData` (Task 1).
- Produces: `LoopEngine` no longer exposes `PEAK_BINS`, `peakMins`, `peakMaxs`, `peakBinSize`. `waveformRevision()` still bumps once per recorded sample and on reset/clear/freeze — unchanged externally.

- [ ] **Step 1: Update the tests first (they define the target API surface)**

In `tests/loooop/test_loop_engine.cpp`:

(a) Delete the three functions `test_peaks_record`, `test_peaks_overdub_and_clear`, and `test_peaks_stereo` in their entirety (currently ~lines 259–301).

(b) Delete their three call lines in `main`:
```cpp
    test_peaks_record();
    test_peaks_overdub_and_clear();
    test_peaks_stereo();
```

(c) Rewrite `test_write_mode_peaks_track_decay` to read the raw buffer instead of peaks. Replace the whole function with:

```cpp
static void test_write_mode_buffer_tracks_decay() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Layer);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(0.f);          // silent pass: buf *= FB
    e.toggleRecord();
    check(near(e.sampleData(0)[0], LoopEngine::LAYER_FEEDBACK),
          "write mode: buffer tracks decayed content");
}
```

Update its call in `main`:
```cpp
    test_write_mode_buffer_tracks_decay();
```

(Reconciliation note vs. spec: the spec mentioned a "clear-zeroes" assertion. `clear()` intentionally does NOT zero the ~60 s buffer — see the comment at `LoopEngine.cpp:120`. Clear is already covered elsewhere via `loopLength() == 0` making the display blank, so no clear-zeroes buffer assertion is added.)

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:
```bash
cd tests && g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_loop_engine \
  loooop/test_loop_engine.cpp ../src/loooop/dsp/LoopEngine.cpp && ../build/tests/test_loop_engine
```
Expected: compiles and PASSES right now (peaks still exist; the rewritten test uses `sampleData`, which exists). This confirms the test edits are self-consistent before we remove the engine internals. (The real "fail" gate is Step 4, after removal, guaranteeing nothing still references the peak API.)

- [ ] **Step 3: Remove the peak machinery from `LoopEngine`**

In `src/loooop/dsp/LoopEngine.hpp`:

- Delete the public peak API (currently lines 70–73):
```cpp
    static constexpr int PEAK_BINS = 4096;
    const std::array<float, PEAK_BINS>& peakMins(int ch = 0) const { return ch ? peakMinR_ : peakMinL_; }
    const std::array<float, PEAK_BINS>& peakMaxs(int ch = 0) const { return ch ? peakMaxR_ : peakMaxL_; }
    std::uint32_t peakBinSize() const { return peakBinSize_; }
```
- Delete the `writePeak` declaration (line 135):
```cpp
    void writePeak(std::size_t idx, float l, float r);
```
- Delete the peak member arrays and counters (lines 183–185):
```cpp
    std::array<float, PEAK_BINS> peakMinL_{}, peakMaxL_{}, peakMinR_{}, peakMaxR_{};
    std::uint32_t peakBinSize_ = 1;
    std::uint32_t lastPeakBin_ = UINT32_MAX;   // forces a bin reset on first write
```
- Fix the now-stale non-atomic comment near lines 78–79. Replace the sentence referencing "Peaks (above) and peakBinSize()" so it reads:
```cpp
    // 32-bit ARM. The raw loop buffers (bufL_/bufR_) are read unlatched by the
    // display: the audio thread writes, the GUI reads, tolerating a transient
    // tear that self-corrects on the next waveformRevision() bump.
```
(Adjust wording minimally to fit the surrounding comment; the key is removing the dangling `peakBinSize_` reference.)

In `src/loooop/dsp/LoopEngine.cpp`:

- In `reset()`, delete lines 23–27:
```cpp
    peakBinSize_ = static_cast<std::uint32_t>((maxSamples_ + PEAK_BINS - 1) / PEAK_BINS);
    if (peakBinSize_ == 0) peakBinSize_ = 1;
    peakMinL_.fill(0.f); peakMaxL_.fill(0.f);
    peakMinR_.fill(0.f); peakMaxR_.fill(0.f);
    lastPeakBin_ = UINT32_MAX;
```
- In `toggleRecord()`, delete line 71:
```cpp
        lastPeakBin_ = UINT32_MAX;          // first write re-seeds its bin
```
- In `clear()`, delete lines 137–139:
```cpp
    peakMinL_.fill(0.f); peakMaxL_.fill(0.f);
    peakMinR_.fill(0.f); peakMaxR_.fill(0.f);
    lastPeakBin_ = UINT32_MAX;
```
- Delete the entire `writePeak` definition (lines 544–558):
```cpp
void LoopEngine::writePeak(std::size_t idx, float l, float r) {
    std::uint32_t bin = static_cast<std::uint32_t>(idx / peakBinSize_);
    if (bin >= PEAK_BINS) bin = PEAK_BINS - 1;
    if (bin != lastPeakBin_) {
        peakMinL_[bin] = l; peakMaxL_[bin] = l;
        peakMinR_[bin] = r; peakMaxR_[bin] = r;
        lastPeakBin_ = bin;
    } else {
        if (l < peakMinL_[bin]) peakMinL_[bin] = l;
        if (l > peakMaxL_[bin]) peakMaxL_[bin] = l;
        if (r < peakMinR_[bin]) peakMinR_[bin] = r;
        if (r > peakMaxR_[bin]) peakMaxR_[bin] = r;
    }
    bumpWaveformRevision();
}
```

- [ ] **Step 4: Replace the `writePeak` calls in `process()` with the revision bump**

In `src/loooop/dsp/LoopEngine.cpp`, in the initial-record branch, replace:
```cpp
            writePeak(writeIdx_, inL, inR);
```
with:
```cpp
            bumpWaveformRevision();   // content changed -> invalidate display cache
```

And in the overdub branch, replace:
```cpp
            writePeak(writeIdx_, bufL_[writeIdx_], bufR_[writeIdx_]);
```
with:
```cpp
            bumpWaveformRevision();   // content changed -> invalidate display cache
```

Leave the post-freeze re-bump (`bumpWaveformRevision();` after the buffer-ceiling auto-end) exactly as is.

- [ ] **Step 5: Run the engine tests to verify they pass**

Run the Step 2 command.
Expected: no `FAIL:`, all engine tests pass, and it compiles cleanly (proving nothing else references the deleted peak API).

- [ ] **Step 6: Run the renderer suite to confirm no regression**

Run:
```bash
cd tests && g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_display_renderer \
  loooop/test_display_renderer.cpp ../src/loooop/dsp/LoopEngine.cpp \
  ../src/loooop/display/LoopWaveformRenderer.cpp && ../build/tests/test_display_renderer
```
Expected: `All display renderer tests passed.`

- [ ] **Step 7: Commit**

```bash
git add src/loooop/dsp/LoopEngine.hpp src/loooop/dsp/LoopEngine.cpp tests/loooop/test_loop_engine.cpp
git commit -m "refactor: retire peak-bin summary, bump revision on sample write"
```

---

### Task 4: Add the VCV-side width cap

Bound the VCV offscreen texture width with a tunable constant, exposed as a testable helper. MetaModule renders at native width and is unaffected.

**Files:**
- Modify: `src/loooop/display/LoopWaveformRenderer.hpp` (add `WAVE_WIDTH_CAP` + `cappedWidth()` helper)
- Modify: `src/loooop/LoopDisplay.hpp:47` (apply the cap to the offscreen buffer width)
- Test: `tests/loooop/test_display_renderer.cpp` (new `test_width_cap`, registered in `main`)

**Interfaces:**
- Consumes: nothing new.
- Produces: `static constexpr int LoopWaveformRenderer::WAVE_WIDTH_CAP` (default 1024); `static constexpr int LoopWaveformRenderer::cappedWidth(int w)` returning `min(w, WAVE_WIDTH_CAP)`.

- [ ] **Step 1: Write the failing test**

Add to `tests/loooop/test_display_renderer.cpp` (place before `main`):

```cpp
static void test_width_cap() {
    check(LoopWaveformRenderer::cappedWidth(500) == 500,
          "cap: width below cap is unchanged");
    check(LoopWaveformRenderer::cappedWidth(LoopWaveformRenderer::WAVE_WIDTH_CAP)
          == LoopWaveformRenderer::WAVE_WIDTH_CAP,
          "cap: width at cap is unchanged");
    check(LoopWaveformRenderer::cappedWidth(4096) == LoopWaveformRenderer::WAVE_WIDTH_CAP,
          "cap: width above cap is clamped");
    // A capped render still fills every column of its destination buffer.
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    for (int i = 0; i < 8; ++i) e.process(0.8f);
    e.toggleRecord();
    const int w = 200, h = 32;
    std::vector<uint32_t> b(std::size_t(w) * h);
    LoopWaveformRenderer::renderWaveform(b.data(), w, h, e, pack);
    const uint32_t bg = C(LoopWaveformRenderer::BG);
    int nonBg = 0;
    for (uint32_t p : b) nonBg += (p != bg);
    check(nonBg > 0, "cap: capped-width render draws a waveform");
}
```

Register it in `main` (add before the final `if (g_failures == 0)`):
```cpp
    test_width_cap();
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cd tests && g++ -std=c++20 -O2 -I../src -I../src/loooop -o ../build/tests/test_display_renderer \
  loooop/test_display_renderer.cpp ../src/loooop/dsp/LoopEngine.cpp \
  ../src/loooop/display/LoopWaveformRenderer.cpp && ../build/tests/test_display_renderer
```
Expected: compile error — `WAVE_WIDTH_CAP`/`cappedWidth` are not members of `LoopWaveformRenderer`.

- [ ] **Step 3: Add the constant and helper**

In `src/loooop/display/LoopWaveformRenderer.hpp`, inside the `public:` section (near `MIN_SPLIT_ROWS`, before the render method declarations), add:

```cpp
    // VCV offscreen-texture width bound. The waveform re-render is cached
    // (once per waveformRevision change), and its scan is O(loopLen) regardless
    // of render width, so this caps texture memory / paint cost, not the scan.
    // VCV applies it (nvgImagePattern stretches the capped texture to panel
    // width — imperceptible). MetaModule renders at native framebuffer width,
    // far below this, so it does not bind there. We can lower this freely if MM
    // ever needs it; below MM's native width it has no effect without an added
    // upscale path, and the O(loopLen) scan is the real MM cost.
    static constexpr int WAVE_WIDTH_CAP = 1024;
    static constexpr int cappedWidth(int w) { return w < WAVE_WIDTH_CAP ? w : WAVE_WIDTH_CAP; }
```

- [ ] **Step 4: Run test to verify it passes**

Run the Step 2 command.
Expected: no `FAIL:`, `All display renderer tests passed.`

- [ ] **Step 5: Apply the cap in the VCV widget**

In `src/loooop/LoopDisplay.hpp`, in `drawLayer`, change line 47 from:
```cpp
            const int w = std::max(1, (int)std::round(box.size.x)) * kOversample;
```
to:
```cpp
            const int w = LoopWaveformRenderer::cappedWidth(
                std::max(1, (int)std::round(box.size.x)) * kOversample);
```
Leave the `h` computation (line 48) unchanged — no height cap.

- [ ] **Step 6: Verify the full test suite and the VCV build**

Run the whole suite:
```bash
bash tests/run.sh
```
Expected: all mf20/loooop/particules C++ tests pass and the python guards pass; final nonzero exit only on failure.

Then confirm the VCV plugin still compiles with the changed engine API and widget (per project build convention):
```bash
make -C vcv
```
Expected: clean build, no errors referencing `peakMins`/`peakMaxs`/`peakBinSize`/`writePeak` or `cappedWidth`.

- [ ] **Step 7: Commit**

```bash
git add src/loooop/display/LoopWaveformRenderer.hpp src/loooop/LoopDisplay.hpp tests/loooop/test_display_renderer.cpp
git commit -m "feat: cap VCV loop-display texture width (WAVE_WIDTH_CAP)"
```

---

## Post-implementation (not a code task)

- **GUI visual check** goes to the user-run simulator checklist per project convention (no agent-driven GUI-sim test): confirm short loops are smooth (not blocky), and that lanes, grid bars, stereo split, armed-head dimming, and the recording view are unchanged. Build/run VCV and, if desired, the MetaModule build to eyeball the display.
- The stale `// peakBinSize == 1` / `// maxSamples=1000 -> peakBinSize == 1` comments in the test files are harmless annotations; optionally tidy them, but they are not load-bearing.

## Self-Review (completed by plan author)

- **Spec coverage:** raw-sample envelope → Task 2; engine accessor + peak removal + revision-bump relocation → Tasks 1 & 3; width cap (VCV-side, constant, MM note) → Task 4; test updates (engine peak tests removed, renderer suite as byte-identical guard, cap test) → Tasks 1/3/4; thread-safety unchanged → Global Constraints + accessor comment. Risks (thread-safety, audio-path edit) → covered by keeping bump sites/frequency identical and by the engine suite.
- **Placeholder scan:** none — every code step shows the exact before/after.
- **Type consistency:** `sampleData(int)` defined in Task 1 and consumed by Tasks 2–4; `cappedWidth(int)`/`WAVE_WIDTH_CAP` defined in Task 4 Step 3 and consumed Step 5; renamed test `test_write_mode_buffer_tracks_decay` updated in both definition and `main` call.
- **Spec reconciliation:** the spec's "clear-zeroes" test idea is dropped (clear does not zero the buffer by design); noted inline in Task 3 Step 1.
