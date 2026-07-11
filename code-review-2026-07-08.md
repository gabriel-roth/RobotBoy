# RobotBoy code review — July 8, 2026 (open items)

Full-repo review at commit `f222f8a`, followed by three fix rounds and a codex-branch carry-over on branch `code-review-fixes`. Fixed items are removed from this document (or struck through with a "done" note when recently closed); what remains below is still open. **Last updated July 10, 2026**, after merging the Loooop and Particules feature tracks and MF-20 Q6.

**Round history:**
- **Round 1 (July 8):** the five highest-priority findings (`2026-07-08-code-review-fixes-spec.md`). Release note: FEEDBACK on MetaModule was nearly inert and now regenerates properly.
- **Round 2 (July 9):** both remaining majors + priority items (`2026-07-08-code-review-fixes-2-spec.md`): SR-change loop preservation, MF-20 NaN recovery, NaN guards, Particules UX, transcendentals at modulate rate, dead-code sweeps.
- **Codex carry-over (July 9):** SVF cache, MetaModule FTZ, CleanLoFi bound, MF-20 first-modulate/R-poly/drive smoothing, shared looper control math, the waveform display cache (−47%/−84% frozen raster), grain-timing dedup + exact overlap smoothing, SR-independent LED decay, deferred Clear (`2026-07-09-codex-carryover-spec.md`).
- **Round 3 (July 9):** `2026-07-09-round-3-spec.md` — Initialize clears the loop; grain kill-fade/kill-order/mode-change fixes; MF-20 stale-voice reset; fractional seam wrap; the hardening sweep (fmod/isfinite wraps, NaN fences incl. density/pitch at ingestion, reverb Init guard, orphan deletions, hoists); **Particules now runs a 64-sample block on both hosts** (VCV gains 1.3 ms latency at 48 kHz, control behavior now identical to MetaModule); python tests wired into the lane, slug parity, beads NaN-robustness + recovery tests.
- **MF-20 Q6 (July 10):** C1 quadratic knees on the K35 forward clip (the "clip knee aliases" item).
- **Loooop track (July 10, merged):** overdub write modes (Add/Replace/Layer/Decay), punch-in/out write-gain ramps, Catmull-Rom playback interpolation, one-shot end fade, smoothed level/pan/dry-wet, **Grid mode** (Off/4/8/16/32/64, both hosts incl. Löp), panel rework (5-state Overdub button + Grid knob), globals-first param order.
- **Particules track (July 10, merged):** grain-trigger pulse separation, grain-count LED, live input-level readout, scale-aware pitch lock with root selection, dry-follows-gain (post-gain dry tap, menu option), reverb idle sleep, context-menu undo (VCV), beads_dsp adopted as first-party code under `src/particules/dsp/`.

**USER CHECKS:**
- July 9 set: all passed (waveform display cache; grain overlap smoothing; 64-block feel).
- July 10 Loooop track: **pending** — 8 items in the plan checklist (`docs/superpowers/plans/2026-07-10-loooop-track-plan.md`), incl. the MetaModule CPU check.
- July 10 Particules track: **pending** — 8 items in `docs/superpowers/plans/2026-07-10-particules-track-user-checklist.md`.
- Loooop/Löp **panel screenshots in the manual are stale** (panel gained the Overdub button and Grid knob) — retake `screenshots/Loooop.png` / `Lop.png` in VCV.

---

## MF-20

### Bugs

1. **[minor, judgment call] K35's asymmetric input clip creates DC that the LP output passes.**
   `src/mf20/MF20Filter.hpp` (forward-path clip, T_neg = 0.85). Hot input + high drive in K35 puts a DC offset on the LP outputs. The hardware arguably does this too; if unwanted, a one-pole DC blocker on the LP output fixes it.

2. **[nit] Cutoff floor is 20 Hz even under CV** (`MF20Filter.cpp`, `modulate()`) though the core supports 1 Hz — deep negative CV can't close the filter the way an MS-20 can. Intentional? If not, lower the clamp.

### DSP improvements

- ~~Hard clip knee aliases~~ **done (Q6, July 10):** quadratic knees on the K35 forward clip. Remaining optional half: 2× oversampling as a VCV-only "HQ" menu option.

### Low-complexity features

- One HP-stage output jack (the core already computes HP/BP) — only if there's panel room.

### Verified correct (don't re-investigate)

TPT algebra both modes; K35 loop clip with bistable-band fallback; `processG` bit-identical at steady state; NaN recovery, first-modulate, R-poly, drive smoothing (no audio-path sqrt/divide), growth-reset voices (state AND targets); Q6 knee equivalence outside the knee region (maxErr = 0) — all tested.

---

## Loooop / Lop

### Bugs / follow-ups (from the July 10 track's final review — none blocking)

1. **[minor] Stale `osRamp` reset in `triggerOneShot`'s `!playing` path** (`LoopEngine.cpp`) — a leftover retrigger ramp value can survive into the next one-shot arm.
2. **[minor] Mid-pass `setWriteMode` to Decay skips the Decay low-pass seed** — switching write mode during an active overdub pass starts the tone filter from stale state; self-heals next pass.
3. **[pre-existing, backlog] MetaModule cores' `set_samplerate` calls `engine_.reset(sr)` (destroys a live loop)** while VCV's `setSampleRate` preserves it. Align MM with the VCV preserve path.

### DSP improvements

- No anti-aliasing above 1× speed; pragmatic half-measure: average two reads spaced `sp/2` when `|speed| > 1`. Optional.
- Constant-power pan (note the center-unity law is load-bearing for the four-heads-sum-to-unity default).

### Display

- Static-waveform caching landed. Remaining quality note: a short loop uses few of the 4096 peak bins stretched across the display — blocky. Rebin over `loopLen_` once at freeze time.

### Performance

- **[nit] `bumpWaveformRevision()` fires per recorded sample** (`writePeak`) — a `dmb ish` per sample on Cortex-A7 while recording. Batch (bump on peak-bin change, ≤30 ms display lag) if recording CPU ever matters; profile first.

### Low-complexity features

- Rejected as not-low-complexity: overdub phase alignment, undo-overdub, saving the loop with the patch.

### Verified correct (don't re-investigate)

Everything in the old bug list is fixed and tested: SR-change preservation, NaN input guard, window-bounds hoist (bit-identical), shared control math (NaN-suppressing clampSafe = rack::clamp for all inputs), display-cache invalidation completeness, fractional seam wrap, Initialize-clears-loop, same-rate early-out, jitter-decrease clamp, trigger short-circuit fix. The July 10 track closed the whole former DSP-improvement list — overdub write modes incl. sound-on-sound Layer/Decay (Add-mode bit-exactness pinned), punch ramps, Catmull-Rom interpolation (seam values re-derived), one-shot end fade, \~2 ms level/pan/dry-wet smoothing — plus Grid quantization with engine window snapping tested at 32 segments, and VCV/MM param-order parity re-verified after the globals-first reorder.

### Test gaps (logged by the track's final review)

- clear()/reset() during a pending overdub stop-ramp; toggling record off mid-up-ramp.
- Very short/fast one-shot window compounding retrigger ramp-in with end-fade (untested corner).

---

## Particules (wrapper)

### Bugs

1. **[nit] A queued menu "Clear buffer" can be arbitrarily delayed** if the module is bypassed (consumed only in the `BlockReady()` branch). Harmless; drain in `onReset`/on-bypass or accept as documented.

### Refactorings

- `particules_density_control.h` — takes *raw* voltage but names the param `conditioned_density_cv`, and includes three headers it doesn't use. Rename or inline the `* 0.2f`.
- `Particules.cpp` includes `dsp/src/util/control_conditioner.h` — a reach into the dsp library's internals (softened now that the library is first-party; promote to `dsp/include/beads/` if it ever bothers anyone).

### DSP improvements

- ~~Grain trigger pulses merge on retrigger~~ **done (July 10):** one forced low sample between back-to-back pulses.

### Low-complexity features

**All four landed (July 10):** scale-aware pitch lock (with Root submenu), grain-count LED, input-level readout, menu undo (VCV). Nothing left on this list.

### Verified correct (don't re-investigate)

Block-runtime ordering at 1/64; 64-block unification verified to land exactly on MetaModule's existing conditioning values (guard is a no-op for in-use sizes; dry path can't comb — dry is delayed through the same pipeline); LED decay SR-independent incl. the reassignment pin test; FTZ verified in ARM disassembly, re-armed on SR change; allocation failure now WARNs.

---

## Particules DSP library (`src/particules/dsp/`, formerly vendored beads_dsp)

### Bugs / open design questions

1. **[design question, from round 3] Grain-pool overflow silently drops the trigger.** `Process()` breaks on `active_before >= max_active` before `AllocateGrain()`, so the (now-correct) oldest-victim kill fallback is unreachable in production — a trigger arriving with a full pool simply vanishes. Either make overflow steal-and-replace (kill oldest *and* start the new grain) or document drop-on-overflow as intended. The kill-fallback code is correct and test-covered (via a test hook) whenever it becomes reachable.

2. **[nit] `NextGaussian()` isn't unit variance** (`random.h`): CLT sum with σ ≈ 0.577 and hard range ±2 (comment now says so). Only matters if spread math was designed in unit-sigma terms.

3. **[nit, hardening] The reverse-branch `while (gp.position >= buf_size_f)` runs before the position fence** (`grain_engine.cpp`). Currently safe (NaN skips the loop; Inf can't reach it), but the safety is incidental — move the fence above the branch or use fmod to make it robust by construction.

### Refactorings

- **Quality-mode crossfade duplication** — `quality_processor.cpp` (two near-identical blocks).
- `RecordingBuffer` read-path duplication: only `ReadHermiteStereoFast` is used in production; `ReadHermite`/`ReadHermiteStereo`/`ReadLinear` share identical guard/wrap code — demote to test-only or consolidate.

### DSP improvements

- ~~Dry path taps pre-auto-gain input~~ **done (July 10, Q9):** dry now taps post-gain by default ("Dry signal follows input gain", menu-off restores the bit-exact bypass). Documented caveat: with the option on, hot inputs pass the dry path through the input soft limiter.
- ~~Reverb never sleeps~~ **done (July 10, P1):** energy-based sleep once amount sits at 0 and the tail decays; wake is bit-clean.
- **Adaptive interpolation under load** — removed with the DTC path; re-add a load-tier check into `Grain::Process`/`ProcessBlock` only if grain-saturation CPU ever matters.
- **kMidi burst mode clumps** (`grain_scheduler.cpp`): up to 15 burst grains inside one ≤1.3 ms block. Spread across blocks with a pending counter.
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
Nothing user-facing remains. Small items only: the two Loooop follow-up minors (stale `osRamp`, mid-pass Decay LP seed), MM `set_samplerate` loop destruction (§ Loooop follow-ups), fence-ordering hardening (§ dsp lib #3), deferred-clear-on-bypass (§ Particules #1), and the grain-overflow design question (§ dsp lib #1 — needs a decision, not just code).

### Performance improvements
1. Batched waveform-revision bump while recording — profile first. (§ Loooop Performance)

### Refactors
1. dsp-lib duplications: quality-crossfade blocks, RecordingBuffer read-path consolidation. (§ dsp lib Refactorings)
2. `particules_density_control.h` naming/includes; `control_conditioner.h` include path. (§ Particules Refactorings)
3. Build/meta cleanups: clean target, single version source, `sourceUrl`/`manualUrl`, misc nits. (§ Plugin glue)

### New features (the remaining backlog)
1. MF-20 HP output jack (panel room permitting).
2. DSP-quality options: Loooop optional anti-aliasing above 1× speed, constant-power pan, display rebinning; MF-20 HQ 2× oversampling (VCV-only menu option); kMidi burst spreading.

### Other
1. Test gaps: kMidi scheduler, interpolation-tail sync, dedicated pitch-NaN grain test, Loooop SR-change breadth (§ dsp lib test gaps); Loooop stop-ramp corners (§ Loooop test gaps).
2. Pending USER CHECKS (top of this file) — the two July 10 track checklists, plus fresh manual screenshots.
3. Release checklist: `sourceUrl` for VCV Library; `plugin-mm.json` schema check; decide the grain-overflow design question before calling the dsp engine "done."

---

## Suggested next steps

Every open bug from the July 8 review is cleared, and the July 10 tracks landed the entire high-value feature backlog (Loooop write modes/grid/panel, all four Particules features, both perf items). What remains:

1. **User checks first:** the two July 10 checklists gate calling the tracks done; screenshots for the manual while you're in VCV.
2. **Release prep:** `sourceUrl`/`manualUrl`, the glue nits, a decision on grain-overflow behavior. Smallest path to shippable.
3. **Polish tail:** the follow-up minors, refactors, test gaps, and optional DSP-quality features — worthwhile but none urgent.
