# Codex-branch carry-over — spec

**Date:** 2026-07-09
**Source branch:** `review-fixes-codex` (worktree `.worktrees/review-fixes-codex`, 27 commits from merge-base `3476c9a`)
**Target branch:** `code-review-fixes` (or its successor after merge)
**Status:** PLANNING ONLY — no code has been changed. Companion implementation plan: `2026-07-09-codex-carryover-plan.md`.

## Background

`review-fixes-codex` is a parallel code-review-fixes attempt by another tool. Three independent comparative analyses (looper, MF-20, Particules/beads) classified all 27 commits against the current `code-review-fixes` HEAD (`cc47ad5`). Summary of verdicts:

- **Duplicates / superseded (do not carry):** the K35 resonance clamp `fbf834e` (our resonance-loop clip + D1≤0 fallback is more faithful — the codex clamp destroys self-oscillation by construction); the jitter seam fix `7e865ea` (our pre-rolled `jitterNext`/`commitJitter` design is cleaner and needs no per-setter invalidation); freeze declick `97e5c59` (our `NotifyFreeze` rewrite); arbitrary block sizes `83c26eb` (our chunking); dead-code chores `6959f8a`/most of `32a051c` (we already deleted more).
- **Not applicable:** DSP-memory growth on SR change `08627e4` — only needed because codex made reverb memory sample-rate-scaled; our reverb sizing is fixed, so our "memory requirements are sample-rate-independent" claim remains true. Delay-tap hoists `43bdcf0` — we removed delay mode entirely.
- **Worth carrying over:** everything below.

## What to carry over (by priority)

### A. Cheap, high-value MetaModule wins

1. **SVF denominator cache + dedup** (from `bad157c`). Our `svf.h` still recomputes the full filter *including a per-sample divide* in three near-identical `ProcessLP/HP/BP` methods. Codex caches `denominator_recip_` (recomputed only in `SetFrequency`/`SetQ`/`Init`) and collapses the triplication into one `Tick()`. Our `svf.h` is byte-identical to codex's "before", so it ports with zero drift. Closes the open "SVF triplication" refactor and kills a per-sample division on Cortex-A7.
2. **MetaModule flush-to-zero** (from `cf74abc`). A self-contained header that sets FPSCR FZ+DN bits once on the audio thread (`#if defined(METAMODULE) && defined(__arm__)`; no-op elsewhere). Broader and better than our open "FTZ insurance in reverb feedback" item — kills denormal CPU spikes in every tail. Add a comment that mutating thread-wide FPU state is intentional.
3. **CleanLoFi feedback limiter actually bounds** (from `6754c75`). `Saturation::AsymmetricSoftClip` uses raw `FastTanh`, which diverges like x/9 for large |x| — it does not limit. Swap to `SoftClip` (clamped, exact ±1 for |x|≥3). Same class of fix we already made in auto_gain; this one is in the CleanLoFi *feedback* path, so it's a stability fix.

### B. MF-20 fixes (from `614a6af`, ported onto our rewritten structure — the mechanical diff will not apply)

4. **First `modulate()` on sample 0.** Closes the open "first modulate delayed \~2.5 ms" item: a saved K35 patch audibly starts in OTA mode, and (post-round-2) the 48 kHz g defaults apply until sample \~110. Fix: initialize `_steps` so the first `process()` modulates immediately, and re-arm in `onSampleRateChange`.
5. **Poly channel count from both inputs.** `std::max({1, L.getChannels(), R.getChannels()})`. Closes the open review item.
6. **Drive smoothing — zero-divide design.** Codex smooths per-voice and calls `setDriveCharacterFromSqrt` per sample (one reciprocal per filter per sample). Our port avoids that: drive is a shared param, so use **two module-level smoothers** — `_driveSqrtSlew` (target `sqrt(drive)`, feeds the OTA pre-gain) and `_clipThreshSlew` (target `1/sqrt(drive)`, computed once per modulate block; feeds a new `MF20Filter::setDriveCharacterFromThreshold(t)` that just stores `clipThreshold = t; satSlope = 0.25·t`). Per-sample cost: two one-pole steps + plain stores; no divisions, no sqrt. Closes the open "drive stepped at 2.5 ms" zipper item without walking back the modulate-rate philosophy. `setDriveCharacter(drive)` stays for the tests' filter-level API.

### C. Looper (MetaModule display performance + the dedup refactor)

7. **Shared control-math header** (from `806d935`). Extracts the duplicated knob+CV conditioning from `Loooop.cpp`/`Lop.cpp` into `src/loooop/LooperModuleDSP.hpp` (`speedFromControls`, `speedFromVOct`, `normalizedControl`, `panControl`, `panLeftGain/RightGain`, `normalledStereo`, `dryWet`), with a host-independent test file. Formulas verified byte-equivalent to our inline math. Closes the open "\~80 duplicated lines" refactor (VCV side; the analysis notes the MetaModule cores' `updateHead` still inlines the same math — extend the helpers there too if cheap).
8. **Waveform display cache** (from `4f04486` + `7b3824d` + the LoopEngine hunks of `d17e13d`, as one unit). Splits `LoopWaveformRenderer::render()` into `renderWaveform()` (expensive static envelope) + `renderLanes()` (cheap per-frame playheads); `LoopEngine` gains an atomic `waveformRevision_` bumped (release-store, after mutation) in `reset()`/`clear()`/`writePeak()`; both the VCV widget and the MetaModule cores re-render the waveform only when the revision changes. Benchmarked at −47% (Loooop) / −84% (Lop) host raster time for a frozen loop. **`7b3824d` is non-optional**: its `geometry()` cap fixes a genuine out-of-bounds write on small displays, and it sets the correct release/acquire ordering. From `d17e13d` take *only* the LoopEngine hunks (the rest is codex's reverb work — superseded). Integrates with our `setSampleRate()` without change (retune path leaves peaks intact → correctly no bump; empty path delegates to `reset()` → bumps). Carry the accompanying tests (`test_split_render_matches_composed_render`, tiny-geometry canary test, `test_waveform_revision_tracks_peak_changes_only`); the `tools/benchmark_loooop_renderer.cpp` harness is optional profiling scaffolding.

### D. Smaller correctness/quality items

9. **Grain-timing unification** (from `ed7beb9`). Dedups the two SIZE→duration mapping copies (our open item), replaces the strided overlap-count smoother with an exact block coefficient, hoists a `static const` exponent. **Caution:** `grain_engine.cpp` has drifted heavily on our branch and the smoother change alters audio behavior — port deliberately and listen-test.
10. **`Random::NextFloat()` strictly < 1** (from `7b99f9d`). Our divide-by-2³² rounds up to exactly 1.0f for large states (24-bit mantissa); use the top-24-bits form. (Note: this does NOT fix the separate open "NextGaussian not unit variance" item — codex only fixed the comment.)
11. **Grain-LED decay sample-rate independence** (from `ee606e6`, adapted to our API). Our `DecayGrainLed` uses a fixed per-block factor, so the LED decays twice as fast at 96 kHz. Make the decay a member configured from the sample rate — which also replaces the function-local magic static, closing one of round 2's RT-hygiene minors.
12. **Thread-safe menu "Clear buffer"** (the one nugget from `c078e05`). Our context-menu item calls `processor_.ClearBuffer()` directly on the UI thread, racing the audio thread. Defer via an `std::atomic<bool> clear_requested_` consumed at a block boundary in `process()`. (`onReset` keeps its direct call — Rack holds the engine lock there.)
13. **`pitch_notch_map.hpp` → `inline constexpr`** (the nugget from `32a051c`). Drops per-TU table duplication.

## Explicitly not carried (and why)

| Codex commit | Reason |
|---|---|
| `fbf834e` K35 resonance clamp | Superseded — our loop-clip keeps self-oscillation; the clamp forbids it |
| `7e865ea` jitter seam | Superseded by our pre-rolled jitterNext design |
| `97e5c59` freeze transitions | Superseded by our NotifyFreeze rewrite |
| `83c26eb` arbitrary block sizes | Superseded by our chunked Process |
| `08627e4` DSP memory growth | Only needed under codex's SR-scaled reverb, which we don't have |
| `43bdcf0` delay tap hoists | Delay mode no longer exists on our branch |
| `6959f8a`, bulk of `32a051c` | Already deleted (we swept more) |
| `614a6af`'s exp2 change | Already done (and moved to modulate rate) |
| docs commits | Informational; nothing to import — they confirm our branch is ahead on K35, tan-at-modulate-rate, and dither |

## Verification standard

Same lanes as round 2: `tests/run.sh`, `tests/beads/run.sh`, `make -C vcv -j8`, `cmake --build metamodule/build -j8`. Ported tests come along with their features. Item 9 additionally wants a listening check (grain overlap smoothing changes audio). The display cache (item 8) should get a MetaModule simulator smoke test (`build-simulator` skill) since its one correctness dependency is host-canvas persistence between draw calls.

## Non-goals

- Anything in the "not carried" table.
- SR-scaled reverb (codex's design choice; ours is fixed-size by intent).
- The remaining open items in `code-review-2026-07-08.md` that codex didn't address (batch-the-VCV-path, reverb sleep, kill-oldest-grain, scheduler off-by-one, etc.).
