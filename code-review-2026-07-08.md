# RobotBoy code review — July 8, 2026 (open items)

Full-repo review at commit `f222f8a`: all four modules (Loooop, Lop, MF-20, Particules), the vendored `beads_dsp` library, plugin glue/metadata/build files, and tests. Five independent review passes were run in parallel, and every critical/major finding was re-verified directly against the source.

**Update (July 8, 2026):** the five highest-priority findings were fixed on branch `code-review-fixes` (see `docs/superpowers/plans/2026-07-08-code-review-fixes-spec.md`). Release note: FEEDBACK on MetaModule was previously nearly inert and now regenerates properly — patches with feedback cranked will sound denser than before.

**Update (July 9, 2026 — round 2):** both remaining majors and the round-1 priority items were fixed (see `2026-07-08-code-review-fixes-2-spec.md`): sample-rate-change loop preservation, MF-20 NaN recovery, NaN guards, Particules UX minors, MF-20 transcendentals at modulate rate, Loooop window-bounds hoist, beads hot-path hoists, dead-code sweeps (wavetable, DTC, dead params).

**Update (July 9, 2026 — codex carry-over):** the worthwhile work from the parallel `review-fixes-codex` branch was ported (see `2026-07-09-codex-carryover-spec.md`): SVF denominator cache + Tick() dedup, MetaModule flush-to-zero, CleanLoFi limiter actually bounds, MF-20 first-modulate/R-poly/drive smoothing, Loooop⁄Lop⁄MM-cores shared control math (with NaN-suppressing clamps), the waveform display cache (−47%/−84% frozen-loop raster), grain SIZE-mapping dedup + exact block overlap smoothing, RNG [0,1) fix, SR-independent grain-LED decay, thread-safe deferred "Clear buffer". Fixed items removed below; behavior notes: MF-20 drive sweeps are now smoothed (5 ms); the menu "Clear buffer" takes effect at the next block boundary; grain-LED decay is wall-clock-constant across sample rates.

**⚠ Pending USER CHECKS (manual, GUI):**
1. **MetaModule simulator** — waveform display cache: record a loop on Loooop, freeze, overdub, clear, switch modules and back; the waveform must stay correct across frames (canvas-persistence assumption). Details: `.superpowers/sdd/task-5-report.md` USER CHECK section.
2. **VCV listening test** — grain overlap smoothing: dense Particules cloud (Density high, Size mid), sweep Density; listen for pumping/stepping in loudness. Details: `.superpowers/sdd/task-6-report.md`.

---

## MF-20

### Bugs

1. **[minor] Voices activated by a channel-count increase carry stale state.**
   `src/mf20/engine.hpp` (`EnginePool::setVoices`). A voice that rang at high resonance, went inactive, then reactivates emits its old ringing state. **Fix:** in `setVoices()`, reset engines in `[oldActive, newActive)` when the count grows.

2. **[minor, judgment call] K35's asymmetric input clip creates DC that the LP output passes.**
   `src/mf20/MF20Filter.hpp` (forward-path clip, T_neg = 0.85). Hot input + high drive in K35 puts a DC offset on the LP outputs. The hardware arguably does this too; if unwanted, a one-pole DC blocker on the LP output fixes it.

3. **[nit] Cutoff floor is 20 Hz even under CV** (`MF20Filter.cpp`, `modulate()`) though the core supports 1 Hz — deep negative CV can't close the filter the way an MS-20 can. Intentional? If not, lower the clamp.

### DSP improvements

- **Hard clip knee aliases** at high drive/cutoff. Rounding the knee with a small quadratic transition region kills the worst high-order harmonics for a few ops; 2× oversampling of the core could be a VCV-only context-menu "HQ" option.

### Low-complexity features

- One HP-stage output jack (the core already computes HP/BP) — only if there's panel room.

### Verified correct (don't re-investigate)

TPT algebra in both modes, OTA region classification, V/oct CV math, JSON round-trip. K35 loop clip algebraically identical in the linear region with guarded bistable-band fallback. `processG(cutoffToG(fc))` bit-identical to the old per-sample path at steady state. NaN recovery, first-modulate timing, R-poly count, and drive-threshold equivalence covered by tests; drive smoothing verified free of per-sample sqrt/divide.

---

## Loooop / Lop

### Bugs

1. **[minor] Seam interpolation wraps to `floor(winStart)` instead of the fractional window start.**
   `LoopEngine.cpp` (`readInterpolated`, the `i1` wrap) — the cast truncates, so with crossfade off and a fractional window the last read before the wrap interpolates toward the wrong sample (up to half a step of error → small extra click on seam-exact windowed sub-loops). **Fix:** interpolate the wrap target at fractional `winStart` (two reads + lerp).

2. **[minor] "Initialize" (Ctrl+I) resets knobs but leaves the recorded loop playing.**
   Neither module overrides `onReset`. **Fix:** `void onReset(const ResetEvent& e) override { Module::onReset(e); engine.clear(); }` in both (`clear()` is already audio-safe, and now correctly bumps the display cache's waveform revision).

3. **[nit] Short-circuited `||`** between button and CV Schmitt triggers (`Loooop.cpp`, `Lop.cpp`, top of `process()`) merges same-sample events and skips one sample of the CV trigger's processing. Evaluate both into locals first.

4. **[nit] A jitter-amount decrease takes one extra wrap to apply.** `setJitter` rescales only the *next* roll. Fix if desired: clamp `jitterNext` on amount decrease.

5. **[nit] A same-rate `setSampleRate()` on an empty engine still runs the full \~23 MB `reset()`.** Rack dispatches a sample-rate event on module add, so the constructor's reset is immediately repeated at 48 kHz hosts. Harmless (no-loop path only) but a one-line early-out removes it.

6. **[nit, new from carry-over review] `bumpWaveformRevision()` fires per recorded sample** (`LoopEngine.cpp`, `writePeak`) — a `dmb ish` per sample on Cortex-A7 while recording. Correct, but could batch (bump on peak-bin change, \~every 1.4k samples, ≤30 ms display lag) if recording CPU ever matters.

### DSP improvements

- **Overdub punch-out records a permanent step into the loop** (`LoopEngine.cpp`, overdub write path): input sums at full gain from the first sample and stops dead at `toggleRecord()`; punch-out lands mid-loop where the seam crossfade can't mask it, clicking on every subsequent pass. **Fix:** ramp an overdub-input gain 0→1 over `xfadeSamples_` at start and 1→0 at stop (defer `recording_ = false` by the ramp length). One multiply per sample while recording.
- **Linear → 4-point cubic (Hermite) interpolation** (`readInterpolated`/`readRaw`). At fractional speeds and V/oct melodies, linear interpolation costs audible HF and adds intermodulation. Cubic is \~4 reads + a few MACs per channel — the single biggest fidelity win available, still modest on MetaModule.
- **Head level/pan/dry-wet are applied unsmoothed** (`LoopEngine.cpp` head output, `Loooop.cpp` mix): a square LFO into Level CV produces hard steps. A 1–5 ms one-pole on each removes it for a few ops.
- **One-shots end with a hard cut** (`fadeLen` returns 0 for one-shots; `advanceHead` stops dead). A short gain-ramp fade-out over the last `xfadeSamples_` de-clicks the "rhythmic re-slicer" patch.
- No anti-aliasing above 1× speed (speed 2 folds everything above SR/4). A pragmatic half-measure is averaging two reads spaced `sp/2` when `|speed| > 1`. Optional.
- Constant-power pan would be nicer than linear balance, but the center-unity law is load-bearing for the "four heads at 0.25 sum to unity" default.

### Display

- Static-waveform caching landed (carry-over round). Remaining quality note: `peakBinSize_` is fixed by `maxSamples_`, so a 1 s loop uses \~68 of 4096 bins stretched across the display — blocky. Rebinning the peaks over `loopLen_` once at freeze time (off the per-sample path) fixes it.

### Low-complexity features

- **Overdub feedback/decay (sound-on-sound):** `buf = buf * feedback + in`. One multiply per sample while recording plus one context-menu param; classic Frippertronics decay *and* bounds the unbounded overdub sum.
- Considered and rejected as *not* low-complexity: overdub phase alignment, undo-overdub, saving the loop with the patch.

### Verified correct (don't re-investigate)

`clear()` not zeroing the buffer is safe; division-by-zero paths guarded; audio↔GUI snapshot atomics documented and bounded; CV scalings match the spec. Jitter+crossfade continuity tested (forward). Sample-rate changes preserve the loop (tested); NaN inputs recorded as 0 (tested); window-bounds hoist bit-identical; shared control math byte-equivalent with NaN-suppressing clamps (`clampSafe` = rack::clamp for ALL inputs, NaN-pinned tests); display-cache invalidation verified complete (every peak-mutating path bumps), split-render byte-identity and tiny-geometry OOB tests in place.

---

## Particules (wrapper)

### Bugs

1. **[nit] Allocation failure is silent** (`Particules.cpp`, constructor DSP init) — the engine null-guards everything (verified), so it degrades to silence, but a `WARN()` would help debugging.

2. **[nit] Doc mismatches in Particules.md:** SEED input described as "bottom right" but placed top-right; the quality-mode table states fixed rates (48/32/24/24 kHz) but the engine applies decimation factors 1/2/8/4 to the *host* rate — 48/24/6/12 kHz at a 48 kHz host.

3. **[nit] `CvDecimationForBlock` divides `8 / block_size` unguarded** (`particules_cv_conditioning.h`) — UB at 0, unreachable today. A one-line guard is cheap future hardening.

4. **[nit, new from carry-over review] A queued menu "Clear buffer" can be arbitrarily delayed** if the module is bypassed (consumed only in the `BlockReady()` branch). Harmless; drain in `onReset`/on-bypass or accept as documented.

### Refactorings

- `particules_density_control.h` — takes *raw* voltage but names the param `conditioned_density_cv`, and includes three headers it doesn't use. Rename or inline the `* 0.2f`.
- `Particules.cpp` includes a vendored-library internal (`src/util/control_conditioner.h`); promote it to `include/beads/`.

### DSP improvements

- **Batch the VCV path.** With `kWrapperBlockSize = 1`, every VCV sample pays full `updateSlowParams()` (\~12 `getVoltage()` calls, 4 conditioner steps), a whole-struct `SetParameters()`, and per-call engine setup. A 16–32 sample VCV block amortizes all of it for \~0.3–0.7 ms latency (the engine mixes dry internally, so no comb risk — feedback and dry/wet are already smoothed engine-side) and further narrows VCV/MM behavioral differences.
- **Grain trigger pulses merge on retrigger** (`particules_block_runtime.h`) — two grains < 1 ms apart produce one continuous high. Force one low sample between pulses.

### Low-complexity features

- **Scale-aware pitch lock** (engine hooks exist). **Grain-count-aware LED** (`ActiveGrainCount()` unused). **Input level readout** in the gain menu (`InputLevel()` unused). **Undo for context-menu options** (`history::ModuleChange`, VCV-only).

### Verified correct (don't re-investigate)

Block-runtime ordering/indexing at both block sizes; pitch notch map; engine null-guards; `GetMemoryRequirements` genuinely sample-rate-independent on this branch (fixed reverb sizing — the codex branch's SR-scaled variant was deliberately not adopted). SEED latching, pitch CV pass-through, conditioner timing, LED decay SR-independence (incl. the reassignment-loses-config pin test), and FTZ (FPSCR sequence verified in ARM disassembly; re-armed on SR change) covered.

---

## beads_dsp (vendored library)

### Bugs

1. **[minor] Grain kill fallback fade never reaches zero** (`grain.h`, kill fallback): `fade = counter/4` runs 1.0, 0.75, 0.5, 0.25 then cuts — a 0.25-amplitude step (audible tick when the pool saturates on DC-heavy content). Use `(counter − 1)/kFallbackFadeSamples`.

2. **[minor] Reverb `DelayLine`s would dereference null on undersized buffers** (`fx_engine.h`, `reverb.cpp`) — unreachable today, but `Reverb::kMinBufferSize` is declared and never checked. Guard `Init`.

3. **[minor] Defensive wrap loops can hang on huge finite positions** (`recording_buffer.cpp` etc.): `while (position >= size_f) position -= size_f;` never terminates once `ulp(position) > size_f`. Currently unreachable, but it *is* the stated defensive layer — use `fmod`/`isfinite`. Related: unguarded float→int casts in `ReadHermiteStereoFast` make a NaN grain position UB; one `isfinite` fence in `ComputeGrainParams` covers the whole path.

4. **[minor] "Kill the oldest grain" kills an arbitrary one** (`grain_engine.cpp`) — it marks the first non-pending grain by array index, which is frequently the *newest* (slot 0 is reused first). Under saturation, fresh grains die while old ones ring on. Track start order or pick the highest `envelope_phase_`.

5. **[minor] Scheduler dead state / mode-change glitch** (`grain_scheduler.cpp`): `clock_period_` is measured and smoothed but never read; `BeadsParameters::clock_interval_samples` is never read by anyone. `gate_phase_` doubles as the clock-division counter without reset on trigger-mode change, so the first division after switching gated→clocked can be off by one.

6. **[nit] `NextGaussian()` isn't unit variance** (`random.h`): CLT sum with σ ≈ 0.577 and hard range ±2. (The misleading comment was fixed in the carry-over round; the math itself only matters if spread math was designed in unit-sigma terms.)

### Refactorings

- **Quality-mode crossfade duplication** — `quality_processor.cpp` (two near-identical blocks).
- **Orphan sweep (accumulated):** `WavetableProvider` + `kWavetableSize` in `include/beads/types.h`; `RecordingBuffer::CopyRegionTo`; and (new, from the carry-over round) `render_load_tier_` + `kHighLoadActiveGrains` in `grain_engine.{h,cpp}` — now write-only after the overlap-smoother change removed its last reader. One small dead-code commit.
- `RecordingBuffer` read-path duplication: only `ReadHermiteStereoFast` is used in production; `ReadHermite`/`ReadHermiteStereo`/`ReadLinear` share identical guard/wrap code — demote to test-only or consolidate.
- **RT-hygiene nit:** auto_gain's `kSilenceGain` is still a function-local `static const` with a dynamic initializer (`auto_gain.cpp`, kLocked branch) — hoist like the others were.
- **Nit:** the 48 kHz g-domain defaults (0.0491/0.0079) appear as bare literals in four places in `src/mf20/engine.hpp`; name them.

### DSP improvements

- **Dry path taps pre-auto-gain input** (`beads_processor.cpp` — deliberate so DRY/WET = 0 equals bypass) — but with auto-gain boosting +32 dB, mid-knob mixes have a big dry/wet level mismatch; hardware Beads crossfades post-gain. Consider tapping post-gain, or document the trade-off.
- **Adaptive interpolation under load** — removed with the DTC path in round 2. If CPU headroom under grain saturation ever matters, wire a fresh load-tier check into `Grain::Process`/`ProcessBlock`.
- **kMidi burst mode clumps** (`grain_scheduler.cpp`): up to 15 burst grains land inside one ≤1.3 ms block. Spread the burst across blocks with a pending counter.
- **Reverb never sleeps** (`reverb.cpp` gates input at `amount_ == 0` but the full 12-delay-line tank runs forever). On the MetaModule budget, add an energy-based sleep once the tail decays.
- Grain-rate cap at 80 Hz is a deliberate, documented retirement of the old high-rate path — revisit only if "faithful recreation" is the goal.
- The overlap-normalization "slow fall" coefficient (`grain_engine.cpp`) is 0.2/sample ≈ 5-sample time constant — not slow; if pumping is ever heard, it wants \~0.001. (The carry-over round made the block application of this smoother exact; the coefficient value itself is unchanged.)

### Test coverage gaps

- **Interpolation-tail sync** — no test writes at `write_head_ < kInterpolationTail` then reads fractionally across the `size_` boundary.
- **No scheduler tests for kClocked or kMidi modes** — kClocked is what the host selects when SEED is patched.
- **No NaN-robustness tests for the beads engine** (feed NaN CVs, assert finite output).
- **A `feedback = 0` control case** in `test_feedback_path.cpp`.
- **Loooop SR-change coverage breadth** (multi-head, non-default speed, redundant same-rate call).
- Stale test comments: `test_quality_modes.cpp` (8 kHz vs 5 kHz), `test_reverb.cpp` (0.3 vs 0.02 st wow).

### Verified correct (don't re-investigate)

Interpolation-tail write mirroring; grain envelopes reach \~0 at both edges; decimation ratios; reverb partition fits, `fb ≤ 0.9375` stable; no heap allocation or locks on the audio path; xorshift128 deterministic. Block-size independence, freeze seams, feedback continuity tested. SVF `Tick()` verified equivalent with reciprocal cached on every g/r-changing path; SoftClip auto-gain delta ≤\~0.5%, ceiling unchanged; grain SIZE helpers re-derived equivalent at both call sites; exact block overlap coefficient algebraically identical to n per-sample updates; RNG `NextExponential` guarded against the now-possible exact 0.

---

## Plugin glue, build, metadata, tests

Remaining items, all minor:

1. **`plugin.json` lacks `sourceUrl`/`manualUrl`** — required (`sourceUrl`) for VCV Library submission.
2. **The two Python guard tests aren't wired into any lane** — add `python3 -m unittest discover` to `tests/run.sh`.
3. **Object files escape `build/` into `vcv/src/`** — extend the clean target.
4. **Module list duplicated** between plugin.json and plugin-mm.json — extend `test_robotboy_identity.py` to assert the slug lists match.
5. **Version stated twice** (plugin.json and CMake `project()`) — drop one or derive it.
6. Nits: stale test-count comment in `tests/beads/CMakeLists.txt`; `-std=c++20` applied to all languages in `metamodule/CMakeLists.txt`; Makefile `cp` sync swallows failures; empty `presets/` not in `DISTRIBUTABLES`; `plugin-mm.json` schema keys unverified against 4ms.

---

## Still to do, by category

### Fixes

1. Loooop/Lop: "Initialize" should clear the loop (`onReset` override). (§ Loooop #2)
2. beads: "kill the oldest grain" kills an arbitrary one. (§ beads #4)
3. beads: grain-kill fallback fade 0.25-amplitude step. (§ beads #1)
4. beads: scheduler off-by-one on mode change + dead clock state. (§ beads #5)
5. MF-20: reactivated voices carry stale ringing state. (§ MF-20 #1)
6. Loooop/Lop: seam interpolation wraps to `floor(winStart)`. (§ Loooop #1)
7. Small hardening batch: beads wrap loops → `fmod`/`isfinite` + `isfinite` fence (§ beads #3); reverb `Init` guard (§ beads #2); `CvDecimationForBlock` guard (§ Particules #3); allocation `WARN()` (§ Particules #1); short-circuited `||` (§ Loooop #3); jitter-decrease nit (§ Loooop #4); same-rate `setSampleRate` early-out (§ Loooop #5); Particules.md doc fixes (§ Particules #2).

### Performance improvements

1. **Batch the Particules VCV path** (block 16–32) — the biggest remaining CPU item. (§ Particules DSP)
2. Reverb energy-based sleep when idle. (§ beads DSP)
3. Batched waveform-revision bump while recording. (§ Loooop #6)
4. `kSilenceGain` magic-static hoist. (§ beads Refactorings)

### Refactors

1. Orphan/dead-code sweep: `WavetableProvider`/`kWavetableSize`, `CopyRegionTo`, `render_load_tier_`/`kHighLoadActiveGrains`. (§ beads Refactorings)
2. beads duplications: quality-crossfade blocks, `RecordingBuffer` read-path consolidation. (§ beads Refactorings)
3. `particules_density_control.h` naming/includes; promote `control_conditioner.h`. (§ Particules Refactorings)
4. Name the MF-20 g-domain default constants. (§ beads Refactorings)
5. Build/meta cleanups: python tests wired in, clean target, slug parity check, single version source, `sourceUrl`/`manualUrl`, misc nits. (§ Plugin glue)

### New features (deferred — not next)

Overdub feedback/decay; scale-aware pitch lock; MF-20 HP output; grain-count LED, input-level readout, menu undo; DSP-quality options (cubic interpolation, overdub ramps, one-shot fades, smoothed level/pan, anti-aliasing, clip-knee rounding, post-gain dry tap, kMidi burst spreading, display rebinning).

### Other

1. Test-coverage gaps: beads NaN robustness, kClocked/kMidi scheduler, interpolation-tail sync, `feedback = 0` control, SR-change breadth, stale comments. (§ beads test gaps)
2. Pending USER CHECKS (top of this file): MetaModule sim display cache; grain-overlap listening test.
3. Release checklist: `plugin-mm.json` schema check; VCV Library `sourceUrl`.

---

## Highest-value next steps (round 3 — no new features)

Chosen for user-audible impact and MetaModule CPU, in order:

1. **Loooop/Lop `onReset` clears the loop** — trivial, user-facing correctness; the only remaining "does the wrong thing on a standard action" bug. (Fixes #1)
2. **Batch the Particules VCV path** — the single biggest remaining CPU win, and it narrows VCV/MM behavioral divergence. Needs care (latency, calibration triggers, per-sample outputs like the grain-trigger pulse). (Perf #1)
3. **beads grain-scheduler fix bundle** — kill-oldest-grain, kill-fade step, gate_phase_ off-by-one + dead clock state. All audible under saturation/clocked use; one focused task in one subsystem. (Fixes #2–4)
4. **MF-20 stale-voice reset + Loooop seam fractional wrap** — two small audible fixes. (Fixes #5–6)
5. **Hardening + hygiene sweep** — the small-guards batch (Fixes #7), the orphan/dead-code sweep (Refactors #1), `kSilenceGain` hoist (Perf #4), g-default constants (Refactors #4).
6. **Test wiring + coverage** — python lane into `tests/run.sh`, slug-parity check, beads NaN-robustness tests, `feedback=0` control case, stale test comments. (Other #1, Glue #2/#4)

Deliberately deferred from this round: reverb sleep (needs design for wake threshold), batched revision bump (wants profiling first), quality-crossfade/RecordingBuffer consolidation (larger refactors, no behavior change), everything under "New features".
