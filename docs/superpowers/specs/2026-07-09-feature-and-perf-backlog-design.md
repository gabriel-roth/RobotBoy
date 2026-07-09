# Feature & performance backlog — specs for consideration

**Date:** 2026-07-09
**Source:** `code-review-2026-07-08.md`, §"Still to do" → "New features" (items 1–5) and "Performance improvements" (items 1–2).
**Status:** decision document. Nothing here is committed work; each spec is meant to be accepted, deferred, or rejected individually. All code references verified against the current tree on `code-review-fixes`.

**Effort scale:** S = a couple of hours, M = about a day, L = multiple days.

## Summary table

| # | Item | Value | Effort | Risk | Recommendation |
|---|------|-------|--------|------|----------------|
| F1 | Loooop overdub feedback (sound-on-sound) | High — the missing looper feature; also bounds the overdub sum | S–M | Low | **Do** |
| F2 | Loooop overdub start/stop ramps | High — fixes the permanent punch-out click | S | Low | **Do with F1** (same code region) |
| F3 | Particules scale-aware pitch lock | High — engine side already done and tested | M | Low | **Do** |
| F4 | MF-20 HP output jack | Medium | S code + panel art | Low | **Do** (one panel slot is free) |
| F5 | Particules grain-count LED | Low–medium | S | None | **Do** |
| F6 | Particules input-level readout | Low | S | None | **Do** |
| F7 | Particules grain-trigger pulse separation | Medium (patch correctness) | S | Low | **Do** |
| F8 | Particules menu undo (VCV-only) | Low | S | None | **Do** |
| Q1 | Loooop cubic (Hermite) interpolation | High — biggest fidelity win at fractional speeds | M | Medium (seam edge cases) | **Do** |
| Q2 | Loooop one-shot fade-out | Medium | S | Low | **Do** |
| Q3 | Loooop smoothed level/pan/dry-wet | Medium | S | Low | **Do** |
| Q4 | Loooop 2-tap anti-aliasing above 1× | Low–medium | S | Low | **Defer** — listen after Q1 lands |
| Q5 | Loooop display rebinning for short loops | Low (cosmetic) | M | Medium (GUI-thread buffer scan) | **Defer** |
| Q6 | MF-20 clip-knee rounding — tier 1 (K35 forward clip) | Medium | S | Low | **Do** |
| Q7 | MF-20 clip-knee rounding — tier 2 (loop clips) | Medium | M | Medium | **Decide after listening to Q6** |
| Q8 | MF-20 2× oversampling HQ menu option | Medium | L | Medium | **Defer** |
| Q9 | beads post-gain dry tap | Decision needed | S–M | Medium (changes bypass semantics) | **Menu option, default off** |
| Q10 | beads kMidi burst spreading | Medium; also closes a test gap | S–M | Low | **Do** |
| P1 | beads reverb idle sleep | Medium–high (MetaModule CPU) | M | Medium (tail-cut audibility) | **Do** |
| P2 | Loooop batched waveform-revision bump | Unknown until profiled | S | Low | **Profile first**, per the review |

---

# Features

## F1. Loooop/Lop overdub feedback (sound-on-sound)

**What/why.** Overdub currently sums input into the buffer at full gain forever: `bufL_[writeIdx_] += inL` (`src/loooop/dsp/LoopEngine.cpp:332-333`). There is no decay control, and the sum is unbounded (a long overdub session can grow without limit). Sound-on-sound feedback — `buf = buf * feedback + in` — is the looper feature users will look for first, and any feedback < 1 also bounds the sum.

**Design.**
- Engine: new member `float overdubFeedback_ = 1.f` with setter `setOverdubFeedback(float)` (clamped 0–1), used only on the overdub branch: `bufL_[writeIdx_] = bufL_[writeIdx_] * fb + inL` (same for R). `fb = 1` reproduces current behavior bit-exactly. The initial record pass (`loopLen_ == 0`, overwrite mode) is untouched. Decay applies only while overdubbing — the classic behavior; an idle loop never decays.
- `writePeak` already re-seeds each bin's min/max on entry (`LoopEngine.cpp:368-371`), so the display tracks the decayed content with no changes.
- Control: a new param `OD_FEEDBACK_PARAM`, `configParam(0.5f, 1.f, 1.f, "Overdub feedback", "%", 0.f, 100.f)` — params auto-persist in both hosts and Loooop/Lop have no `dataToJson` today, so a param avoids adding one. No panel widget — every existing ParamId already has a panel position, and adding a knob would mean panel-SVG rework on both modules. Exposed in the context menu: a slider bound to the ParamQuantity on VCV; on MetaModule, a `createIndexSubmenuItem` with discrete steps (100 / 97 / 95 / 90 / 85 / 75 / 50 %), following the pattern Particules uses for manual gain (`Particules.cpp:527-535`).
  - Range floor 0.5 rather than 0: below \~50% the loop audibly vanishes in a pass or two; the useful musical range is 85–100%. (Open to 0 if you'd rather have the full range.)
- Both modules get the menu item; the engine change covers Lop automatically.
- MetaModule param sync: a new ParamId requires the corresponding `metamodule/loooop/*_info.hh` entry (per the patch-id alignment note in the module source) — use the `update-all-metamodule-params` / `check-metamodule-coverage` skills at implementation time.

**Interaction with F2:** if both land, the overdub write becomes `buf = buf * effFb + in * odGain` where `effFb = 1 + (fb − 1) · odGain` — feedback fades in with the input ramp so punch-in doesn't step the decay on.

**Tests** (`tests/loooop/test_loop_engine.cpp`, headless): record a known loop, overdub with `fb = 0.5` and known input → assert `buf == old · 0.5 + new` per sample; `fb = 1` path asserted identical to the existing `test_overdub_sums`; feedback clamp; peaks track decay.

**Trade-offs.** None significant. The only design question is knob-on-panel vs menu; panel real estate says menu.

## F2. Loooop/Lop overdub start/stop ramps (punch declick)

**What/why.** `toggleRecord()` (`LoopEngine.cpp:50-67`) just flips `recording_`; input enters and leaves the loop as a step. The punch-out step is *recorded into the buffer* — it clicks on every subsequent pass. The review calls this the one remaining recorded-artifact bug-adjacent item.

**Design.**
- Engine members: `float odGain_ = 0.f` and a pending-stop flag. On overdub start: `odGain_` ramps 0 → 1 over `xfadeSamples_` (\~5 ms, already computed at `LoopEngine.cpp:13,45`). On overdub stop: don't clear `recording_` immediately; ramp `odGain_` 1 → 0 over `xfadeSamples_`, keep writing `buf·effFb + in·odGain_`, and set `recording_ = false` when the ramp hits 0.
- A second `toggleRecord()` during the stop ramp re-arms the up-ramp from the current gain (no snap).
- The initial record pass (overwrite mode) is untouched: there is no prior content to crossfade into, and the loop-seam crossfade in `readHead` already declicks the start/end join. Ramps apply to the overdub branch only.
- `xfadeSamples_` can be 0 at very low sample rates (test rates); ramp degrades to the current step behavior there, which keeps the exact-value engine tests valid at 10 Hz.

**Tests:** overdub a constant into a silent loop, assert the written samples follow the ramp envelope at start and stop; assert `recording_` stays true until the ramp completes (observable via a stop-then-immediate-read check); combined F1+F2 expression test.

**Trade-offs.** Punch timing becomes \~5 ms soft on both edges — inaudible as timing, and it's exactly what hardware loopers do.

## F3. Particules scale-aware pitch lock

**What/why.** The engine already has a complete, tested scale quantizer the wrapper never calls: `BeadsProcessor::LoadScale(const double* ratios, uint32_t n)` / `ClearScale()` / `SetScaleRoot(int midi)` (`src/vendor/beads_dsp/include/beads/beads.h:43-46`), backed by `PitchQuantizer` (`src/pitch/pitch_quantizer.h`, tested in `tests/beads/test_pitch_quantizer.cpp`). The wrapper currently exposes only the cruder `pitch_lock` (off / octaves / octaves+5ths, `Particules.cpp:566-570`).

**Design.**
- Extend the existing "Lock pitch" menu into a single unified selector (one list, so scale mode and pitch-lock mode can't double-quantize — note the engine applies `pitch_lock` *after* the scale quantizer, `grain_engine.cpp:149-151`):
  `Off | Octaves | Octaves + 5ths | Chromatic | Major | Minor | Major pentatonic | Minor pentatonic`
- Selecting one of the five scales sets `pitch_lock_ = 0` and calls `LoadScale()` with a static 12-TET ratio table (`exp2(semitone/12)`, period entry 2.0); selecting Off/Octaves/5ths calls `ClearScale()` and sets `pitch_lock_` as today.
- A "Root" submenu (C…B, default C), enabled for the scale modes, calling `SetScaleRoot(60 + semitone)`.
- Persistence: two new JSON keys `pitchScale` (0–7, clamped) and `pitchRoot` (0–11, clamped) following the existing `dataToJson`/`dataFromJson` pattern (`Particules.cpp:258-283`). Scale state must be re-pushed to the engine after any processor (re)`Init` — audit `onSampleRateChange`/constructor for where `Init` runs and re-apply there, same as other post-init state.
- Ratio tables live in a small standalone header (e.g. `particules_scales.h`) so the table→ratio math is testable in the headless lane.
- Out of scope (future): SCL file loading via the VCV file dialog. The `LoadScale` API takes arbitrary ratios, so this stays open.

**Tests:** headless check that each table is ascending, 1/1-relative, period-terminated; engine-side quantization already covered by `test_pitch_quantizer.cpp`. Wrapper wiring is compile+manual (no Rack test lane), consistent with prior rounds.

**Trade-offs.** Menu-only feature (no CV over scale). Five scales is a deliberate YAGNI floor — trivial to extend later since it's a table.

## F4. MF-20 HP output jack

**What/why.** The core computes LP/BP/HP every sample and discards all but LP: `struct Out { float lp, bp, hp; }` (`src/mf20/MF20Filter.hpp:105`); the module reads only `.lp` (`MF20Filter.cpp:220,231,235`). An HP jack is free DSP-wise. The module chains HP-stage → LP-stage; the natural tap is the HP stage's `hp` output (post-HP, pre-LP) — the MS-20-style HP out.

**Design.**
- **One decision to make — mono or stereo HP out.** The module is stereo (paired L/R jacks for audio in and LP out), but there is exactly **one** free panel slot: the 12.7 mm gap at the bottom I/O row between the input pair (x = 19.05) and output pair (x = 31.75), centered ≈ 25.4 mm under the DRIVE column (`MF20Filter.cpp:273-287` positions). One jack fits at \~(25.4, 113.69) without moving anything. **Recommendation: a single polyphonic mono-sum HP jack** (L+R summed, or L-only matching the mono-input normalling convention — L-only is simpler and consistent with `AUDIO_INPUT` normalling). A stereo HP pair would force a bottom-row relayout.
- Code: add to the OUTPUTS enum (`MF20Filter.cpp:26-30`), `configOutput` + `configBypass(AUDIO_INPUT, HP_OUTPUT)`, `setChannels` alongside the existing calls (`:243-244`), and write `hpStage`'s `.hp` in `processChannel`. Zero extra per-sample DSP — the value already exists.
- Panel: add the port graphic to `res/MF20Filter.svg` (and `vcv/res/` copy), regenerate positions via the panel helper, regenerate `metamodule/assets/MF20Filter.png`. MetaModule needs no per-jack code (jacks come from the shared C++; `plugin-mm.json` lists slugs only) — verify with `check-metamodule-coverage`.

**Tests:** the headless suite already asserts HP rolloff behavior on the core (`tests/mf20/test_mf20.cpp` HP tests); the new jack is wiring, verified by compile + the identity guard scripts.

**Trade-offs.** Mono HP out on a stereo module is a compromise; the alternative (stereo pair) costs a panel redesign. Bypass semantics for the new jack (input → HP out) are debatable but conventional.

## F5. Particules grain-count LED

**What/why.** `ActiveGrainCount()` exists and is unused (`beads_processor.cpp:290-291`, counts up to `kMaxGrains = 30`). The current LED is a boolean flash: any grain this block → full brightness, then exponential decay (`Particules.cpp:407-418`, `particules_block_runtime.h:64-81`). Scaling brightness by grain count makes the LED read as density.

**Design.** At the same block boundary, replace the boolean with: `count = processor_.ActiveGrainCount(); if (count > 0) SetGrainLed(max(GrainLed(), clamp(0.25 + 0.75·count/10, 0, 1)))`. One grain still visibly flashes (floor 0.25); \~10+ concurrent grains reads full-bright. Decay machinery unchanged. `GrainTriggeredThisBlock()` can stay as the "any activity" OR-in so sparse single grains at high pitch (short lifetimes that might straddle the block check) still register.

**Tests:** `SetGrainLed`/decay already covered in the block-runtime tests; the mapping is one expression — headless test of the clamp/floor math if put in the runtime header, otherwise compile+manual.

**Trade-offs.** None. Constants (floor 0.25, full at 10) are taste; adjust on the simulator.

## F6. Particules input-level readout in the gain menu

**What/why.** `InputLevel()` exists and is unused (`beads_processor.cpp:298-299`) — a linear peak envelope, ±5 V → 1.0, \~500 ms release. The gain menu already shows the current auto-gain in dB (`AutoGainItem`, `Particules.cpp:486-511`); showing the input level next to it turns the menu into a proper gain-staging view.

**Design.** Add a display-only menu entry "Input: −18.4 dB" (or "Input: silent" below −60 dB), `20·log10(InputLevel())`. On VCV, a custom `MenuItem` whose `step()` refreshes the label live while the menu is open (same pattern as `AutoGainItem`'s rightText). On MetaModule, a static label computed at menu-open time (menus there don't animate) — acceptable.

**Tests:** formatting helper (linear→dB string, silent threshold) in a testable header; rest is compile+manual.

**Trade-offs.** None.

## F7. Particules grain-trigger pulse separation

**What/why.** `StartGrainTriggerPulse()` *overwrites* the remaining-samples counter (`particules_block_runtime.h:52-54`), and the pulse is 1 ms while blocks are \~1.33 ms — so back-to-back block triggers re-arm the counter before it empties and the R output stays continuously high (`Particules.cpp:365-372, 409-414`). Downstream trigger inputs see one event instead of many; a dense cloud produces a DC-ish gate.

**Design.** In the block runtime (all in `particules_block_runtime.h`, headless-testable):
- `StartGrainTriggerPulse(n)`: if `grain_trigger_remaining_ > 0`, set `pending_pulse_ = n` and let the current pulse run down to a forced single low sample (`gap_pending_ = true`); else start normally.
- `ConsumeTriggerPulseSample()`: when the counter hits 0 with `gap_pending_`, return false for exactly one sample, then load `pending_pulse_` and continue high.
- One low sample at 48 kHz (\~21 µs) is enough for Rack-standard Schmitt triggers (rearm at low threshold) and for hardware trigger inputs downstream of MetaModule.

**Tests:** extend `test_particules_block_runtime.cpp`: two Start calls in adjacent blocks → the emitted sample stream contains exactly one low sample between two high runs; single-pulse behavior unchanged.

**Trade-offs.** A retrigger arriving mid-pulse now *extends* total high time by the gap + new pulse rather than silently merging — that's the point.

## F8. Particules menu undo (VCV-only)

**What/why.** None of the context-menu options push undo actions (`history::` appears nowhere in `src/`). Rack convention is that module-menu changes are undoable.

**Design.** A small helper in `Particules.cpp` (inside `#ifndef METAMODULE`): capture `toJson()` of the module before and after the mutation, push a `history::ModuleChange` with both snapshots. Wrap the mutating lambdas for: SEED CV mode, Lock pitch (and the F3 scale/root items), Auto gain toggle, Manual gain value, Grain trigger on R. **Not** "Clear buffer" — it mutates engine audio state that isn't in JSON and can't be restored.

Because the snapshot is whole-module JSON, one helper covers every field with no per-item code. The manual-gain *slider* is the awkward case: a drag produces many `setValue` calls, and Rack coalesces repeated whole-module snapshots poorly, so covering it means either chatty undo or fragile drag-start/drag-end hooks on `ManualGainQuantity` (`Particules.cpp:441-464`). **Recommendation: discrete items only; slider excluded.**

**Tests:** none headless (Rack-only); manual: change each option, Ctrl-Z restores.

**Trade-offs.** VCV-only by nature (MetaModule has no undo stack). Slider exclusion is the pragmatic cut.

---

# DSP-quality options

## Q1. Loooop cubic (Hermite) interpolation

**What/why.** `readInterpolated` is linear (`LoopEngine.cpp:184-208`); at fractional speeds and V/oct melodies linear interpolation is the dominant fidelity limit (HF rolloff + intermodulation). 4-point Catmull-Rom is the single biggest win available. The review rates it "still modest on MetaModule" CPU-wise: reads go 2 → 8 per stereo head-sample plus a short polynomial; worst case 4 heads ≈ 32 reads + 4 polys per sample.

**Design.**
- Add a window-aware tap fetch: a helper that resolves `pos + k` (k = −1, 0, +1, +2) into the window `[winStart, winStart + winLen)` with the same fractional-`winStart` seam semantics the current wrap tap uses (`LoopEngine.cpp:196-201`), clamping to `[0, loopLen_)` as today.
- Catmull-Rom on the 4 taps. Catmull-Rom reproduces linear ramps exactly, so ramp-based tests keep passing with tightened expectations rather than rewritten ones.
- `readRaw` (seam-crossfade helper) stays linear — it feeds a 5 ms crossfade where interpolation quality is irrelevant.
- No menu option: one code path, always on. If MetaModule profiling later shows pain, a compile-time or menu fallback can be added then (YAGNI now).
- Edge cases to nail in tests: taps straddling the seam with fractional `winStart`; 1-sample and minimum-window loops (the k = −1 / +2 taps must clamp, not wrap out of the window); reverse playback.

**Tests:** existing seam/crossfade/window tests must pass unchanged (`test_crossfade_declicks_seam`, fractional wrap, minimum audible window); new: interpolating a sampled sine at speed 0.5 has lower error vs the analytic sine than the linear baseline; exact-ramp reproduction; seam-tap clamp cases.

**Trade-offs.** The seam/window edge cases are genuinely fiddly (this is the item's whole risk); the test suite around the seam is strong, which is why this is M not L.

## Q2. Loooop one-shot fade-out

**What/why.** A one-shot ends by hard-stopping the head (`advanceHead`, `LoopEngine.cpp:277,291`), and one-shots explicitly disable the seam crossfade (`fadeLen()` returns 0 when `h.oneShot`, `:225`). The re-slicer patch clicks at every one-shot end.

**Design.** In `readHead`, when a one-shot head is within `xfadeSamples_ · |speed|` of its endpoint (winEnd forward / winStart reverse), apply the existing smoothstep gain (`t·t·(3−2t)`, the same curve at `LoopEngine.cpp:265-267`) as a fade-to-zero instead of a crossfade-to-seam. The stop logic in `advanceHead` is unchanged — by the time `playing = false`, the gain has already reached \~0. Retrigger during the fade (`triggerOneShot` calls `restartHead`) would snap from partial gain back to full — so `restartHead` on a fading one-shot starts the new pass with a fast (\~1 ms) gain ramp-in instead. Keep both under the existing `xfadeSamples_` machinery; no new constants.

**Tests:** one-shot forward and reverse end with monotonically decreasing |output| over the final fade window and no step > the crossfade-test threshold; retrigger-mid-fade continuity check.

**Trade-offs.** The last \~5 ms of a one-shot is attenuated — that's the feature.

## Q3. Loooop smoothed head level / pan / dry-wet

**What/why.** Head level is a raw multiply in the engine (`LoopEngine.cpp:347-348`); pan and dry/wet are raw per-sample maps in the modules (`Loooop.cpp:157-167`, `LooperModuleDSP.hpp:48-58`). A square LFO into Level/Pan/Mix CV produces hard steps. Nothing in `src/loooop/` has any smoothing utility today.

**Design.**
- Add a minimal one-pole smoother to `LooperModuleDSP.hpp` (mirroring `OnePoleSmoother` in `src/mf20/dsp_utils.hpp` rather than pulling in Rack's, so it stays headless-testable), \~2 ms time constant.
- Engine: smooth each head's level at the point of use (4 smoother states in `PlayHead` or alongside), advanced per sample.
- Modules: smooth `pan` per head and the dry/wet `mix` before applying (per-sample advance in the module process loop). CV-rate changes glide over \~2 ms; knob feel is unchanged.
- Deliberately **not** touching the pan law itself: center-unity linear balance is load-bearing for the four-heads-sum-to-unity default (`Loooop.cpp:47-48`, `test_four_heads_mix`). Constant-power pan stays off the table per the review.

**Tests:** step a head level 0→1, assert output follows a one-pole (no full-step in one sample) and settles; `test_module_dsp.cpp` gets smoother unit tests; four-heads-unity test unchanged at steady state.

**Trade-offs.** \~2 ms lag on level/pan CV — inaudible as lag, removes zipper/steps.

## Q4. Loooop 2-tap anti-aliasing above 1× — defer

**What/why.** No anti-aliasing above 1× speed; the review's pragmatic half-measure is averaging two reads spaced `sp/2` when `|speed| > 1` (`advanceHead`/`fadeLen` already compute `sp = fabs(speed)`, `LoopEngine.cpp:226,286`).

**Design (if taken).** In `readHead`, when `sp > 1`: `out = 0.5·(read(pos) + read(pos − sp/2))`, with the second-tap weight blended in over speed 1→1.5 to avoid a tone discontinuity when crossing 1×. It's a 2-tap boxcar — a crude comb lowpass, first null at `fs/sp` — not real band-limiting.

**Recommendation: defer.** Q1 (cubic) changes the HF picture at speed > 1 substantially; listen to cubic first and only add this if fast-forward playback still sounds gritty. Doing both blind risks paying the HF-dulling cost twice.

## Q5. Loooop display rebinning for short loops — defer

**What/why.** Peak bins are sized against `maxSamples_` (`peakBinSize_ = maxSamples_/4096`, `LoopEngine.cpp:18-19`), so a 2-second loop at the default 60 s buffer occupies only \~136 of 4096 bins and renders blocky. Note the renderer already scales the axis to true loop length (`axisLen`, `LoopWaveformRenderer.cpp:24`) — the blockiness is bin *granularity*, not axis mapping, contrary to the review's one-line phrasing.

**Design (if taken).** GUI-side, not engine-side: when `axisLen / peakBinSize < threshold` (\~256 bins on screen), `renderWaveform` bypasses the peak arrays and builds per-pixel min/max by scanning the audio buffer directly (new `const float*` accessors on the engine). Rationale for GUI-side: an engine-side re-bin at freeze time scans `loopLen_` samples on the audio thread — exactly the class of stall that crashed a MetaModule patch in `clear()` before. GUI reads of a buffer being overdubbed are racy-but-benign, consistent with the documented unlatched-peaks design (`LoopEngine.hpp:58-65`). The scan is bounded by the threshold (≤ \~180k samples/render at defaults) and only runs on revision change, but MetaModule's renderer budget is tight — this needs a simulator check.

**Recommendation: defer.** Cosmetic; the display is already correct, just coarse. Bundle it with the next display-focused round where the simulator check (user-run, per checklist policy) is happening anyway.

## Q6. MF-20 clip-knee rounding, tier 1: K35 forward clip

**What/why.** The K35 forward-path clip is a *static* piecewise-linear waveshaper on the drive-scaled input (`MF20Filter.hpp:224-237`: slope 1 → 0.25 with a hard corner at +1.0 / −0.85). Hard corners in a drive-scaled static nonlinearity are the cheapest aliasing source to fix — and unlike the loop clips, this one is **not** inside the closed-form solve, so rounding it is a local change with no algebra.

**Design.** Replace each corner with a quadratic transition region of half-width `w` (\~0.1 normalized): below `T−w` linear, above `T+w` the 0.25-slope line, between them the C1 quadratic that matches value and slope at both ends. Same for the −0.85 corner (asymmetry preserved — it's the K35 character and is pinned by the asymmetric-clip test). Steady-state I/O curve changes only within ±w of the corners.

**Tests:** existing K35 tests (asymmetric clip, forward-clip isolation, self-oscillation, K35-vs-OTA) must still pass — the asymmetry test compares positive/negative peaks and survives knee rounding; add a C1-continuity check (numerical derivative across the knee) and a "output within ε of old clip outside the knee region" check. No FFT tooling exists in the repo, so the aliasing improvement itself is verified by ear (VCV listening check, user-run).

**Trade-offs.** Slight softening of the clip character right at the corner — that's the intent.

## Q7. MF-20 clip-knee rounding, tier 2: loop clips — decide after Q6

**What/why.** The OTA diode clip and the K35 feedback clip are *inside* the closed-form per-sample solve (`MF20Filter.hpp:179-214` OTA, `:247-275` K35 loop): each region (linear / saturated) has its own linear solve for `x1`, selected by threshold checks. Rounding those knees means adding a third, quadratic region — the solve in that region becomes a quadratic equation in `x1` (solvable closed-form, one `sqrt`, region-selection logic grows from 2 to 3 branches per stage per sample).

**Design (if taken).** Quadratic knee of half-width `w` around `clipThreshold` (OTA) / `kFbThreshold` (K35 loop); in-knee solve via the quadratic formula with the numerically stable form; region selection ordered linear → knee → saturated so the common (linear) case stays the fast path. Discriminant is provably non-negative in the knee construction; still guard with the existing NaN-recovery net (`sanitize()` at `MF20Filter.cpp:148`).

**Recommendation: decide after Q6.** The loop clip runs at self-oscillation amplitudes where the resonance loop's own feedback dominates the spectrum; the forward clip (Q6) is likely most of the audible aliasing at "high drive/cutoff" per the review. Land Q6, do the listening check, and take Q7 only if screech-register drive still aliases audibly. If Q7 is taken, Q8 is probably moot.

## Q8. MF-20 2× oversampling HQ option — defer

**What/why.** A VCV-only "HQ" menu option running the filter at 2× is the brute-force alternative to Q6/Q7.

**Design (if taken).** Wrap the two cascade calls in `processChannel` (`MF20Filter.cpp:218-230`): halfband-interpolate input to 2×, run the filter twice per host sample, halfband-decimate. **Not** as trivial as it sounds: `g` targets are computed at modulate rate from the *host* rate (`cutoffToG`, `MF20Filter.hpp:135-138`); HQ mode must compute `g` against `2·fs` and keep the g-domain smoothers consistent when toggled — an SR-change-like transition. Menu + JSON flag follows the existing `_filterMode` pattern (`MF20Filter.cpp:90-103, 290-302`). No spectrum-test tooling exists; correctness tests would be equivalence-at-low-drive (RMS) plus the existing suite run in HQ mode.

**Recommendation: defer.** L-sized, VCV-only, and redundant if Q6 (+ possibly Q7) already kills the audible aliasing. Revisit only on listening evidence.

## Q9. beads post-gain dry tap — make it a menu option, default off

**What/why.** The dry path is tapped before auto-gain (`beads_processor.cpp:178-179`, deliberate: DRY/WET = 0 equals bypass), but auto-gain can add up to +32 dB (`auto_gain.h:27-29`) to the wet side — mid-knob mixes can have a gross dry/wet level mismatch. Hardware Beads crossfades post-gain. Both behaviors are defensible; this is a taste decision, not a bug.

**Design.** A `bool dry_post_gain` in `BeadsParameters` (default false = current behavior). In the processor, when set, the dry tap at `beads_processor.cpp:179` stores the post-auto-gain sample instead (i.e., moves below line 182); everything else (equal-power crossfade at `:271-276`) is unchanged. Wrapper: context-menu bool "Dry signal follows input gain", persisted JSON key, undo-wrapped per F8. Note the post-gain dry also passes AutoGain's `SoftLimit`, so "dry" is no longer bit-clean at hot inputs — worth a manual-page sentence.

**Recommendation.** Option, default **off**: modular users patch around bypass-equality assumptions, and breaking `DRY/WET=0 == bypass` silently would be a regression for existing patches. The option gives Beads-hardware behavior to those who want it.

**Tests:** processor-level Catch2: with the flag on, DRY/WET = 0 output equals the auto-gained input (not the raw input); flag off preserves the current exact-bypass test.

## Q10. beads kMidi burst spreading

**What/why.** In kMidi mode with density < 0.5, a gate edge emits up to 15 burst grains *inside one ≤64-sample block* (\~1.3 ms) — `grain_scheduler.cpp:212-222` — which clumps into a thud rather than a burst, and can momentarily saturate the 30-grain pool. kMidi is also the one scheduler mode with zero test coverage; this feature would bring its first tests.

**Design.**
- New member `pending_burst_ = 0` in `GrainScheduler` (state today is only `gate_phase_`/`prev_gate_`, `grain_scheduler.h:46-47`).
- On the rising edge: compute `burst_count` as today, emit at most `kBurstPerBlock = 2` this block (offsets spread across the block as now), stash the remainder in `pending_burst_`.
- Each subsequent `Process` call drains up to 2 more (same offset spreading) until empty; 15 grains now span \~8 blocks ≈ 10 ms — a burst, not a clump. A new gate edge resets `pending_burst_` (retrigger replaces the old burst). Reset in `Init`/mode change alongside the existing counter resets.
- Bound interactions unchanged: `max_triggers = 32` per block, pool cap at the engine.

**Tests:** first kMidi Catch2 tests: rising edge at low density yields triggers across multiple consecutive blocks with total count = old single-block count; retrigger resets; density ≥ 0.5 (CW repeat path) unaffected. This closes the "kMidi scheduler mode still untested" gap from the review's test-coverage list.

**Trade-offs.** Burst timing changes audibly (that's the point); anyone relying on the 1 ms thud loses it — accepted.

---

# Performance improvements

## P1. beads reverb idle sleep

**What/why.** The reverb gates its *input* at `amount_ == 0` (`reverb.cpp:143`) but the full Dattorro tank — 12 delay lines, 8 allpasses, 2 modulated interpolated taps, LFO, LPs, DC blockers, per-sample `fb` recompute — runs unconditionally per sample (`reverb.cpp:98-231`, called from the per-sample output loop at `beads_processor.cpp:280`). `params.reverb` defaults to 0 (`parameters.h:21`), so most patches pay the full tank for silence. This is the biggest single MetaModule CPU item left.

**Design.**
- Sleep condition: `amount_ == 0` **and** the tank has decayed. Track a cheap wet-output peak envelope inside `Process` (abs-max of `wet_l/wet_r`, one multiply decay per sample — the same envelope pattern as `AutoGain`, `auto_gain.cpp:124-136`). When `amount_ == 0` and the envelope stays below \~−80 dBFS for \~250 ms, flush state (zero the 12 delay lines + feedback/LP/DC members — a one-time memset of \~11k samples, at sleep entry only) and set `asleep_ = true`.
- While asleep, `Process` short-circuits: output = dry crossfade only (which at `amount_ == 0` is what the full path produces anyway, since `wet_xfade_` is 0), skipping the entire tank. Cost drops to a branch.
- Wake: `SetAmount(> 0)` clears `asleep_` (state was flushed, so the tank starts clean — no stale-tail burp). Since `SetAmount` is called every block from `params.reverb` (`beads_processor.cpp:120`), wake latency is one block.
- Scope decision: sleep **only** at `amount_ == 0`. Sleeping when amount > 0 but input is silent would also save CPU but risks audible tail-cut/wake artifacts and needs input-side detection; not worth the risk for the default-case win. (Extend later if profiling says otherwise.)
- One subtlety the current code documents deliberately (`reverb.cpp:136-142`): delay lines are *not* flushed on the 0→nonzero transition so a tail can ring out naturally when amount is turned down briefly to 0 and back. Sleep changes this: if amount sits at 0 past the sleep threshold, the (already inaudible, < −80 dB) tail is flushed. The envelope gate is exactly what makes this inaudible; the 250 ms hold keeps quick 0-crossings (CV wiggle through zero) from flushing an audible tail.

**Tests** (Catch2, `tests/beads/`): amount = 0 + decayed → asleep (observable via output-equality with dry and a test hook or friend accessor); wake on SetAmount produces silence-then-clean-onset (no stale content, no NaN); a loud tail at amount = 0 is *not* cut before it decays below the threshold; envelope math NaN-fenced like the rest of round 3.

**Trade-offs.** The knob-at-zero-rings-out behavior is preserved up to the −80 dB point and truncated after; by construction inaudible. Adds \~3 ops/sample while awake (envelope) to save the whole tank while idle.

## P2. Loooop batched waveform-revision bump — profile first

**What/why.** `writePeak` calls `bumpWaveformRevision()` on every recorded sample (`LoopEngine.cpp:365-379`), i.e. one `memory_order_release` atomic store — a `dmb ish` on Cortex-A7 — per sample while recording. The review's own verdict: profile before doing anything; a predictable store to a hot cache line may be noise.

**Design.**
- **Gate: profile on MetaModule first.** Measure recording-path CPU with the store present vs stubbed (the simulator or a `#if` build). If the delta is < \~1% of the module budget, close this item as "not worth it" and keep the per-sample bump (simpler invariant: revision changes ⇔ peaks changed).
- If taken: bump only when `writePeak` enters a new bin (the existing `bin != lastPeakBin_` branch at `LoopEngine.cpp:368`) plus once in `toggleRecord()` on stop and in `clear()`/`reset()` (already there, `:34,88`). Display lag becomes ≤ one bin ≈ 15 ms at defaults (60 s buffer / 4096 bins at 48 kHz) — under the review's 30 ms bound. Audio output is bit-identical; only display-refresh timing changes.
- Subtlety: within-bin min/max *widening* (`:372-375`) would no longer bump; the display misses intra-bin envelope growth until the next bin transition. At ≤15 ms that's invisible, but it's why the test below asserts bin-transition bumps rather than value-change bumps.

**Tests:** adapt the existing revision test: during a recording pass, revision increments per *bin transition*, not per sample; still bumps on record stop, clear, reset; playback-only produces no bumps (unchanged).

**Trade-offs.** None if the profile gate is respected; the only cost is a slightly weaker "revision tracks every peak change" invariant.

---

# Suggested packaging, if approved

Independent tracks — any subset can proceed, each its own plan/branch:

1. **Loooop track:** F1 + F2 (one unit — same write path), then Q1, Q2, Q3. P2 profile gate alongside.
2. **Particules track:** F3, F5, F6, F7, F8 (all small, one plan), Q10, P1 (both beads-engine, Catch2-covered).
3. **MF-20 track:** F4 + Q6 (small pair); Q7/Q8 held for listening evidence.
4. **Deferred:** Q4, Q5, Q8 — revisit with evidence (post-Q1 listening, display round, post-Q6 listening respectively).
