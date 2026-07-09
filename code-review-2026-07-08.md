# RobotBoy code review — July 8, 2026 (open items)

Full-repo review at commit `f222f8a`, followed by three fix rounds and a codex-branch carry-over on branch `code-review-fixes`. Fixed items are removed from this document; what remains below is still open.

**Round history:**
- **Round 1 (July 8):** the five highest-priority findings (`2026-07-08-code-review-fixes-spec.md`). Release note: FEEDBACK on MetaModule was nearly inert and now regenerates properly.
- **Round 2 (July 9):** both remaining majors + priority items (`2026-07-08-code-review-fixes-2-spec.md`): SR-change loop preservation, MF-20 NaN recovery, NaN guards, Particules UX, transcendentals at modulate rate, dead-code sweeps.
- **Codex carry-over (July 9):** SVF cache, MetaModule FTZ, CleanLoFi bound, MF-20 first-modulate/R-poly/drive smoothing, shared looper control math, the waveform display cache (−47%/−84% frozen raster), grain-timing dedup + exact overlap smoothing, SR-independent LED decay, deferred Clear (`2026-07-09-codex-carryover-spec.md`).
- **Round 3 (July 9):** `2026-07-09-round-3-spec.md` — Initialize clears the loop; grain kill-fade/kill-order/mode-change fixes; MF-20 stale-voice reset; fractional seam wrap; the hardening sweep (fmod/isfinite wraps, NaN fences incl. density/pitch at ingestion, reverb Init guard, orphan deletions, hoists); **Particules now runs a 64-sample block on both hosts** (VCV gains 1.3 ms latency at 48 kHz, control behavior now identical to MetaModule); python tests wired into the lane, slug parity, beads NaN-robustness + recovery tests.

**USER CHECKS: all passed (July 9).**
1. **MetaModule simulator — waveform display cache:** waveform persists correctly across frames and module switches (canvas-persistence assumption holds).
2. **VCV listening — grain overlap smoothing:** no pumping/stepping in a dense cloud under a Density sweep.
3. **VCV feel — Particules 64-block batching:** passthrough and freeze responsiveness confirmed fine.

---

## MF-20

### Bugs

1. **[minor, judgment call] K35's asymmetric input clip creates DC that the LP output passes.**
   `src/mf20/MF20Filter.hpp` (forward-path clip, T_neg = 0.85). Hot input + high drive in K35 puts a DC offset on the LP outputs. The hardware arguably does this too; if unwanted, a one-pole DC blocker on the LP output fixes it.

2. **[nit] Cutoff floor is 20 Hz even under CV** (`MF20Filter.cpp`, `modulate()`) though the core supports 1 Hz — deep negative CV can't close the filter the way an MS-20 can. Intentional? If not, lower the clamp.

### DSP improvements

- **Hard clip knee aliases** at high drive/cutoff. Rounding the knee with a small quadratic transition region kills the worst high-order harmonics; 2× oversampling could be a VCV-only "HQ" menu option.

### Low-complexity features

- One HP-stage output jack (the core already computes HP/BP) — only if there's panel room.

### Verified correct (don't re-investigate)

TPT algebra both modes; K35 loop clip with bistable-band fallback; `processG` bit-identical at steady state; NaN recovery, first-modulate, R-poly, drive smoothing (no audio-path sqrt/divide), growth-reset voices (state AND targets) — all tested.

---

## Loooop / Lop

### DSP improvements

- **Overdub punch-out records a permanent step into the loop** (`LoopEngine.cpp`, overdub write path): input sums at full gain from the first sample and stops dead at `toggleRecord()`; clicks on every subsequent pass. **Fix:** ramp an overdub-input gain over `xfadeSamples_` at start/stop (defer `recording_ = false` by the ramp length).
- **Linear → 4-point cubic (Hermite) interpolation** (`readInterpolated`/`readRaw`) — the single biggest fidelity win available at fractional speeds/V-oct melodies; still modest on MetaModule.
- **Head level/pan/dry-wet applied unsmoothed** — a square LFO into Level CV produces hard steps; a 1–5 ms one-pole each fixes it.
- **One-shots end with a hard cut** — a gain-ramp fade-out over the last `xfadeSamples_` de-clicks the re-slicer patch.
- No anti-aliasing above 1× speed; pragmatic half-measure: average two reads spaced `sp/2` when `|speed| > 1`. Optional.
- Constant-power pan (note the center-unity law is load-bearing for the four-heads-sum-to-unity default).

### Display

- Static-waveform caching landed. Remaining quality note: a short loop uses few of the 4096 peak bins stretched across the display — blocky. Rebin over `loopLen_` once at freeze time.

### Performance

- **[nit] `bumpWaveformRevision()` fires per recorded sample** (`writePeak`) — a `dmb ish` per sample on Cortex-A7 while recording. Batch (bump on peak-bin change, ≤30 ms display lag) if recording CPU ever matters; profile first.

### Low-complexity features

- **Overdub feedback/decay (sound-on-sound):** `buf = buf * feedback + in` — the looper feature users will actually look for; also bounds the unbounded overdub sum.
- Rejected as not-low-complexity: overdub phase alignment, undo-overdub, saving the loop with the patch.

### Verified correct (don't re-investigate)

Everything in the bug list is now fixed and tested: SR-change preservation, NaN input guard, window-bounds hoist (bit-identical), shared control math (NaN-suppressing clampSafe = rack::clamp for all inputs), display-cache invalidation completeness, fractional seam wrap, Initialize-clears-loop, same-rate early-out, jitter-decrease clamp, trigger short-circuit fix.

---

## Particules (wrapper)

### Bugs

1. **[nit] A queued menu "Clear buffer" can be arbitrarily delayed** if the module is bypassed (consumed only in the `BlockReady()` branch). Harmless; drain in `onReset`/on-bypass or accept as documented.

### Refactorings

- `particules_density_control.h` — takes *raw* voltage but names the param `conditioned_density_cv`, and includes three headers it doesn't use. Rename or inline the `* 0.2f`.
- `Particules.cpp` includes a vendored-library internal (`src/util/control_conditioner.h`); promote it to `include/beads/`.

### DSP improvements

- **Grain trigger pulses merge on retrigger** (`particules_block_runtime.h`) — two grains < 1 ms apart produce one continuous high. Force one low sample between pulses.

### Low-complexity features

- **Scale-aware pitch lock** (engine hooks exist). **Grain-count-aware LED** (`ActiveGrainCount()` unused). **Input level readout** in the gain menu (`InputLevel()` unused). **Undo for context-menu options** (`history::ModuleChange`, VCV-only).

### Verified correct (don't re-investigate)

Block-runtime ordering at 1/64; 64-block unification verified to land exactly on MetaModule's existing conditioning values (guard is a no-op for in-use sizes; dry path can't comb — dry is delayed through the same pipeline); LED decay SR-independent incl. the reassignment pin test; FTZ verified in ARM disassembly, re-armed on SR change; allocation failure now WARNs.

---

## beads_dsp (vendored library)

### Bugs / open design questions

1. **[design question, from round 3] Grain-pool overflow silently drops the trigger.** `Process()` breaks on `active_before >= max_active` before `AllocateGrain()`, so the (now-correct) oldest-victim kill fallback is unreachable in production — a trigger arriving with a full pool simply vanishes. Either make overflow steal-and-replace (kill oldest *and* start the new grain) or document drop-on-overflow as intended. The kill-fallback code is correct and test-covered (via a test hook) whenever it becomes reachable.

2. **[nit] `NextGaussian()` isn't unit variance** (`random.h`): CLT sum with σ ≈ 0.577 and hard range ±2 (comment now says so). Only matters if spread math was designed in unit-sigma terms.

3. **[nit, hardening] The reverse-branch `while (gp.position >= buf_size_f)` runs before the position fence** (`grain_engine.cpp`). Currently safe (NaN skips the loop; Inf can't reach it), but the safety is incidental — move the fence above the branch or use fmod to make it robust by construction.

### Refactorings

- **Quality-mode crossfade duplication** — `quality_processor.cpp` (two near-identical blocks).
- `RecordingBuffer` read-path duplication: only `ReadHermiteStereoFast` is used in production; `ReadHermite`/`ReadHermiteStereo`/`ReadLinear` share identical guard/wrap code — demote to test-only or consolidate.

### DSP improvements

- **Dry path taps pre-auto-gain input** (deliberate so DRY/WET = 0 equals bypass) — but with auto-gain boosting +32 dB, mid-knob mixes have a big dry/wet level mismatch; hardware Beads crossfades post-gain. Consider tapping post-gain, or document the trade-off.
- **Adaptive interpolation under load** — removed with the DTC path; re-add a load-tier check into `Grain::Process`/`ProcessBlock` only if grain-saturation CPU ever matters.
- **kMidi burst mode clumps** (`grain_scheduler.cpp`): up to 15 burst grains inside one ≤1.3 ms block. Spread across blocks with a pending counter.
- **Reverb never sleeps** (`reverb.cpp` gates input at `amount_ == 0` but the 12-delay-line tank runs forever). Add an energy-based sleep once the tail decays (MetaModule CPU).
- Grain-rate cap at 80 Hz is deliberate; revisit only for "faithful recreation."
- The overlap-normalization "slow fall" coefficient is 0.2/sample ≈ 5-sample time constant — not slow; if pumping is ever heard, it wants \~0.001. (Block application is now exact; the coefficient value is unchanged.)

### Test coverage gaps

- **Interpolation-tail sync** — no test writes at `write_head_ < kInterpolationTail` then reads fractionally across the `size_` boundary.
- **kMidi scheduler mode** still untested (kClocked gained its first test in round 3).
- **A dedicated pitch-NaN grain test** mirroring the time-NaN one in `test_grain.cpp` (the pitch_ratio fence is currently exercised only transitively via the NaN-robustness test).
- **Loooop SR-change coverage breadth** (multi-head, non-default speed, redundant same-rate call).

### Verified correct (don't re-investigate)

All prior verified items stand. Round 3 added: fmod wrap equivalence (incl. negative-position edge), NaN fences at ingestion for feedback/dry_wet/reverb/density (+ pitch_ratio and position in ComputeGrainParams) with liveness AND recovery pinned by test (grains fire during NaN CVs and after they clear), reverb Init guard never rejects a real buffer, kill-fade sequence pinned (0.75/0.5/0.25/0.0), spawn-order victim selection, mode-change counter reset, feedback=0 exact-zero baseline.

---

## Plugin glue, build, metadata, tests

Remaining items, all minor:

1. **`plugin.json` lacks `sourceUrl`/`manualUrl`** — required (`sourceUrl`) for VCV Library submission.
2. **Object files escape `build/` into `vcv/src/`** — extend the clean target.
3. **Version stated twice** (plugin.json and CMake `project()`) — drop one or derive it.
4. Nits: stale test-count comment in `tests/beads/CMakeLists.txt`; `-std=c++20` applied to all languages in `metamodule/CMakeLists.txt`; Makefile `cp` sync swallows failures; empty `presets/` not in `DISTRIBUTABLES`; `plugin-mm.json` schema keys unverified against 4ms.

---

## Still to do, by category

### Fixes
Nothing user-facing remains. Hardening nits only: fence-ordering hardening (§ beads #3), deferred-clear-on-bypass (§ Particules #1), and the grain-overflow design question (§ beads #1 — needs a decision, not just code).

### Performance improvements
1. Reverb energy-based sleep when idle (MetaModule CPU). (§ beads DSP)
2. Batched waveform-revision bump while recording — profile first. (§ Loooop Performance)

### Refactors
1. beads duplications: quality-crossfade blocks, RecordingBuffer read-path consolidation. (§ beads Refactorings)
2. `particules_density_control.h` naming/includes; promote `control_conditioner.h`. (§ Particules Refactorings)
3. Build/meta cleanups: clean target, single version source, `sourceUrl`/`manualUrl`, misc nits. (§ Plugin glue)

### New features (the remaining backlog — none started)
1. **Loooop overdub feedback/decay (sound-on-sound)** — highest musical value.
2. **Particules scale-aware pitch lock** (engine hooks exist).
3. MF-20 HP output jack (panel room permitting).
4. Particules: grain-count LED, input-level readout, menu undo; grain-trigger pulse separation.
5. DSP-quality options: Loooop cubic interpolation, overdub ramps, one-shot fades, smoothed level/pan, optional anti-aliasing, display rebinning; MF-20 clip-knee rounding / HQ oversampling; beads post-gain dry tap, kMidi burst spreading.

### Other
1. Test gaps: kMidi scheduler, interpolation-tail sync, dedicated pitch-NaN grain test, Loooop SR-change breadth. (§ beads test gaps)
2. Pending USER CHECKS (top of this file) — three manual checks.
3. Release checklist: `sourceUrl` for VCV Library; `plugin-mm.json` schema check; decide the grain-overflow design question before calling the beads engine "done."

---

## Suggested next steps

Rounds 1–3 have cleared every open bug. What remains splits three ways — pick a direction:

1. **Release prep:** the three USER CHECKS, `sourceUrl`/`manualUrl`, the glue nits, and a decision on grain-overflow behavior. Smallest path to shippable.
2. **Features:** the "New features" list above, roughly in listed order of value (overdub feedback first — it also bounds the overdub sum, so it has a robustness angle too).
3. **Polish/perf tail:** reverb sleep, the DSP-quality options, remaining refactors and test gaps — worthwhile but none urgent.
