# Code-review fixes, round 2 — spec

**Date:** 2026-07-08
**Branch:** `code-review-fixes`
**Source:** `code-review-2026-07-08.md`, "Suggested order of work" items 1–4.

Four work items, in priority order:

1. Loooop/Lop sample-rate-change handling (data-loss bug)
2. MF-20 NaN recovery (robustness bug)
3. NaN input guards (Loooop, Particules) + Particules UX minors
4. MetaModule CPU items + dead-code sweeps

Items 5+ from the review (new features, deeper DSP work) are explicitly out of scope.

---

## Task 1 — Loooop/Lop: sample-rate change must not erase the loop

**Bug:** `Loooop::onSampleRateChange` / `Lop::onSampleRateChange` call `engine.reset(e.sampleRate)`, which zero-fills \~23 MB of buffers and zeroes `loopLen_` — destroying the recorded loop mid-performance, with the same stall risk that crashed a MetaModule patch in `clear()`.

**Fix:** split "retune" from "reallocate" with a new `LoopEngine::setSampleRate(float sr)`:

- Always: update `sampleRate_` and recompute `xfadeSamples_` (\~5 ms at the new rate). The minimum-window length derives from `sampleRate_` inside `windowBounds()`, so it follows automatically.
- If `loopLen_ == 0 && !recording_` (nothing to lose): perform a full `reset(sr, maxSeconds_)` so the buffer is sized correctly for the new rate. This covers the patch-load path (module constructed at a default 48 kHz, then told the real rate before any recording).
- Otherwise (a loop exists or recording is in progress): keep buffers, loop length, write index, heads, and peaks untouched. The loop plays back repitched — far less destructive than erasure. `maxSamples_` stays at its old value, so the maximum loop duration in seconds shifts with the rate; accepted trade-off per the review.

`reset()` stores its `maxSeconds` argument in a new `maxSeconds_` member so `setSampleRate` can re-derive buffer size.

Both modules' `onSampleRateChange` switch from `engine.reset(...)` to `engine.setSampleRate(...)`.

**Tests** (`tests/loooop/test_loop_engine.cpp`):
- Record a loop, `setSampleRate()` to a different rate → `loopLength()` unchanged, buffer content plays back intact.
- `setSampleRate()` while recording → recording continues, content before the change preserved.
- `setSampleRate()` on an empty engine → buffer reallocated for the new rate (observable: record past the old `maxSamples_` ceiling; auto-stop happens at the new ceiling).

## Task 2 — MF-20: NaN/inf recovery in filter state

**Bug:** any non-finite input sample permanently poisons `MF20Filter`'s `s1`/`s2` (state update `s = 2·mid − s` propagates NaN forever). One bad upstream sample silences the module until reinitialization.

**Fix:** per-modulate-block guard (one branch per \~110 samples per voice):

- `MF20Filter` gains `bool stateFinite() const { return std::isfinite(s1) && std::isfinite(s2); }`.
- `VoiceEngine` gains `void sanitize()`: if any of its four filters reports non-finite state, `reset()` all four filters' integrator states. Slew smoothers are left alone — their inputs are clamped params and stay finite, and preserving them avoids a spurious cutoff sweep after recovery.
- `MF20FilterModule::modulate()` calls `eng->sanitize()` for each active voice.

Worst case: up to \~2.5 ms of non-finite output before the next modulate tick, then full recovery. This is the standard practice the review cites for feedback filters.

**Tests** (`tests/mf20/test_mf20.cpp`): feed a NaN sample into `MF20Filter` (both modes) → `stateFinite()` goes false; after `reset()`, finite input yields finite output again. `VoiceEngine::sanitize()` recovery is covered via `engine.hpp` (feed NaN through a voice's filters, call `sanitize()`, assert finite processing resumes).

## Task 3 — NaN input guards + Particules UX minors

### 3a. Loooop/Lop NaN input guard

A NaN on the audio input is recorded into the loop and (via overdub `+=`) persists until Clear. Guard once in `LoopEngine::process(inL, inR, heads)`: non-finite `inL`/`inR` → `0.f`. Covers both modules. Test: record a NaN mid-loop, assert playback is finite.

### 3b. Particules wrapper fixes (all in `src/particules/Particules.cpp` unless noted)

1. **NaN guard on audio input** (feeds a feedback recorder): non-finite `l`/`r` → `0.f` in `process()`. Guard `l` before the `r = in_r_connected ? … : l` fallback.
2. **Freeze light reflects CV-driven freeze:** `lights[FREEZE_BUTTON_LIGHT]` uses `frozen` (button OR gate), not `freeze_button`.
3. **Quality LED colors match the manual** (white/cyan/amber/magenta): `kQualityColors` in `src/plugin.hpp` becomes `{1,1,1}, {0,1,1}, {1,0.5,0}, {1,0,1}`. The manual is the design intent; the code drifted.
4. **Schmitt hysteresis on gate comparators:** replace the plain `> 1.f` comparisons for FREEZE and SEED with `dsp::SchmittTrigger` state (process with low 0.1 V / high 1 V per the VCV standard; use `isHigh()` since both are level-sensitive gates).
5. **Randomize must not hit FREEZE/QUALITY:**
   - QUALITY (momentary) → `configButton<QualityParamQuantity>(QUALITY_PARAM, "Quality")` (sets `randomizeEnabled = false`).
   - FREEZE (latching) → `configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"})` **plus explicit `randomizeEnabled = false`** — note `configSwitch` alone does *not* disable randomization (verified against Rack-SDK `Module.hpp`); the review's suggestion was incomplete.
6. **`onReset` clears engine audio state:** call `processor_.ClearBuffer()` so the 4-second buffer, feedback path, and reverb tail don't survive Initialize. (`stereo_input_` staleness is handled in Task 4 — the field turns out to be dead; see below.)
7. **Clamp `manualGainDb` on JSON load** to [0, 32] (adjacent one-line hardening from the review's minors; included because it touches the same file).

These are module-level (Rack-dependent) changes with no headless test lane; verified by compile + review. The 3a guard is unit-tested.

## Task 4 — MetaModule CPU items + dead-code sweeps

### 4a. MF-20 CPU (the biggest win)

Per voice per sample the module currently pays 2× `std::pow(2,x)`, up to 4× `std::tan`, a `std::sqrt` inside a lambda, and 1–2 RNG calls (\~1.5 M pow+tan/sec at 16 voices).

**Move both transcendentals to modulate rate** (the review's preferred option — slew `g` itself):

- `MF20Filter` gains `Out processG(float in, float g, float res)` / `Out processVCVG(...)` — the existing solve with `g` supplied by the caller. The existing `process(in, cutoffHz, res)` signature remains as a thin wrapper (`g = tan(π·clamp(fc)/fs)`), so every existing test still passes and transitively exercises `processG`.
- `VoiceEngine`'s cutoff smoothers change domain: `lpCutoffSlew`/`hpCutoffSlew` (log₂ Hz) become `lpGSlew`/`hpGSlew` (prewarp gain g). `modulate()` computes, per voice, `gTarget = tan(π·clamp(exp2(cutoffLog), 20, fs·0.498)/fs)` with full-precision `std::exp2`/`std::tan` — now \~110× less often. `processChannel()` advances the g-smoothers per sample and calls `processVCVG`.
- Consequence: no `pow`/`tan` in the audio path at all (the separate approx-exp2 suggestion becomes moot). Smoothing now happens in the g domain instead of log-frequency; same 5 ms time constant, slightly different sweep trajectory — endorsed by the review.
- Sanitize check (Task 2) already lives in `modulate()`, unaffected.

**Smaller MF-20 items:**
- Hoist `std::sqrt(_drive)` out of the per-sample lambda: `_driveSqrt` computed in `modulate()`.
- RNG dither → deterministic alternating constant: replace `1e-6·U(−1,1)` with `±1e-9` flipping sign each sample (member `_dither`). Cheaper, and restores bit-reproducibility between VCV and MetaModule builds.
- Hoist the duplicated `(1+g)²` in the OTA path into one local.

### 4b. Loooop CPU

- Precompute the minimum-window length (`minWinLen_`) in `reset()`/`setSampleRate()` instead of re-deriving `ceil(sampleRate·1ms)` in every `windowBounds()` call (currently 16 redundant ceil+divide chains per sample on a Cortex-A7).
- Compute each head's window bounds **once per sample** in `process()` and pass them down to `readHead()`/`readInterpolated()`/`advanceHead()` (currently derived 4–5×/head/sample). The seam-preview bounds (`jitterNext`) and post-wrap bounds still need their own calls. Pure refactor — identical values flow everywhere, so behavior is bit-identical; the existing seam/jitter tests guard this.

### 4c. Particules / beads_dsp CPU

- `particules_block_runtime.h:73`: `std::pow(0.9999f, BlockSize)` recomputed every `DecayGrainLed()` call (a per-sample `powf` in VCV) → compute once (`static const` local).
- `beads_processor.cpp:233`: `float a = 1.0f - std::pow(1.0f - 0.002f, num_frames)` per `Process` call (per-sample in VCV) → cache last `num_frames` + coefficient in the state.
- `auto_gain.cpp:35`: `std::tanh` in `SoftLimit` → the library's own `FastTanh` (`dsp_utils.h:58`, Padé `x·(27+x²)/(27+9x²)`) — verify the argument range stays within the Padé's accurate domain, and confirm `tests/beads/test_auto_gain.cpp` tolerances hold.
- `auto_gain.cpp:91/106/118`: `FastDbToGain(kMinGainDb)` (a `std::exp2` each call) with a constant argument → hoist to a file-level `static const`.

The beads Catch2 suite must stay green — `test_auto_gain.cpp` and `test_silence.cpp` cover these paths.

### 4d. Dead-code sweeps

**MF-20 (`src/mf20/engine.hpp`, `MF20Filter.cpp`):**
- `EnginePool::processVoice()` — unused ("used by tests" comment is stale); delete.
- `EnginePool::resetAll()` — keep, and make `MF20FilterModule::onReset` call it instead of reimplementing the loop inline.
- `EnginePool::sampleRate` member — stored, never read; delete.
- `VoiceEngine* engines[16]` heap indirection → by-value `VoiceEngine engines[16]`. Removes `new`/`delete`, every null check (constructor, destructor, `onSampleRateChange`, `onReset`, `modulate`, `processChannel`), and improves cache behavior on MetaModule. (\~150 B/voice by value; trivially affordable.)

**Particules wrapper:**
- Dead member `grain_led_` (`Particules.cpp:105`) — delete.
- Empty file `src/particules/Particules.hpp` — delete (confirm no build-file references).
- Redundant `output_index_ = input_index_ + 1;` in `ParticulesBlockRuntime::PushInputSample` — transiently sets an out-of-range value that's never dereferenced; delete the line. Covered by `test_particules_block_runtime.cpp`.

**beads_dsp (vendored but actively developed in-repo — 5 substantive local commits since vendoring, so deletion creates no upstream-sync burden):**
- `EqualPowerCrossfade` (`dsp_utils.h:28`) and `FastPowUnit` (`dsp_utils.h:95`) — no callers anywhere; delete.
- The entire `src/wavetable/` module — included only by its own `.cpp`, never instantiated; delete the directory and its explicit entry at `metamodule/CMakeLists.txt:39` (the VCV Makefile wildcard and the tests' `GLOB_RECURSE` pick up the removal automatically).
- The DTC path — `GrainDTCCache` (`grain.h:16`), `Grain::ProcessBlockCached` (`grain.h:204`), the 5-arg `BeadsProcessor::Init` (`beads.h:36`, `beads_processor.cpp:39`; the 3-arg form delegates with nulls), `GrainEngine::SetDTCCache`/`dtc_cache_`, and the `if (dtc_cache_)` branch at `grain_engine.cpp:264`. `dtc_cache_` is provably always null at runtime (every in-repo caller uses the 3-arg `Init`); zero test coverage. Collapse `Init` to the 3-arg form and delete the rest. With `ProcessBlockCached` gone, `use_linear_`/`set_use_linear` (its only reader) become write-only dead code — delete them too (`grain.h:261,266`, `grain.cpp:11`, `grain_engine.cpp:241`); `render_load_tier_` itself stays (still read for `normalization_stride` at `grain_engine.cpp:293`). If DTC or adaptive interpolation is wanted later, it's in git history.
- `BeadsParameters::seed_connected` and `stereo_input` (`include/beads/parameters.h:42,54`) — set by the host, never read by the engine; delete the fields and their assignments in `Particules.cpp`. This also makes the wrapper's `stereo_input_` member dead (it only fed `params_.stereo_input`) — delete it too, which subsumes the review's "reset `stereo_input_` in onReset" note.
- `Saturation::Process` is test-only but *is* used by tests — keep.

**Verification for Task 4:** all three test lanes green (`tests/run.sh`, `tests/beads/run.sh`), plus a clean VCV build (`make -C vcv`, RACK_DIR=\~/Dev/Rack-SDK) and MetaModule build (`metamodule/` CMake) to prove the dead-code removal didn't break either target's source lists.

---

## Non-goals

- Loooop/Lop `process()`/menu deduplication, cubic interpolation, overdub ramps, one-shot fades, pan law (review refactorings/DSP items outside items 1–4).
- MF-20 minors #2–#7 (poly R-channel count, first-modulate delay, stale-voice reset, drive smoothing, K35 DC, cutoff floor).
- Wiring `use_linear` into the direct grain path; other beads DSP improvements.
- New features (overdub feedback, pitch-lock scales, HP output).

## Commit strategy

One commit per task (Tasks 1–3), and Task 4 split into: MF-20 perf+dead-code, Loooop perf, Particules/beads perf+dead-code. All on `code-review-fixes`.
