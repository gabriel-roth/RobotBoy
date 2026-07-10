# Loooop Grid Mode — Design

2026-07-10

## Purpose

Add Gloop's grid function to Loooop: an optional global grid that divides the
frozen loop into 4, 8, or 16 equal segments and snaps every playhead's window
(position and size) to that grid. This makes rhythmic slicing practical — a
size knob selects a whole number of segments, a position knob (or CV, or
jitter) jumps between segment boundaries instead of landing anywhere.

Reference: Gloop's CV/MISC menu "Grid: off / 4 / 8 / 16"
(`~/Dev/GloopResources/gloop-video-demo-transcript.txt` lines 213–231).

## Setting

One global choice, **Grid: Off / 4 / 8 / 16**, default Off.

- **VCV** (`src/loooop/Loooop.cpp`): new `GRID_PARAM` appended to `ParamId`
  immediately before `PARAMS_LEN` (a `configSwitch` with labels
  `{"Off", "4", "8", "16"}`, values 0–3, default 0). Exposed in the context
  menu as an index submenu ("Grid"), alongside Overdub and Crossfade.
  Appending at the end of the enum leaves existing patch param ids intact.
- **MetaModule** (`metamodule/loooop/`): new `QlpGridAlt`
  (`AltParamChoiceLabeled`, 4 choices `{"Off", "4", "8", "16"}`, default 0) in
  `QlpElements.hh`, added to the global-params block of `Loooop_info.hh`
  right after `QlpCrossfadeAlt` (array grows 85 → 86; `Elem::GridSwitch`
  after `CrossfadeSwitch`). Inserting there adds the last param index and
  shifts no jack/display indices, so existing patches load unchanged.
  Default 0 = Off matches the loader's zero-init of unset params.
  `sync-map-loooop.yaml` gets `GridSwitch: null` (menu-only, no SVG element).

Value → segments mapping is a shared table `{0, 4, 8, 16}`; both hosts call
`engine.setGrid(segments)` from their per-sample update, like
`setOverdub`/`setCrossfade`.

## Engine semantics (`src/loooop/dsp/LoopEngine`)

New state `int grid_` (0 = off) with setter `setGrid(int segments)`; values
< 2 mean off. The grid divides the **frozen loop** (`loopLen_`) into `grid_`
equal segments of `seg = loopLen_ / grid_` samples (fractional segment
lengths are fine — window starts are already fractional doubles).

All quantization happens in `windowBounds()` — the single place window
position/size are computed — so knob, CV, jitter, trigger restarts, and the
seam crossfade's next-window preview all obey the grid consistently:

1. Compute the continuous window length from `size` as today (clamped to
   `[minWinLen_, L]`).
2. Quantize length to whole segments: `m = clamp(round(winLen / seg), 1,
   grid_)`, then grow `m` while `m * seg < minWinLen_` (only reachable when
   the loop is shorter than `grid_` × 1 ms). `winLen = m * seg`.
3. Compute the continuous window start from the jittered centre as today
   (`centre*L − winLen/2`), then snap to the nearest boundary:
   `k = clamp(round(winStart / seg), 0, grid_ − m)`; `winStart = k * seg`.

Because `k + m ≤ grid_`, the window always lies exactly inside the loop — no
post-hoc edge clamping needed in the grid path. Grid off (0) leaves the
existing computation byte-for-byte unchanged.

Jitter interacts musically: the jitter offset moves the *continuous* centre
before snapping, so a jittered head hops between grid slots (matching the
Gloop demo's sample-and-hold-LFO-into-grid patch); offsets smaller than half
a segment quantize away.

## Display

`DisplaySnapshot` gains `std::uint32_t grid` (0 = off), mirrored through a
relaxed atomic written by `setGrid()` on change.

`LoopWaveformRenderer` (shared by VCV and MetaModule) draws the grid only
when `grid ≥ 2` **and** a frozen loop exists (`loopLen > 0` — a growing
initial recording has no meaningful divisions): vertical bars at the N−1
interior boundaries, `x = k * width / N`.

- Color: new `GRID[3] = {0x2E, 0x3A, 0x46}` — brighter than the background,
  dimmer than the waveform tone.
- Bar width: `max(1, width / 300)` px (≈2 screen px on the oversampled VCV
  display, 1 px on the MetaModule screen).
- Z-order: in the waveform region the bars draw **after** the waveform,
  visibly slicing it into chunks (Gloop-style) and still showing against the
  background during silence; in the lane region they draw **before** the
  window/playhead bars so head markers stay prominent.

Cache invalidation: the waveform image is cached against
`waveformRevision()`; both hosts add the snapshot's `grid` to the cache key
(`LoopDisplay.hpp` in VCV, `LoooopCore.cc` on MetaModule) so toggling grid
repaints the waveform region without an audio change.

## Persistence

Grid is an ordinary param on both platforms, so patch save/load and the
MetaModule param system persist it with no extra JSON.

## Testing

- `tests/loooop/test_loop_engine.cpp`: grid snapping observable via
  `displaySnapshot().winStart01/winEnd01` and via played-back sample values —
  size quantizes to whole segments (min 1), position snaps to nearest
  boundary, window stays inside the loop at the extremes, grid off matches
  pre-change behavior, jitter offsets land on boundaries.
- `tests/loooop/test_display_renderer.cpp`: grid bars appear at expected
  columns when grid is set with a frozen loop; absent when grid is off or
  while only recording.
- GUI checks (context menu present, bars look right, MetaModule menu entry)
  go on a user-run checklist at the end of implementation — no agent-driven
  GUI/simulator tests.

## Out of scope

- **Lop**: the single-head sibling gets engine grid support for free, but its
  host wiring (param + menu + info) is deferred until wanted.
- Snapping record start/stop to the grid (Gloop uses a gate input for that;
  Loooop's record trigger already covers externally clocked recording).
- Per-head grid overrides.
