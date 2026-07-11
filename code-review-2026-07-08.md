# RobotBoy code review — July 8, 2026 (open items)

Full-repo review at commit `f222f8a`, followed by three fix rounds, a codex-branch carry-over, and **round 4** (this update). Fixed items are removed from this document (or struck through with a "done" note when recently closed); what remains below is still open. **Last updated July 11, 2026**, after round 4 (small fixes / test gaps / refactors) completed and both work streams passed their final whole-branch reviews.

**In flight (unmerged branches):**
- `loooop-track` (worktree): Loooop/Löp menu polish (Randomize opt-out, dimmed armed one-shot heads, reorder/rename) + the user's Löp Overdub/Grid panel work **and** round-4 Stream A (commits 7d63539, 42dd120, bf28267, 74b80a1 — the two Loooop follow-up minors, MM `set_samplerate` loop preservation, stop-ramp corner tests). Final-reviewed ready-to-merge; gates on the pending USER CHECKS.
- `review-round-4` (worktree, 9 commits off `main`): round-4 Stream B — grain steal-and-replace, fence-ordering hardening, three new dsp tests, two behavior-preserving refactors, wrapper hygiene, and the build/metadata cleanups. Final-reviewed ready-to-merge; no pending checks.
- `loooop-display-overlay-wontmerge` (5 commits): full-height waveform with translucent per-head lane overlay — **rejected July 11** (user didn't like it); branch kept for reference, will not merge.

**Round history:**
- **Round 1 (July 8):** the five highest-priority findings (`2026-07-08-code-review-fixes-spec.md`). Release note: FEEDBACK on MetaModule was nearly inert and now regenerates properly.
- **Round 2 (July 9):** both remaining majors + priority items (`2026-07-08-code-review-fixes-2-spec.md`): SR-change loop preservation, MF-20 NaN recovery, NaN guards, Particules UX, transcendentals at modulate rate, dead-code sweeps.
- **Codex carry-over (July 9):** SVF cache, MetaModule FTZ, CleanLoFi bound, MF-20 first-modulate/R-poly/drive smoothing, shared looper control math, the waveform display cache (−47%/−84% frozen raster), grain-timing dedup + exact overlap smoothing, SR-independent LED decay, deferred Clear (`2026-07-09-codex-carryover-spec.md`).
- **Round 3 (July 9):** `2026-07-09-round-3-spec.md` — Initialize clears the loop; grain kill-fade/kill-order/mode-change fixes; MF-20 stale-voice reset; fractional seam wrap; the hardening sweep (fmod/isfinite wraps, NaN fences incl. density/pitch at ingestion, reverb Init guard, orphan deletions, hoists); **Particules now runs a 64-sample block on both hosts** (VCV gains 1.3 ms latency at 48 kHz, control behavior now identical to MetaModule); python tests wired into the lane, slug parity, beads NaN-robustness + recovery tests.
- **MF-20 Q6 (July 10):** C1 quadratic knees on the K35 forward clip (the "clip knee aliases" item).
- **Loooop track (July 10, merged):** overdub write modes (Add/Replace/Layer/Decay), punch-in/out write-gain ramps, Catmull-Rom playback interpolation, one-shot end fade, smoothed level/pan/dry-wet, **Grid mode** (Off/4/8/16/32/64, both hosts incl. Löp), panel rework (5-state Overdub button + Grid knob), globals-first param order.
- **Particules track (July 10, merged):** grain-trigger pulse separation, grain-count LED, live input-level readout, scale-aware pitch lock with root selection, dry-follows-gain (post-gain dry tap, menu option), reverb idle sleep, context-menu undo (VCV), beads_dsp adopted as first-party code under `src/particules/dsp/`.
- **Menu rework (July 10–11, merged):** commands-first Loooop menu, color playhead names, per-head "Exclude from Grid" (engine + VCV + MetaModule alt-params), one-shot checkmark submenu replacing Trigger, manual section.
- **Rename (July 11):** all Beads naming removed from code — engine is now `particules_dsp` (tests moved to `tests/particules_dsp/`, public headers under `dsp/include/particules_dsp/`). A new MF-20 panel spec landed at `panel-specs/mf20filter.yaml`.

**USER CHECKS:**
- July 9 set: all passed (waveform display cache; grain overlap smoothing; 64-block feel).
- July 10 Loooop track: **pending** — 8 items in the plan checklist (`docs/superpowers/plans/2026-07-10-loooop-track-plan.md`), incl. the MetaModule CPU check.
- July 10 Particules track: **pending** — 8 items in `docs/superpowers/plans/2026-07-10-particules-track-user-checklist.md`.
- July 10–11 menu rework: **pending** — 4 items in `docs/superpowers/plans/2026-07-10-loooop-menu-rework-user-checklist.md`.
- Loooop/Löp **panel screenshots in the manual are stale** (panel gained the Overdub button and Grid knob; last taken July 8) — retake `screenshots/Loooop.png` / `Lop.png` in VCV.

---

## MF-20

### Bugs

1. **[minor, judgment call] K35's asymmetric input clip creates DC that the LP output passes.**
   `src/mf20/MF20Filter.hpp` (forward-path clip, T_neg = 0.85). Hot input + high drive in K35 puts a DC offset on the LP outputs. The hardware arguably does this too; if unwanted, a one-pole DC blocker on the LP output fixes it.

2. **[nit] Cutoff floor is 20 Hz even under CV** (`MF20Filter.cpp`, `modulate()`) though the core supports 1 Hz — deep negative CV can't close the filter the way an MS-20 can. Intentional? If not, lower the clamp.

### DSP improvements

- ~~Hard clip knee aliases~~ **done (Q6, July 10):** quadratic knees on the K35 forward clip. Remaining optional half: 2× oversampling as a VCV-only "HQ" menu option.

### Low-complexity features

- ~~One HP-stage output jack~~ **decided against (July 11)** — not doing it.

### Verified correct (don't re-investigate)

TPT algebra both modes; K35 loop clip with bistable-band fallback; `processG` bit-identical at steady state; NaN recovery, first-modulate, R-poly, drive smoothing (no audio-path sqrt/divide), growth-reset voices (state AND targets); Q6 knee equivalence outside the knee region (maxErr = 0) — all tested.

---

## Loooop / Lop

### Bugs / follow-ups (from the July 10 track's final review — none blocking)

1. ~~Stale `osRamp` reset in `triggerOneShot`'s `!playing` path~~ **done (round 4, July 11 — `loooop-track` commit 7d63539):** the `!playing` branch now snaps `osRamp` back to 1.0; the `playing` branch's deliberate mid-fade hold is preserved. Test-covered (stale-ramp reference comparison + short/fast one-shot window).
2. ~~Mid-pass `setWriteMode` to Decay skips the Decay low-pass seed~~ **done (round 4, July 11 — commit 42dd120):** `setWriteMode` is now out-of-line and seeds `decayLpL_/R_` from `bufL_/R_[writeIdx_]` when switching into Decay during an active overdub pass, matching the pass-start seed.
3. ~~MetaModule cores' `set_samplerate` calls `engine_.reset(sr)` (destroys a live loop)~~ **done (round 4, July 11 — commit bf28267):** both `LoooopCore`/`LopCore` now call `engine_.setSampleRate(sr)` (preserves the loop, retunes coefficients), matching VCV `onSampleRateChange`.

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

- ~~clear()/reset() during a pending overdub stop-ramp; toggling record off mid-up-ramp.~~ **done (round 4, July 11 — commit 74b80a1):** three corner tests pin all three (no defects found; state machine traced by review).
- ~~Very short/fast one-shot window compounding retrigger ramp-in with end-fade (untested corner).~~ **done (round 4, July 11 — commit 7d63539, Test B):** finite + no-hard-snap under rapid retrigger.

---

## Particules (wrapper)

### Bugs

1. ~~A queued menu "Clear buffer" can be arbitrarily delayed~~ **done (round 4, July 11 — `review-round-4` commit 090a143):** `onReset` now drains `clear_requested_` (stores false next to the direct `ClearBuffer()`), so a stale queued clear can't fire after an intentional reset. The bypass half is accepted as documented: VCV stops calling `process()` while bypassed, the `exchange`-guarded flag survives, and the clear lands at the first block after un-bypass.

### Refactorings

- ~~`particules_density_control.h` — takes *raw* voltage but names the param `conditioned_density_cv`, and includes three headers it doesn't use.~~ **done (round 4, July 11 — commit 090a143):** param renamed `density_cv_volts`, comment says "raw ±5 V", three unused includes dropped; `* 0.2f` math unchanged.
- ~~`Particules.cpp` includes `dsp/src/util/control_conditioner.h` — a reach into the dsp library's internals.~~ **done (round 4, July 11 — commit 090a143):** `git mv`'d to `dsp/include/particules_dsp/control_conditioner.h` (public include dir) as a pure rename; all includers now use `<particules_dsp/control_conditioner.h>`.

### DSP improvements

- ~~Grain trigger pulses merge on retrigger~~ **done (July 10):** one forced low sample between back-to-back pulses.

### Low-complexity features

**All four landed (July 10):** scale-aware pitch lock (with Root submenu), grain-count LED, input-level readout, menu undo (VCV). Nothing left on this list.

### Verified correct (don't re-investigate)

Block-runtime ordering at 1/64; 64-block unification verified to land exactly on MetaModule's existing conditioning values (guard is a no-op for in-use sizes; dry path can't comb — dry is delayed through the same pipeline); LED decay SR-independent incl. the reassignment pin test; FTZ verified in ARM disassembly, re-armed on SR change; allocation failure now WARNs.

---

## Particules DSP library (`src/particules/dsp/`, formerly vendored beads_dsp, renamed `particules_dsp` July 11)

### Bugs / open design questions

1. ~~Grain-pool overflow silently drops the trigger.~~ **done (round 4, July 11 — `review-round-4` commits 70c8fa6 + d57c6e3):** `Process()` now steal-and-replaces at saturation. CPU-cap-with-headroom: mark the oldest grain for the click-free pending-kill and start the new grain in a free slot (active count transiently over the *dynamic* cap by 1, ≤36 samples; never over the fixed 30-slot pool). Genuinely-full pool: hard-replace the oldest slot. New `FindOldestActiveGrain()` helper (excludes pending-kill grains); the `ForceAllocateGrainForTest` hook keeps its observable semantics. Both steal paths RED-validated; `Grain::Start()` already fully resets kill state so a reused slot inherits nothing. (Minor, plan-mandated: a multi-trigger block at CPU-cap can exceed the *cap* by +N until victims' fades finish — bounded/transient, never over the pool.)

2. **[nit] `NextGaussian()` isn't unit variance** (`random.h`): CLT sum with σ ≈ 0.577 and hard range ±2 (comment now says so). Only matters if spread math was designed in unit-sigma terms.

3. ~~The reverse-branch `while (gp.position >= buf_size_f)` runs before the position fence~~ **done (round 4, July 11 — commit e26c009):** the `isfinite(gp.position)` fence now sits above the reverse-branch wrap loop, robust by construction (+Inf can no longer spin the loop). Test-covered with a reverse-playback NaN case (reverse is driven by `size < kSizeBoundary`, not pitch sign).

### Refactorings

- ~~Quality-mode crossfade duplication~~ **done (round 4, July 11 — commit 24f099c):** extracted a shared `ApplyModeXfade(int& counter, input, result)` helper; arithmetic verified token-identical to both original blocks (behavior-preserving, existing tests pass unchanged).
- ~~`RecordingBuffer` read-path duplication~~ **done (round 4, July 11 — commit 43c7bda):** extracted a shared `ResolveReadPosition` guard/wrap prologue for the three test-only readers (tap math per-reader preserved); deleted the dead `ReadLinearStereoFast`; the hot path `ReadHermiteStereoFast` is untouched. (Minor left: a now-dead `#include <cmath>` in `recording_buffer.cpp` — the one in `recording_buffer.h` is correct.)

### DSP improvements

- ~~Dry path taps pre-auto-gain input~~ **done (July 10, Q9):** dry now taps post-gain by default ("Dry signal follows input gain", menu-off restores the bit-exact bypass). Documented caveat: with the option on, hot inputs pass the dry path through the input soft limiter.
- ~~Reverb never sleeps~~ **done (July 10, P1):** energy-based sleep once amount sits at 0 and the tail decays; wake is bit-clean.
- **Adaptive interpolation under load** — removed with the DTC path; re-add a load-tier check into `Grain::Process`/`ProcessBlock` only if grain-saturation CPU ever matters.
- Grain-rate cap at 80 Hz is deliberate; revisit only for "faithful recreation."
- The overlap-normalization "slow fall" coefficient is 0.2/sample ≈ 5-sample time constant — not slow; if pumping is ever heard, it wants \~0.001. (Block application is now exact; the coefficient value is unchanged.)

### Test coverage gaps

- ~~Interpolation-tail sync~~ **done (round 4, July 11 — commit fd626af):** test writes past the wrap (`write_head_ < kInterpolationTail`) then reads fractionally across the `size_` boundary via the tail mirror; fast + out-of-line readers asserted to agree. Prove-it-bites confirmed (RED with the mirror removed).
- ~~A dedicated pitch-NaN grain test~~ **done (round 4, July 11 — commit cd7271f):** mirrors the time-NaN test with a NaN pitch CV (liveness + recovery); prove-it-bites confirmed the pitch_ratio fence guards it.
- ~~Loooop SR-change coverage breadth~~ **done (round 4, July 11 — commit bf28267):** multi-head non-default-speed retune + redundant same-rate no-op tests added.

### Verified correct (don't re-investigate)

All prior verified items stand. Round 3 added: fmod wrap equivalence (incl. negative-position edge), NaN fences at ingestion for feedback/dry_wet/reverb/density (+ pitch_ratio and position in ComputeGrainParams) with liveness AND recovery pinned by test (grains fire during NaN CVs and after they clear), reverb Init guard never rejects a real buffer, kill-fade sequence pinned (0.75/0.5/0.25/0.0), spawn-order victim selection, mode-change counter reset, feedback=0 exact-zero baseline.

---

## Plugin glue, build, metadata, tests

All done in round 4 (July 11 — `review-round-4` commit f4f684a):

1. ~~`plugin.json` lacks `sourceUrl`/`manualUrl`~~ **done:** both added at top level (JSON validated).
2. ~~Object files escape `build/` into `vcv/src/`~~ **done:** added a `clean-escaped-objects` rule (scoped to `vcv/src`, live sources at repo root are safe).
3. ~~Version stated twice~~ **done:** `metamodule/CMakeLists.txt` now derives the version from `plugin.json` via `file(READ)` + `string(JSON …)`; single source of truth.
4. ~~Nits~~ **done:** `-std=c++20` scoped to `$<$<COMPILE_LANGUAGE:CXX>:…>`; the Makefile `cp` sync now fails loudly via `$(error …)`; empty `presets/` deleted; **`plugin-mm.json` schema keys verified against the MetaModule SDK docs** (`metamodule-plugin-sdk/docs/plugin-mm-json.md`) — all keys correct, file left untouched; `tests/beads/` did not exist in the `review-round-4` branch (no-op). Verified with a real MM ARM-toolchain configure (`CMAKE_PROJECT_VERSION=2.0.1` → `RobotBoy.mmplugin`) plus the dsp suite, Lane 1, and the VCV dylib build all green.

---

## Still to do, by category

### Fixes
~~grain-overflow steal-and-replace; the two Loooop follow-up minors; MM `set_samplerate` loop destruction; fence-ordering hardening; deferred-clear-on-bypass~~ **all done in round 4 (July 11)** — see the struck-through items in their sections. Nothing left here.

### Performance improvements
1. Batched waveform-revision bump while recording — profile first. (§ Loooop Performance)

### Refactors
~~dsp-lib duplications; `particules_density_control.h` naming/includes + `control_conditioner.h` include path; build/meta cleanups~~ **all done in round 4 (July 11).** Nothing left here.

### New features (the remaining backlog)
1. DSP-quality options: Loooop optional anti-aliasing above 1× speed, constant-power pan, display rebinning; MF-20 HQ 2× oversampling (VCV-only menu option).

### Other
1. ~~Test gaps: interpolation-tail sync, dedicated pitch-NaN grain test, Loooop SR-change breadth; Loooop stop-ramp corners~~ **all done in round 4 (July 11).**
2. Pending USER CHECKS (top of this file) — the three pending checklists (Loooop track, Particules track, menu rework), plus fresh manual screenshots.
3. Merge `loooop-track` (menu polish + the Randomize opt-out bug fix + the round-4 Loooop fixes, all sitting unmerged) and `review-round-4` (the round-4 dsp/wrapper/build work).
4. ~~Release checklist: `sourceUrl`; `plugin-mm.json` schema check; grain-overflow steal-and-replace~~ **all done in round 4 (July 11).** The dsp engine's decided-July-11 item is implemented.

---

## Suggested next steps

Every open bug from the July 8 review is cleared, the July 10 tracks landed the entire high-value feature backlog, the July 10–11 menu rework landed on top, and **round 4 (July 11) closed all the remaining fixes, refactors, build/meta cleanups, and test gaps** — the dsp engine's decided-July-11 steal-and-replace is implemented. What remains:

1. **Land the in-flight branches:** merge `loooop-track` (Randomize opt-out fix + Löp panel work + round-4 Stream A) and `review-round-4` (round-4 Stream B). Both are final-reviewed ready-to-merge. The display-overlay branch was rejected and renamed `loooop-display-overlay-wontmerge`.
2. **User checks:** the three pending checklists gate calling the tracks done; screenshots for the manual while you're in VCV.
3. **Optional polish tail:** the optional DSP-quality features (Loooop anti-aliasing above 1× speed, constant-power pan, display rebinning, MF-20 HQ 2× oversampling), the batched waveform-revision perf item, and one cosmetic cleanup (the dead `<cmath>` in `recording_buffer.cpp`) — worthwhile but none urgent.
