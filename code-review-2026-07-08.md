# RobotBoy code review — July 8, 2026 (open items)

Full-repo review at commit `f222f8a`: all four modules (Loooop, Lop, MF-20, Particules), the vendored `beads_dsp` library, plugin glue/metadata/build files, and tests. Five independent review passes were run in parallel, and every critical/major finding was re-verified directly against the source.

**Update (July 8, 2026):** the five highest-priority findings were fixed on branch `code-review-fixes` (see `docs/superpowers/plans/2026-07-08-code-review-fixes-spec.md`). One release note from that round: FEEDBACK on MetaModule was previously nearly inert and now regenerates properly — patches with feedback cranked will sound denser than before.

**Update (July 9, 2026 — round 2):** both remaining majors and the round-1 "order of work" items 1–4 are fixed on `code-review-fixes` (see `docs/superpowers/plans/2026-07-08-code-review-fixes-2-spec.md`): Loooop/Lop sample-rate changes no longer erase the loop (playback repitches instead); MF-20 recovers from NaN/inf within \~2.5 ms; NaN input guards on Loooop/Lop/Particules; the Particules UX minors (freeze light, LED colors now match the manual, Schmitt gate hysteresis, randomize protection, onReset clears the engine, manual-gain clamp); MF-20 transcendentals moved to modulate rate with a by-value voice pool; Loooop window-bounds hoisted; beads hot-path pow/tanh hoists; and the dead-code sweeps (wavetable module, DTC path, `use_linear`, `seed_connected`/`stereo_input`, `EqualPowerCrossfade`, `FastPowUnit`, MF-20 pool dead code, `Particules.hpp`). Fixed items have been removed from this document; what remains below is still open. Behavior notes from round 2: MF-20 cutoff sweeps now smooth in the prewarp-gain domain (same 5 ms time constant, endpoints identical) and its dither is a deterministic ±1e-9 (bit-reproducible between VCV and MetaModule builds).

The plugin-level glue (slugs, registration, build parity, assets, docs) checked out unusually clean — only nits remain there.

---

## MF-20

### Bugs

1. **[minor] Polyphony channel count taken from the L input only.**
   `src/mf20/MF20Filter.cpp` (`process()`). A poly cable patched only into `AUDIO_INPUT_R` runs mono. **Fix:** `std::max({1, inputs[AUDIO_INPUT].getChannels(), inputs[AUDIO_INPUT_R].getChannels()})`.

2. **[minor] First `modulate()` is delayed \~2.5 ms after construction/patch load.**
   `src/mf20/MF20Filter.cpp` (`_steps = -1`). Until sample \~110 the filters run with default drive/g and **OTA mode even if the patch saved K35** (`dataFromJson` sets `_filterMode` but modes are only pushed in `modulate()`). **Fix:** initialize `_steps = _modulationSteps` so the first `process()` modulates immediately (the standard Bogaudio idiom). Round 2 note: the g-slew init values are the 48 kHz defaults, so at 96/192 kHz hosts the default cutoffs also sit up to \~2 octaves off during this window — same fix covers it.

3. **[minor] Voices activated by a channel-count increase carry stale state.**
   `src/mf20/engine.hpp` (`EnginePool::setVoices`). A voice that rang at high resonance, went inactive, then reactivates emits its old ringing state. **Fix:** in `setVoices()`, reset engines in `[oldActive, newActive)` when the count grows.

4. **[minor] Drive is stepped at the 2.5 ms modulate rate with no smoothing.**
   `src/mf20/MF20Filter.cpp` (`_drive`/`_driveSqrt`, set in `modulate()`). Gain up to √8 ≈ 2.83× and the clip threshold jump every \~110 samples → zipper noise when sweeping Drive with hot audio. **Fix:** a fifth `OnePoleSmoother` for drive, advanced per-sample.

5. **[minor, judgment call] K35's asymmetric input clip creates DC that the LP output passes.**
   `src/mf20/MF20Filter.hpp` (forward-path clip, T_neg = 0.85). Hot input + high drive in K35 puts a DC offset on the LP outputs. The hardware arguably does this too; if unwanted, a one-pole DC blocker on the LP output fixes it.

6. **[nit] Cutoff floor is 20 Hz even under CV** (`MF20Filter.cpp`, `modulate()`) though the core supports 1 Hz — deep negative CV can't close the filter the way an MS-20 can. Intentional? If not, lower the clamp.

### DSP improvements

- **Hard clip knee aliases** at high drive/cutoff. Rounding the knee with a small quadratic transition region kills the worst high-order harmonics for a few ops; 2× oversampling of the core could be a VCV-only context-menu "HQ" option.

### Low-complexity features

- The core already computes HP and BP (`struct Out { lp, bp, hp; }`) but only the cascade LP is exposed. One HP-stage output jack would give MS-20-style band-reject/HP patching for near-zero DSP cost — only if there's panel room.

### Verified correct (don't re-investigate)

TPT algebra in both modes (re-derived), OTA region classification, V/oct CV math, `onSampleRateChange` propagation, JSON round-trip, denormal avoidance via input dither. The K35 loop clip added by the fix branch was verified algebraically identical to the old math in the linear region, with a guarded fallback for the bistable band (k > 8/3 at fc ≈ 12–16 kHz). Round 2: `processG(cutoffToG(fc))` verified bit-identical to the old per-sample path at steady state; NaN recovery covered by tests.

---

## Loooop / Lop

### Bugs

1. **[minor] Seam interpolation wraps to `floor(winStart)` instead of the fractional window start.**
   `LoopEngine.cpp` (`readInterpolated`, the `i1` wrap) — the cast truncates, so with crossfade off and a fractional window the last read before the wrap interpolates toward the wrong sample (up to half a step of error → small extra click on seam-exact windowed sub-loops). **Fix:** interpolate the wrap target at fractional `winStart` (two reads + lerp).

2. **[minor] "Initialize" (Ctrl+I) resets knobs but leaves the recorded loop playing.**
   Neither module overrides `onReset`. **Fix:** `void onReset(const ResetEvent& e) override { Module::onReset(e); engine.clear(); }` in both (`clear()` is already audio-safe).

3. **[nit] Short-circuited `||`** between button and CV Schmitt triggers (`Loooop.cpp`, `Lop.cpp`, top of `process()`) merges same-sample events and skips one sample of the CV trigger's processing. Evaluate both into locals first.

4. **[nit] A jitter-amount decrease takes one extra wrap to apply.** `setJitter` rescales only the *next* roll; dropping jitter from 1.0 to 0.05 lets one already-pre-rolled window (offset up to ±0.5) play first. Not a click — preview and commit stay consistent. Fix if desired: clamp `jitterNext` on amount decrease.

5. **[nit] A same-rate `setSampleRate()` on an empty engine still runs the full \~23 MB `reset()`.** Rack dispatches a sample-rate event on module add, so the constructor's reset is immediately repeated at 48 kHz hosts. Harmless (no-loop path only) but a one-line early-out removes it.

### Refactorings

- **\~80 lines of `process()` and the context menu are duplicated line-for-line between Loooop and Lop** (Lop is literally the head-0 body). Any fix must currently be made twice. Extract shared free functions (`loopProcessGlobalControls`, `loopProcessHeadControls`) and a shared menu builder. No behavior change.

### DSP improvements

- **Overdub punch-out records a permanent step into the loop** (`LoopEngine.cpp`, overdub write path): input sums at full gain from the first sample and stops dead at `toggleRecord()`; punch-out lands mid-loop where the seam crossfade can't mask it, clicking on every subsequent pass. **Fix:** ramp an overdub-input gain 0→1 over `xfadeSamples_` at start and 1→0 at stop (defer `recording_ = false` by the ramp length). One multiply per sample while recording.
- **Linear → 4-point cubic (Hermite) interpolation** (`readInterpolated`/`readRaw`). At fractional speeds and V/oct melodies, linear interpolation costs audible HF and adds intermodulation. Cubic is \~4 reads + a few MACs per channel — the single biggest fidelity win available, still modest on MetaModule.
- **Head level/pan/dry-wet are applied unsmoothed** (`LoopEngine.cpp` head output, `Loooop.cpp` mix): a square LFO into Level CV produces hard steps. A 1–5 ms one-pole on each removes it for a few ops.
- **One-shots end with a hard cut** (`fadeLen` returns 0 for one-shots; `advanceHead` stops dead). A short gain-ramp fade-out over the last `xfadeSamples_` (no second read needed) de-clicks the spec's "rhythmic re-slicer" patch.
- No anti-aliasing above 1× speed (speed 2 folds everything above SR/4). A proper fix isn't cheap; a pragmatic half-measure is averaging two reads spaced `sp/2` when `|speed| > 1`. Optional.
- Constant-power pan would be nicer than linear balance, but note the center-unity law is load-bearing for the "four heads at 0.25 sum to unity" default (`Loooop.cpp`).

### Display

- No defects found. One quality note: `peakBinSize_` is fixed by `maxSamples_`, so a 1 s loop uses \~68 of 4096 bins stretched across the display — blocky. Rebinning the peaks over `loopLen_` once at freeze time (off the per-sample path) fixes it.

### Low-complexity features

- **Overdub feedback/decay (sound-on-sound):** `buf = buf * feedback + in` instead of `buf += in`. One multiply per sample while recording plus one context-menu param; gives the classic Frippertronics decay *and* bounds the currently unbounded overdub sum. This is the one feature looper users will actually go looking for.
- Considered and rejected as *not* low-complexity: overdub phase alignment (writes always start at index 0 — fixing it needs a canonical loop-phase clock; explicitly marked "v1" in the code), undo-overdub (a second 23 MB buffer), saving the loop with the patch.

### Verified correct (don't re-investigate)

`clear()` not zeroing the buffer is safe; all division-by-zero paths guarded; audio↔GUI snapshot uses atomics with the non-atomic peak reads documented and bounded; no dataToJson needed (all persistent state lives in params); every CV scaling matches the spec's "±10 V spans the range" claims. Jitter + crossfade continuity is covered by a test (forward wraps; the reverse path was verified by review trace but has no automated test). Round 2: sample-rate changes preserve the loop (tested: frozen loop, mid-recording, empty-realloc); NaN inputs are recorded as 0 (tested); the window-bounds hoist was traced bit-identical including the display stores' pre-commit values.

---

## Particules (wrapper)

### Bugs

1. **[nit] Allocation failure is silent** (`Particules.cpp`, constructor DSP init) — the engine null-guards everything (verified), so it degrades to silence, but a `WARN()` would help debugging.

2. **[nit] Doc mismatches in Particules.md:** SEED input described as "bottom right" but placed top-right; the quality-mode table states fixed rates (48/32/24/24 kHz) but the engine applies decimation factors 1/2/8/4 to the *host* rate (`types.h`) — 48/24/6/12 kHz at a 48 kHz host, different again at 96 kHz.

3. **[nit] `CvDecimationForBlock` divides `8 / block_size` unguarded** (`particules_cv_conditioning.h`) — UB at 0, unreachable today (compile-time constants 1 and 64). A one-line guard is cheap future hardening.

### Refactorings

- `particules_density_control.h` — takes *raw* voltage but names the param `conditioned_density_cv`, and includes three headers it doesn't use. Rename or inline the `* 0.2f`.
- `Particules.cpp` includes a vendored-library internal (`src/util/control_conditioner.h`); promote it to `include/beads/`.

### DSP improvements

- **Batch the VCV path.** With `kWrapperBlockSize = 1`, every VCV sample pays full `updateSlowParams()` (\~12 `getVoltage()` calls, 4 conditioner steps), a whole-struct `SetParameters()`, and per-call engine setup. A 16–32 sample VCV block amortizes all of it for \~0.3–0.7 ms latency (the engine mixes dry internally, so no comb risk — feedback and dry/wet are already smoothed engine-side) and further narrows VCV/MM behavioral differences.
- **Grain trigger pulses merge on retrigger** (`particules_block_runtime.h`) — two grains < 1 ms apart produce one continuous high. Force one low sample between pulses so downstream counters see one edge per grain.

### Low-complexity features

- **Scale-aware pitch lock:** the engine already exposes `LoadScale`/`ClearScale`/`SetScaleRoot` (`beads.h`) and the wrapper already has the "Lock pitch" submenu and JSON field. "Chromatic / Major / Minor pentatonic" entries are a few tables plus one call — high musical value, zero panel clutter.
- **Grain-count-aware LED:** `ActiveGrainCount()` (`beads.h`) is unused; scaling the grain light by active count makes Density visually legible (the current flash-and-2-second-decay reads as a fade, not the per-grain flash the manual promises).
- **Input level readout in the gain menu:** `InputLevel()` (`beads.h`) is unused; showing it next to "Manual gain" makes gain staging without auto-gain far less blind.
- **Undo for context-menu options:** the seed/pitch-lock/grain-trigger/gain settings bypass VCV's history — Ctrl+Z does nothing. `history::ModuleChange` wrapping is boilerplate-cheap (VCV-only).

### Verified correct (don't re-investigate)

Block-runtime read-before-push ordering and indexing at both block sizes; the pitch notch map; engine null-guards after failed `Init`; `GetMemoryRequirements` genuinely sample-rate-independent today (the re-`Init` in `onSampleRateChange` is safe, but assert the size to future-proof). SEED latching, pitch CV pass-through, and block-aware conditioner timing are covered by tests. Round 2: the redundant `output_index_` write in `PushInputSample` was verified dead (never observable in the real Read-before-Push order) before removal; freeze/SEED gates now carry Schmitt hysteresis; LED colors match the manual.

---

## beads_dsp (vendored library)

### Bugs

1. **[minor] Grain kill fallback fade never reaches zero** (`grain.h`, kill fallback): `fade = counter/4` runs 1.0, 0.75, 0.5, 0.25 then cuts — a 0.25-amplitude step (audible tick when the pool saturates on DC-heavy content). Use `(counter − 1)/kFallbackFadeSamples`.

2. **[minor] Reverb `DelayLine`s would dereference null on undersized buffers** (`fx_engine.h`, `reverb.cpp`) — unreachable today (needs ≈ 11,996 of 16,384 floats), but `Reverb::kMinBufferSize` is declared and never checked. Guard `Init`.

3. **[minor] Defensive wrap loops can hang on huge finite positions** (`recording_buffer.cpp` etc.): `while (position >= size_f) position -= size_f;` never terminates once `ulp(position) > size_f` (\~3e12 with this buffer). Currently unreachable, but it *is* the stated defensive layer — use `fmod`/`isfinite`. Related: unguarded float→int casts in `ReadHermiteStereoFast` make a NaN grain position UB; one `isfinite` fence in `ComputeGrainParams` covers the whole path.

4. **[minor] "Kill the oldest grain" kills an arbitrary one** (`grain_engine.cpp`) — it marks the first non-pending grain by array index, which is frequently the *newest* (slot 0 is reused first). Under saturation, fresh grains die while old ones ring on. Track start order or pick the highest `envelope_phase_`.

5. **[minor] Scheduler dead state / mode-change glitch** (`grain_scheduler.cpp`): `clock_period_` is measured and smoothed but never read; `BeadsParameters::clock_interval_samples` is never read by anyone (host doesn't set it). `gate_phase_` doubles as the clock-division counter without reset on trigger-mode change, so the first division after switching gated→clocked can be off by one.

6. **[minor, uncertain] No FTZ/denormal measures** in the reverb/SVF/smoother tails. VCV sets FTZ on its engine threads and MetaModule firmware presumably sets FPSCR flush-to-zero (unverified), but the *test binary* doesn't — silent-tail CPU spikes on x86 are possible. Cheap insurance: quash values below 1e-20 in the reverb feedback.

7. **[nit] `NextGaussian()` isn't unit variance** (`random.h`): it's a CLT sum with σ ≈ 0.577 and hard range ±2, despite the comment. Only matters if spread math was designed in unit-sigma terms.

### Refactorings

- **Duplicated SIZE→duration mapping** — `grain_engine.cpp` implements the same boundary/exp2 mapping twice with subtly different inputs. Extract one helper; divergence here is a future bug.
- **`StateVariableFilter` triplication** (`svf.h`) — `ProcessLP/HP/BP` are identical except the return expression.
- **Quality-mode crossfade duplication** — `quality_processor.cpp` (two near-identical blocks).
- **Round-2 orphans (new):** `WavetableProvider` + `kWavetableSize` in `include/beads/types.h` (only consumer was the deleted wavetable module) and `RecordingBuffer::CopyRegionTo` (only caller was the deleted `GrainDTCCache::Prefetch`). One small dead-code sweep commit.
- `RecordingBuffer` read-path duplication: only `ReadHermiteStereoFast` is used in production; `ReadHermite`/`ReadHermiteStereo`/`ReadLinear` share identical guard/wrap code — demote to test-only or consolidate.
- **RT-hygiene nit (new):** `DecayGrainLed`'s `kDecay` and auto_gain's `kSilenceGain` are function-local `static const` with dynamic initializers — a guard-variable check per call and a potential one-time lock on the first audio callback. Hoist to member/namespace scope.
- **Nit (new):** the 48 kHz g-domain defaults (0.0491/0.0079) appear as bare literals in four places in `src/mf20/engine.hpp`; name them.

### DSP improvements

- **Dry path taps pre-auto-gain input** (`beads_processor.cpp`, dry capture — deliberate so DRY/WET = 0 equals bypass) — but with auto-gain boosting +32 dB, mid-knob mixes have a big dry/wet level mismatch; hardware Beads crossfades post-gain. Consider tapping post-gain, or document the trade-off.
- **Adaptive interpolation under load** — the old `use_linear` flag was removed in round 2 along with the DTC path (it was only honored by dead code). If CPU headroom under grain saturation ever matters, wire a fresh load-tier check into `Grain::Process`/`ProcessBlock` (the `render_load_tier_` computation still exists).
- **kMidi burst mode clumps** (`grain_scheduler.cpp`): up to 15 burst grains land inside one ≤1.3 ms block — effectively one thick grain. Spread the burst across blocks with a pending counter.
- **Reverb never sleeps** (`reverb.cpp` gates input at `amount_ == 0` but the full 12-delay-line tank runs forever). On the MetaModule budget, add an energy-based sleep once the tail decays.
- Grain-rate cap at 80 Hz (`grain_scheduler.h`) is a deliberate, documented retirement of the old high-rate path, but hardware Beads reaches higher perceived densities at full CW — revisit only if "faithful recreation" is the goal.
- The overlap-normalization "slow fall" coefficient (`grain_engine.cpp`) is 0.2/sample ≈ 5-sample time constant — not slow; if pumping is ever heard, it wants \~0.001.

### Test coverage gaps

- **Interpolation-tail sync** — no test writes at `write_head_ < kInterpolationTail` then reads fractionally across the `size_` boundary (the trickiest indexing in the buffer).
- **No scheduler tests for kClocked or kMidi modes** — kClocked is what the host actually selects when SEED is patched.
- **No NaN-robustness tests for the beads engine** (feed NaN CVs, assert finite output). Round 2 added NaN tests for MF-20 and LoopEngine; the beads engine itself still has none.
- **A `feedback = 0` control case** in `test_feedback_path.cpp` would make its single roughness threshold self-explanatory if future DSP tuning shifts the measured values.
- **Loooop sample-rate-change coverage** could be broadened: multi-head/non-default-speed and redundant same-rate calls are untested (round-2 review note).
- Stale test comments: `test_quality_modes.cpp` says 8 kHz tape LP (constant is 5 kHz); `test_reverb.cpp` cites 0.3 st wow (actual 0.02).

### Verified correct (don't re-investigate)

Interpolation-tail write mirroring and all tail-based index math; grain envelopes reach \~0 at both edges for all SHAPE zones; decimation ratios consistent; reverb partition fits its buffer and `fb ≤ 0.9375` with in-loop `SoftClip` is stable; no heap allocation or locks anywhere in the audio path; xorshift128 PRNG deterministic and fine. Block-size independence (256 vs 4×64 bit-identical), freeze seam smoothness, and feedback per-sample continuity are covered by tests. Round 2: the SoftClip swap in the auto-gain limiter deviates from `std::tanh` by ≤\~0.5% with an unchanged ceiling; the dry/wet coefficient cache was verified against first-block cold state.

---

## Plugin glue, build, metadata, tests

**All the high-risk checks pass** (verified by running both test lanes and diffing files): module slugs consistent across plugin.json, `createModel` calls, MetaModule info headers, and plugin-mm.json; the two plugin.json copies byte-identical with an auto-sync that prevents drift; all four modules registered in both targets; the beads_dsp sources present in both build systems; all referenced panels/faceplates exist; every spot-checked doc claim matches the code.

Remaining items, all minor:

1. **`plugin.json` lacks `sourceUrl`/`manualUrl`** — required (`sourceUrl`) for VCV Library submission; per-module `manualUrl` could point at the per-module .md files.
2. **The two Python guard tests aren't wired into any lane** — `tests/run.sh` doesn't run them and `tests/README.md` doesn't mention them, so the identity/no-delay guards will silently stop being run. Add `python3 -m unittest discover` to `tests/run.sh`.
3. **Object files escape `build/` into `vcv/src/`** because all sources are `../src/...`, so `make clean` leaves stale objects (gitignored, but a "clean" build isn't). Extend the clean target.
4. **Module list duplicated** between plugin.json and plugin-mm.json with nothing checking they agree — extend `test_robotboy_identity.py` to assert the two slug lists are equal.
5. **Version stated twice** (plugin.json and `project(RobotBoy VERSION 2.0.1)`) — the CMake one is informational and will drift; drop it or read it from plugin.json.
6. Nits: stale test-count comment in `tests/beads/CMakeLists.txt`; `-std=c++20` applied to all languages in `metamodule/CMakeLists.txt` (use `target_compile_features`); the Makefile's `cp` sync swallows failures; empty `presets/` directory that, if ever populated, wouldn't ship (not in `DISTRIBUTABLES`); `plugin-mm.json`'s `MetaModuleBrandSlug`/`displayName` keys unverified against the 4ms schema (likely fine, worth a one-time check).

---

## Still to do, by category

### Fixes

1. Loooop/Lop: "Initialize" (Ctrl+I) should clear the loop (`onReset` override). (§ Loooop #2)
2. MF-20: first `modulate()` delayed \~2.5 ms — saved K35 mode and non-48 kHz g targets apply late. (§ MF-20 #2)
3. MF-20: poly channel count from L input only. (§ MF-20 #1)
4. MF-20: reactivated voices carry stale ringing state. (§ MF-20 #3)
5. Loooop/Lop: seam interpolation wraps to `floor(winStart)`. (§ Loooop #1)
6. beads: grain-kill fallback fade 0.25-amplitude step. (§ beads #1)
7. beads: "kill the oldest grain" kills an arbitrary one. (§ beads #4)
8. beads: scheduler `gate_phase_` off-by-one on trigger-mode change + dead `clock_period_`/`clock_interval_samples` state. (§ beads #5)
9. Small hardening, as touched: beads defensive wrap loops → `fmod`/`isfinite` (§ beads #3); reverb `Init` buffer-size guard (§ beads #2); `CvDecimationForBlock` divide guard (§ Particules #3); silent allocation failure `WARN()` (§ Particules #1); Loooop/Lop short-circuited `||` trigger nit (§ Loooop #3); jitter-decrease one-extra-wrap nit (§ Loooop #4); same-rate `setSampleRate` early-out (§ Loooop #5); Particules.md doc mismatches (§ Particules #2); FTZ insurance in reverb feedback (§ beads #6).

### Performance improvements

1. **Batch the Particules VCV path** (block size 16–32 instead of 1) — the biggest remaining CPU item. (§ Particules DSP)
2. MF-20 drive smoothing (also a quality fix — zipper noise). (§ MF-20 #4)
3. Reverb energy-based sleep when idle. (§ beads DSP)
4. Magic-static hoists in `DecayGrainLed`/auto-gain (RT hygiene, one-time cost). (§ beads Refactorings)
5. Re-add adaptive interpolation under grain-saturation load, wired into the live path this time. (§ beads DSP)

### Refactors

1. Extract the \~80 duplicated lines between Loooop and Lop `process()`/menus. (§ Loooop Refactorings)
2. Round-2 orphan sweep: `WavetableProvider`/`kWavetableSize`, `RecordingBuffer::CopyRegionTo`. (§ beads Refactorings)
3. beads duplications: SIZE→duration mapping, `StateVariableFilter` triplication, quality-crossfade blocks, `RecordingBuffer` read-path consolidation. (§ beads Refactorings)
4. `particules_density_control.h` naming/includes; promote `control_conditioner.h` to `include/beads/`. (§ Particules Refactorings)
5. Name the MF-20 g-domain default constants. (§ beads Refactorings, nit)
6. Build/meta cleanups: python tests wired into `tests/run.sh`, clean target for `vcv/src` objects, slug-list parity check, single version source, `sourceUrl`/`manualUrl`, misc nits. (§ Plugin glue #1–6)

### New features

1. **Loooop overdub feedback/decay (sound-on-sound)** — the one feature looper users will go looking for; also bounds the unbounded overdub sum. (§ Loooop features)
2. **Particules scale-aware pitch lock** (engine hooks already exist). (§ Particules features)
3. MF-20 HP output jack (panel room permitting). (§ MF-20 features)
4. Particules grain-count-aware LED; input-level readout in the gain menu; context-menu undo. (§ Particules features)
5. DSP-quality options: Loooop cubic interpolation, overdub punch-in/out ramps, one-shot fade-out, smoothed level/pan/dry-wet, optional anti-aliasing above 1× speed; MF-20 clip-knee rounding / HQ oversampling; beads post-gain dry tap (or document), kMidi burst spreading. (§ Loooop/MF-20/beads DSP)

### Other

1. Test-coverage gaps: beads-engine NaN robustness, kClocked/kMidi scheduler tests, interpolation-tail sync, `feedback = 0` control case, broader Loooop SR-change cases, stale test comments. (§ beads test gaps)
2. Loooop display rebinning for short loops (quality-of-life). (§ Loooop Display)
3. Release checklist: verify `plugin-mm.json` keys against the 4ms schema; VCV Library submission needs `sourceUrl`.
