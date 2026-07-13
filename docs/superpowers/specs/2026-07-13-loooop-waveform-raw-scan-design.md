# Loooop waveform: draw from raw samples, retire the peak-bin summary

**Date:** 2026-07-13
**Module:** Loooop + Löp (VCV Rack + MetaModule). The waveform renderer is shared, so both modules and both platforms are affected.

## Problem

The loop-display waveform looks blocky, especially for short loops. The cause is
the peak-bin summary the renderer draws from.

While recording, `LoopEngine` maintains a fixed **4096-bin** min/max summary of
the audio (`peakMinL_/peakMaxL_/peakMinR_/peakMaxR_`, filled by `writePeak`).
`renderWaveform` draws the waveform from that summary. The bin size is derived
from the **maximum buffer capacity**, not the actual loop length:

```
peakBinSize_ = ceil(maxSamples_ / PEAK_BINS)   // LoopEngine.cpp:23
```

Both modules call `engine.reset(48000.f)`, so `maxSeconds` defaults to 60 →
`maxSamples_ ≈ 2.88M` → **~704 samples/bin**. A loop only populates
`loopLen / 704` bins, which are then stretched across the full display width:

| Loop length | Bins populated | Columns per bin (VCV, ~1122px internal) |
|-------------|----------------|------------------------------------------|
| 1 s | ~68 | ~16 (very blocky) |
| 4 s | ~273 | ~4 (blocky) |
| 10 s | ~682 | ~1.6 (fine) |

The shorter the loop relative to the 60s ceiling, the coarser the resolution.

## Goal

Draw the waveform directly from the recorded audio samples so resolution is
constant regardless of loop length, and retire the now-unused peak-bin summary.

Non-goal: changing the lane/playhead rendering, the level-aware dB-fullness
height logic, the stereo split / combined fallback, or the grid bars. Those all
stay exactly as they are — only the *source of the waveform min/max* changes.

## Approach (chosen: Option 2, raw-sample envelope, width cap only)

The renderer computes each pixel column's min/max by scanning that column's exact
sample span in the raw loop buffer, instead of reading precomputed bins. This is
a true per-pixel envelope. Because both platforms already **cache** the rendered
waveform and re-rasterize only when the audio changes (`waveformRevision`), the
per-render cost is paid once per content change (record / overdub / freeze / grid
/ geometry change), never per frame.

### Cost model and the width cap

A raw-sample render costs roughly:

- **Scan term: O(loopLen)** — every sample in the loop is read to find the
  column min/max, independent of render width.
- **Draw term: O(width × height)** — per-column vertical fills, per-column index
  math, and the output buffer size.

A **width cap bounds the draw term, memory, and per-column overhead — not the
O(loopLen) scan.** For a long loop the scan dominates (a 60s loop ≈ 5.8M sample
reads → a one-time GUI-thread cost of maybe tens of ms on MetaModule's ARM, once
per freeze; typical 2–8s loops are ~1–5ms). Rendering is GUI-side on both
platforms, so this never affects audio.

We add a width cap only. The O(loopLen) scan is accepted as-is. If the freeze-time
scan ever hitches on MetaModule, the documented follow-up lever is scan
decimation (striding the scan, trading a little accuracy on very long loops) —
**deliberately deferred; not built now.**

### Width cap details

- New constant `WAVE_WIDTH_CAP` in `LoopWaveformRenderer.hpp`. **Default 1024.**
- Applied **VCV-side**: the widget allocates its offscreen pixel buffer at
  `w = min(round(box.size.x) * kOversample, WAVE_WIDTH_CAP)`. `nvgImagePattern`
  already stretches that texture to the panel width (GPU bilinear), so a lower
  cap is visually imperceptible. Lowering the cap reduces VCV texture memory,
  paint cost, and per-column overhead.
- **MetaModule renders directly into the native-width framebuffer** (`dispBuf_`),
  whose width comes from the 39.733mm display region — on the order of ~120–160px,
  far below 1024, so the cap does not bind on MM. It would not reduce MM's
  dominant cost (the scan) anyway. **We can lower `WAVE_WIDTH_CAP` freely if we
  hit CPU constraints on MetaModule; note that below MM's native framebuffer width
  it would have no effect without an added upscale path, and the scan cost is what
  actually matters there.**

`renderWaveform` keeps its current contract: it fills exactly `width` columns from
the sample range. The cap lives at the VCV call site, not inside the renderer, so
MM's direct-to-framebuffer path is untouched.

## Changes

### 1. Engine: expose raw samples, retire the peak summary

`src/loooop/dsp/LoopEngine.hpp` / `.cpp`:

- **Add** `const float* sampleData(int ch) const { return (ch ? bufR_ : bufL_).data(); }`.
  The renderer reads `[0, axisLen)` where `axisLen` = `loopLen` (frozen) or
  `recordedLen` (recording) — the same axis it derives today from the display
  snapshot.
- **Remove** the peak-bin machinery: `PEAK_BINS`, `peakMinL_/peakMaxL_/peakMinR_/
  peakMaxR_`, `peakBinSize_`, `lastPeakBin_`, the `peakMins()/peakMaxs()/
  peakBinSize()` accessors, `writePeak()` (declaration + definition), the
  `peakBinSize_` computation and `peak*_.fill()` calls in `reset()`, and the
  `lastPeakBin_` reset.
- **Relocate the revision bump.** `writePeak` currently ends with
  `bumpWaveformRevision()`, and it is called once per recorded sample in both the
  initial-record and overdub branches of `process()`. Replace each `writePeak(...)`
  call with a direct `bumpWaveformRevision()` — same per-sample "content changed"
  frequency, just without building the summary. The other four bump sites
  (reset, clear/state changes, and the post-freeze re-bump at the buffer ceiling)
  are unchanged.

### 2. Renderer: read samples instead of bins

`src/loooop/display/LoopWaveformRenderer.cpp` — `renderWaveform` only:

- Replace `engine.peakMins/peakMaxs/peakBinSize()` with `engine.sampleData(0/1)`
  and index by sample.
- **Peak pass:** scan `[0, axisLen)` for the global peak that drives the existing
  dB-fullness height logic (`HEADROOM`/`LEVEL_REF`/`LEVEL_FLOOR`/`LEVEL_DB_FLOOR`
  — all unchanged).
- **Draw pass (`drawBand`):** for each column `x`, compute `[s0, s1)` as today,
  then min/max over `sampleData` in that span (union both channels in the
  combined-fallback case, exactly as the current `mins2/maxs2` path does).
- Everything else in the function — `MIN_SPLIT_ROWS` split vs combined,
  `yScale`/`midY`, grid bars via `drawGridBars`, background fill — stays as-is.
- This is a structural 1:1 port: "bin index" becomes "sample index," two passes
  over the sample range, no extra allocation.

The `LoopWaveformRenderer.hpp` doc comment that mentions the pack-function
contract stays; update any bin-specific wording.

### 3. VCV widget: apply the cap

`src/loooop/LoopDisplay.hpp` — clamp the offscreen buffer width:
`const int w = min(round(box.size.x) * kOversample, WAVE_WIDTH_CAP)`.
No other widget change; the existing size-change reallocation and
`nvgImagePattern` stretch already handle a smaller buffer.

### 4. MetaModule core: no change

`metamodule/loooop/LoooopCore.cc` continues to pass its native `dispWidth_` and
render straight into `dispBuf_`. It picks up the raw-sample renderer automatically
(shared code). The cap does not bind at native width.

## Testing

- **`tests/loooop/test_loop_engine.cpp`:** remove the peak-bin assertions (the
  `peaks:` and `stereo peaks:` blocks around lines 260–300 and the layer-feedback
  peak check near 1183–1191, which read `peakMaxs/peakMins/peakBinSize`). Replace
  with a small test of `sampleData`: record a known signal and assert the buffer
  holds the expected samples (initial record, overdub, and clear-zeroes).
- **`tests/loooop/test_display_renderer.cpp`:** this renders and inspects pixels.
  Re-derive expectations for the raw-sample path (the `peakBinSize == 1` setup
  comment at line 74 becomes irrelevant; with tiny test rates and small buffers,
  per-sample and per-bin results should coincide, but verify each assertion and
  update any that assumed bin coarseness). Add at least one case that exercises
  the width cap (render width > cap) to confirm the VCV buffer clamps and still
  fills every column.
- Build and run the existing headless renderer/engine tests; both must pass.
- GUI-simulator visual confirmation (blockiness gone, no regressions in lanes /
  grid / stereo split) goes to the user-run checklist per project convention —
  not an agent-driven simulator test.

## Risks

- **Thread-safety (unchanged from status quo).** The renderer reads the raw buffer
  unlatched from the GUI thread while the audio thread may be writing. Initial-record
  samples are write-once below `recordedLen` (safe). Overdub rewrites the loop in
  place, so a mid-overdub read can be torn → a transient visual glitch, corrected on
  the next revision bump. This is the same risk profile as the current unlatched
  peak-bin reads; no new synchronization is introduced.
- **Audio path touched.** Removing `writePeak` and relocating the revision bump
  edits `process()`. Mitigated by keeping the bump at the identical call sites and
  frequency, and by the engine tests.
