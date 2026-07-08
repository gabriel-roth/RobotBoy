# RobotBoy code review — July 8, 2026 (open items)

Full-repo review at commit `f222f8a`: all four modules (Loooop, Lop, MF-20, Particules), the vendored `beads_dsp` library, plugin glue/metadata/build files, and tests. Five independent review passes were run in parallel, and every critical/major finding was re-verified directly against the source.

**Update (July 8, 2026):** the five highest-priority findings were fixed on branch `code-review-fixes` (see `docs/superpowers/plans/2026-07-08-code-review-fixes-spec.md`): MF-20 K35 divergence, beads_dsp `Process()` out-of-bounds read, beads_dsp block-rate feedback hold, beads_dsp freeze-crossfade rewrite, Loooop jitter/crossfade discontinuity, and the three Particules wrapper issues (pitch CV quantization, per-block SEED sampling, conditioner timing). Those items have been removed from this document; what remains below is still open. One release note from the fixes: FEEDBACK on MetaModule was previously nearly inert and now regenerates properly — patches with feedback cranked will sound denser than before.

The plugin-level glue (slugs, registration, build parity, assets, docs) checked out unusually clean — only nits remain there.

---

## Remaining priorities, ranked

1. **Loooop/Lop [major]** — a sample-rate change erases the recorded loop and zero-fills \~23 MB of buffers mid-performance. (§ Loooop #1)
2. **MF-20 [major]** — no NaN recovery in filter state; one bad input sample silences the module until re-initialization. (§ MF-20 #1)
3. The minor NaN-hardening, UX, and CPU items below, as time allows.

---

## MF-20

### Bugs

1. **[major] No NaN/inf recovery in filter state.**
   `src/mf20/MF20Filter.hpp` (state updates `s1 = 2·x1_mid − s1` etc.). Any non-finite input (an upstream module emitting one NaN) permanently poisons `s1`/`s2`. **Fix:** per modulate-block guard, e.g. `if (!std::isfinite(lpStage.lp)) eng->reset();` — one branch per \~110 samples per voice, standard practice for feedback filters.

2. **[minor] Polyphony channel count taken from the L input only.**
   `src/mf20/MF20Filter.cpp:222`. A poly cable patched only into `AUDIO_INPUT_R` runs mono. **Fix:** `std::max({1, inputs[AUDIO_INPUT].getChannels(), inputs[AUDIO_INPUT_R].getChannels()})`.

3. **[minor] First `modulate()` is delayed \~2.5 ms after construction/patch load.**
   `src/mf20/MF20Filter.cpp:40, 228`. `_steps = -1` means modulate first runs at sample \~110; until then the filters run with default drive and **OTA mode even if the patch saved K35** (`dataFromJson` sets `_filterMode` but modes are only pushed in `modulate()`). **Fix:** initialize `_steps = _modulationSteps` so the first `process()` modulates immediately (the standard Bogaudio idiom).

4. **[minor] Voices activated by a channel-count increase carry stale state.**
   `src/mf20/engine.hpp:73-75`. A voice that rang at high resonance, went inactive, then reactivates emits its old ringing state. **Fix:** in `setVoices()`, reset engines in `[oldActive, newActive)` when the count grows.

5. **[minor] Drive is stepped at the 2.5 ms modulate rate with no smoothing.**
   `src/mf20/MF20Filter.cpp:114-115, 160-163, 180`. `_drive` (gain up to √8 ≈ 2.83×) and the clip threshold jump every \~110 samples → zipper noise when sweeping Drive with hot audio. **Fix:** a fifth `OnePoleSmoother` for drive, advanced per-sample.

6. **[minor, judgment call] K35's asymmetric input clip creates DC that the LP output passes.**
   `src/mf20/MF20Filter.hpp` (forward-path clip, T_neg = 0.85). Hot input + high drive in K35 puts a DC offset on the LP outputs. The hardware arguably does this too; if unwanted, a one-pole DC blocker on the LP output fixes it.

7. **[nit] Cutoff floor is 20 Hz even under CV** (`MF20Filter.cpp:196-197`) though the core supports 1 Hz — deep negative CV can't close the filter the way an MS-20 can. Intentional? If not, lower the clamp.

### Refactorings

- `src/mf20/engine.hpp:56-71` — `VoiceEngine* engines[16]` heap indirection is dead weight: all 16 are allocated in the constructor and never freed until destruction. A by-value array removes `new`/`delete`, every null check, and improves cache behavior on MetaModule.
- Dead code: `EnginePool::processVoice()` (unused; the "used by tests" comment is stale) and `resetAll()` (unused while `onReset` reimplements it inline). Also `EnginePool::sampleRate` is stored but never read.
- `(1.f + g) * (1.f + g)` computed twice per sample in the OTA path; hoist.

### DSP improvements

- **Per-sample transcendentals dominate MetaModule CPU:** per voice per sample there are 2× `std::pow(2.f, x)`, up to 4× `std::tan`, `std::sqrt(_drive)` recomputed inside a per-sample lambda, and RNG calls — \~1.5M pow+tan/sec at 16 voices. In order of effort: hoist `sqrt(_drive)` to `modulate()`; replace `pow(2,x)` with `rack::dsp::approxExp2_taylor5`; either approximate `tan` with a Padé polynomial or compute `g` at modulate rate and slew `g` itself (moves both transcendentals out of the audio loop entirely).
- **RNG dither → deterministic constant.** The ±1e-6 V dither (`MF20Filter.cpp:187, 209`) costs cycles and breaks bit-reproducibility between VCV and MetaModule builds (relevant to headless comparison testing). An alternating ±1e-9 constant is cheaper and deterministic.
- **Hard clip knee aliases** at high drive/cutoff. Rounding the knee with a small quadratic transition region kills the worst high-order harmonics for a few ops; 2× oversampling of the core could be a VCV-only context-menu "HQ" option.

### Low-complexity features

- The core already computes HP and BP (`struct Out { lp, bp, hp; }`) but only the cascade LP is exposed. One HP-stage output jack would give MS-20-style band-reject/HP patching for near-zero DSP cost — only if there's panel room.

### Verified correct (don't re-investigate)

TPT algebra in both modes (re-derived), OTA region classification, V/oct CV math, `onSampleRateChange` propagation, JSON round-trip, denormal avoidance via input dither. The K35 loop clip added by the fix branch was verified algebraically identical to the old math in the linear region, with a guarded fallback for the bistable band (k > 8/3 at fc ≈ 12–16 kHz).

---

## Loooop / Lop

### Bugs

1. **[major] Sample-rate change erases the recorded loop and reallocates \~23 MB.**
   `src/loooop/Loooop.cpp:84-86`, `src/loooop/Lop.cpp:58-60` → `LoopEngine::reset` (`LoopEngine.cpp:13-14`) does `bufL_.assign(...)`/`bufR_.assign(...)` and zeroes `loopLen_`. Changing Rack's engine sample rate mid-performance destroys the loop with no warning, and the zero-fill is the same class of stall that the `clear()` comment says crashed a MetaModule patch.
   **Fix (minimal):** split "reallocate" from "retune" — on SR change recompute only `xfadeSamples_` (and the min-window length) and keep the buffer; the loop plays back repitched, far less destructive than erasure. If reallocation is wanted, only when `maxSamples_` must grow, never audio-adjacent.

2. **[minor] Seam interpolation wraps to `floor(winStart)` instead of the fractional window start.**
   `LoopEngine.cpp` (`readInterpolated`, the `i1` wrap) — the cast truncates, so with crossfade off and a fractional window the last read before the wrap interpolates toward the wrong sample (up to half a step of error → small extra click on seam-exact windowed sub-loops). **Fix:** interpolate the wrap target at fractional `winStart` (two reads + lerp).

3. **[minor] "Initialize" (Ctrl+I) resets knobs but leaves the recorded loop playing.**
   Neither module overrides `onReset`. **Fix:** `void onReset(const ResetEvent& e) override { Module::onReset(e); engine.clear(); }` in both (`clear()` is already audio-safe).

4. **[minor, judgment call] A NaN on the audio input is recorded into the loop** and, with overdub (`bufL_[writeIdx_] += inL`), persists until Clear. The param-CV paths are safe (Rack's `clamp` converts NaN to a bound) and reads can't go out of bounds. **Fix (cheap):** `if (!std::isfinite(inL)) inL = 0.f;` at the module boundary.

5. **[nit] Short-circuited `||`** between button and CV Schmitt triggers (`Loooop.cpp:91-96`, `Lop.cpp:65-70`) merges same-sample events and skips one sample of the CV trigger's processing. Evaluate both into locals first.

6. **[nit, from the fix branch's final review] A jitter-amount decrease takes one extra wrap to apply.** `setJitter` rescales only the *next* roll; dropping jitter from 1.0 to 0.05 lets one already-pre-rolled window (offset up to ±0.5) play first. Not a click — preview and commit stay consistent. Fix if desired: clamp `jitterNext` on amount decrease.

### Refactorings

- **\~80 lines of `process()` and the context menu are duplicated line-for-line between Loooop and Lop** (`Loooop.cpp:88-134` vs `Lop.cpp:62-104`; Lop is literally the head-0 body). Any fix must currently be made twice. Extract shared free functions (`loopProcessGlobalControls`, `loopProcessHeadControls`) and a shared menu builder. No behavior change.
- **`windowBounds` runs 4× per head per sample**, each time re-deriving `std::ceil(sampleRate_ * MINIMUM_LOOP_MILLISECONDS / 1000.0)` — 16 redundant ceil+divide chains per sample for Loooop on a Cortex-A7. Precompute `minWinLen_` in `reset()`; compute the window once per head per sample and pass it down.

### DSP improvements

- **Overdub punch-out records a permanent step into the loop** (`LoopEngine.cpp`, overdub write path): input sums at full gain from the first sample and stops dead at `toggleRecord()`; punch-out lands mid-loop where the seam crossfade can't mask it, clicking on every subsequent pass. **Fix:** ramp an overdub-input gain 0→1 over `xfadeSamples_` at start and 1→0 at stop (defer `recording_ = false` by the ramp length). One multiply per sample while recording.
- **Linear → 4-point cubic (Hermite) interpolation** (`readInterpolated`/`readRaw`). At fractional speeds and V/oct melodies, linear interpolation costs audible HF and adds intermodulation. Cubic is \~4 reads + a few MACs per channel — the single biggest fidelity win available, still modest on MetaModule.
- **Head level/pan/dry-wet are applied unsmoothed** (`LoopEngine.cpp` head output, `Loooop.cpp:141-145`): a square LFO into Level CV produces hard steps. A 1–5 ms one-pole on each removes it for a few ops.
- **One-shots end with a hard cut** (`fadeLen` returns 0 for one-shots; `advanceHead` stops dead). A short gain-ramp fade-out over the last `xfadeSamples_` (no second read needed) de-clicks the spec's "rhythmic re-slicer" patch.
- No anti-aliasing above 1× speed (speed 2 folds everything above SR/4). A proper fix isn't cheap; a pragmatic half-measure is averaging two reads spaced `sp/2` when `|speed| > 1`. Optional.
- Constant-power pan would be nicer than linear balance, but note the center-unity law is load-bearing for the "four heads at 0.25 sum to unity" default (`Loooop.cpp:45-47`).

### Display

- No defects found. One quality note: `peakBinSize_` is fixed by `maxSamples_`, so a 1 s loop uses \~68 of 4096 bins stretched across the display — blocky. Rebinning the peaks over `loopLen_` once at freeze time (off the per-sample path) fixes it.

### Low-complexity features

- **Overdub feedback/decay (sound-on-sound):** `buf = buf * feedback + in` instead of `buf += in`. One multiply per sample while recording plus one context-menu param; gives the classic Frippertronics decay *and* bounds the currently unbounded overdub sum. This is the one feature looper users will actually go looking for.
- Considered and rejected as *not* low-complexity: overdub phase alignment (writes always start at index 0 — fixing it needs a canonical loop-phase clock; explicitly marked "v1" in the code), undo-overdub (a second 23 MB buffer), saving the loop with the patch.

### Verified correct (don't re-investigate)

`clear()` not zeroing the buffer is safe; all division-by-zero paths guarded; audio↔GUI snapshot uses atomics with the non-atomic peak reads documented and bounded; no dataToJson needed (all persistent state lives in params); every CV scaling matches the spec's "±10 V spans the range" claims. Jitter + crossfade continuity is now covered by a test (forward wraps; the reverse path was verified by review trace but has no automated test).

---

## Particules (wrapper)

### Bugs

1. **[minor] Freeze light ignores CV-driven freeze** (`Particules.cpp:378` uses only the button; use `frozen` from line 315). The manual says the light shows "when freeze is active."

2. **[minor] Quality LED colors don't match the manual** — code (`plugin.hpp:27-29`): white/green/yellow/red; manual: white/**cyan**/amber/**magenta**. Fix one side.

3. **[minor] Gate comparators have no hysteresis** (`Particules.cpp` — plain `> 1.f` for FREEZE and SEED). A noisy gate crossing 1 V chatters freeze (audible buffer glitching) or floods clocked mode with spurious edges. Use Schmitt thresholds per the VCV standard (high ≥ 1 V, low ≤ 0.1 V).

4. **[minor] FREEZE and QUALITY are `configParam`, so patch Randomize hits them** — Ctrl+R randomly latches freeze and phantom-presses quality. Use `configButton`/`configSwitch` (which disable randomization).

5. **[minor] `manualGainDb` not clamped on JSON load** (`Particules.cpp:243-244`) — a hand-edited patch can inject 500 dB. Clamp to [0, 32].

6. **[minor] `onReset` doesn't clear the engine's audio state** (`Particules.cpp:195-207`) — the 4-second buffer, feedback path, and reverb tail survive "Initialize" (`stereo_input_` is also left stale). Call `processor_.ClearBuffer()` and reset `stereo_input_`.

7. **[minor] No NaN guard on audio input feeding a feedback recorder** (`Particules.cpp:351-353`) — with feedback up, one upstream NaN poisons buffer, reverb, and auto-gain until the user finds "Clear buffer". Two `isfinite` branches per sample fix it.

8. **[nit] Allocation failure is silent** (`Particules.cpp:160-168`) — the engine null-guards everything (verified), so it degrades to silence, but a `WARN()` would help debugging.

9. **[nit] Doc mismatches in Particules.md:** SEED input described as "bottom right" but placed top-right; the quality-mode table states fixed rates (48/32/24/24 kHz) but the engine applies decimation factors 1/2/8/4 to the *host* rate (`types.h:37-45`) — 48/24/6/12 kHz at a 48 kHz host, different again at 96 kHz.

10. **[nit, from the fix branch's review] `CvDecimationForBlock` divides `8 / block_size` unguarded** (`particules_cv_conditioning.h`) — UB at 0, unreachable today (compile-time constants 1 and 64). A one-line guard is cheap future hardening.

### Refactorings

- `particules_block_runtime.h:17` — redundant `output_index_ = input_index_ + 1;` in `PushInputSample` (already advanced in lockstep by `ReadOutputSample`; transiently sets an out-of-range value that's never dereferenced but is a trap). Remove.
- `Particules.cpp:104` — dead member `grain_led_`. Delete.
- `particules_block_runtime.h:73` — `std::pow(0.9999f, BlockSize)` recomputed every call; in VCV that's a per-sample `powf` for a constant. Make it `static const`.
- `particules_density_control.h` — takes *raw* voltage but names the param `conditioned_density_cv`, and includes three headers it doesn't use. Rename or inline the `* 0.2f`.
- `Particules.cpp:3` includes a vendored-library internal (`src/util/control_conditioner.h`); promote it to `include/beads/`.
- `Particules.hpp` is an empty file; delete.

### DSP improvements

- **Batch the VCV path.** With `kWrapperBlockSize = 1`, every VCV sample pays full `updateSlowParams()` (\~12 `getVoltage()` calls, 4 conditioner steps), a whole-struct `SetParameters()`, and per-call engine setup. A 16–32 sample VCV block amortizes all of it for \~0.3–0.7 ms latency (the engine mixes dry internally, so no comb risk — feedback and dry/wet are already smoothed engine-side) and further narrows VCV/MM behavioral differences.
- **Grain trigger pulses merge on retrigger** (`particules_block_runtime.h:53-55`) — two grains < 1 ms apart produce one continuous high. Force one low sample between pulses so downstream counters see one edge per grain.

### Low-complexity features

- **Scale-aware pitch lock:** the engine already exposes `LoadScale`/`ClearScale`/`SetScaleRoot` (`beads.h:49-51`) and the wrapper already has the "Lock pitch" submenu and JSON field. "Chromatic / Major / Minor pentatonic" entries are a few tables plus one call — high musical value, zero panel clutter.
- **Grain-count-aware LED:** `ActiveGrainCount()` (`beads.h:41`) is unused; scaling the grain light by active count makes Density visually legible (the current flash-and-2-second-decay reads as a fade, not the per-grain flash the manual promises).
- **Input level readout in the gain menu:** `InputLevel()` (`beads.h:43`) is unused; showing it next to "Manual gain" makes gain staging without auto-gain far less blind.
- **Undo for context-menu options:** the seed/pitch-lock/grain-trigger/gain settings bypass VCV's history — Ctrl+Z does nothing. `history::ModuleChange` wrapping is boilerplate-cheap (VCV-only).

### Verified correct (don't re-investigate)

Block-runtime read-before-push ordering and indexing at both block sizes; the pitch notch map; engine null-guards after failed `Init`; `GetMemoryRequirements` genuinely sample-rate-independent today (the re-`Init` in `onSampleRateChange` is safe, but assert the size to future-proof). SEED latching, pitch CV pass-through, and block-aware conditioner timing are now covered by tests.

---

## beads_dsp (vendored library)

### Bugs

1. **[minor] Grain kill fallback fade never reaches zero** (`grain.h:309-338`): `fade = counter/4` runs 1.0, 0.75, 0.5, 0.25 then cuts — a 0.25-amplitude step (audible tick when the pool saturates on DC-heavy content). Use `(counter − 1)/kFallbackFadeSamples`.

2. **[minor] Reverb `DelayLine`s would dereference null on undersized buffers** (`fx_engine.h:21-29`, `reverb.cpp:33-41`) — unreachable today (needs ≈ 11,996 of 16,384 floats), but `Reverb::kMinBufferSize` is declared and never checked. Guard `Init`.

3. **[minor] Defensive wrap loops can hang on huge finite positions** (`recording_buffer.cpp:104-107` etc.): `while (position >= size_f) position -= size_f;` never terminates once `ulp(position) > size_f` (\~3e12 with this buffer). Currently unreachable, but it *is* the stated defensive layer — use `fmod`/`isfinite`. Related: unguarded float→int casts in `ReadHermiteStereoFast` and the DTC cache make a NaN grain position UB; one `isfinite` fence in `ComputeGrainParams` covers the whole path.

4. **[minor] "Kill the oldest grain" kills an arbitrary one** (`grain_engine.cpp:61-72`) — it marks the first non-pending grain by array index, which is frequently the *newest* (slot 0 is reused first). Under saturation, fresh grains die while old ones ring on. Track start order or pick the highest `envelope_phase_`.

5. **[minor] Scheduler dead state / mode-change glitch** (`grain_scheduler.cpp:122-131, 146`): `clock_period_` is measured and smoothed but never read; `BeadsParameters::clock_interval_samples` is never read by anyone (host doesn't set it). `gate_phase_` doubles as the clock-division counter without reset on trigger-mode change, so the first division after switching gated→clocked can be off by one.

6. **[minor, uncertain] No FTZ/denormal measures** in the reverb/SVF/smoother tails. VCV sets FTZ on its engine threads and MetaModule firmware presumably sets FPSCR flush-to-zero (unverified), but the *test binary* doesn't — silent-tail CPU spikes on x86 are possible. Cheap insurance: quash values below 1e-20 in the reverb feedback.

7. **[nit] `NextGaussian()` isn't unit variance** (`random.h:43-51`): it's a CLT sum with σ ≈ 0.577 and hard range ±2, despite the comment. Only matters if spread math was designed in unit-sigma terms.

### Refactorings

- **Duplicated SIZE→duration mapping** — `grain_engine.cpp:86-101` and `:205-216` implement the same boundary/exp2 mapping twice with subtly different inputs. Extract one helper; divergence here is a future bug.
- **`StateVariableFilter` triplication** (`svf.h:33-60`) — `ProcessLP/HP/BP` are identical except the return expression.
- **Quality-mode crossfade duplication** — `quality_processor.cpp:66-79` vs `:130-143`.
- **Dead code:** `EqualPowerCrossfade` and `FastPowUnit` (`dsp_utils.h`) have no callers; `Saturation::Process` is test-only; the **entire wavetable module** (`src/wavetable/`) is referenced by nothing; the **entire DTC path** (`GrainDTCCache`, `ProcessBlockCached`, the 5-arg `Init`) is never used by this repo's host and has zero test coverage — keep only if it serves another port, else delete; `BeadsParameters::seed_connected` and `stereo_input` are set by the host but never read.
- `RecordingBuffer` read-path duplication: only `ReadHermiteStereoFast` is used in production; `ReadHermite`/`ReadHermiteStereo`/`ReadLinear` share identical guard/wrap code — demote to test-only or consolidate.

### DSP improvements

- **Dry path taps pre-auto-gain input** (`beads_processor.cpp`, dry capture — deliberate so DRY/WET = 0 equals bypass) — but with auto-gain boosting +32 dB, mid-knob mixes have a big dry/wet level mismatch; hardware Beads crossfades post-gain. Consider tapping post-gain, or document the trade-off.
- **Adaptive interpolation is inert where it's needed** (`grain_engine.cpp:241`): `use_linear` is set under load but only `ProcessBlockCached` honors it; the direct path (the only one VCV uses) always calls Hermite. Wire `use_linear_` into `Grain::Process`/`ProcessBlock`.
- **kMidi burst mode clumps** (`grain_scheduler.cpp:211-221`): up to 15 burst grains land inside one ≤1.3 ms block — effectively one thick grain. Spread the burst across blocks with a pending counter.
- **Reverb never sleeps** (`reverb.cpp:126` gates input at `amount_ == 0` but the full 12-delay-line tank runs forever). On the MetaModule budget, add an energy-based sleep once the tail decays.
- `AutoGain::SoftLimit` uses `std::tanh` per channel (`auto_gain.cpp:35`) while `FastTanh` exists and is used elsewhere; `FastDbToGain(kMinGainDb)` recomputed every locked sample (`:118`) — hoist.
- `std::pow(0.998f, block)` per block (`beads_processor.cpp`, dry/wet smoothing) is a per-sample `pow` in the VCV build; special-case `block == 1` or precompute.
- Grain-rate cap at 80 Hz (`grain_scheduler.h:24`) is a deliberate, documented retirement of the old high-rate path, but hardware Beads reaches higher perceived densities at full CW — revisit only if "faithful recreation" is the goal.
- The overlap-normalization "slow fall" coefficient (`grain_engine.cpp:288-296`) is 0.2/sample ≈ 5-sample time constant — not slow; if pumping is ever heard, it wants \~0.001.

### Test coverage gaps

- **Interpolation-tail sync** — no test writes at `write_head_ < kInterpolationTail` then reads fractionally across the `size_` boundary (the trickiest indexing in the buffer).
- **No scheduler tests for kClocked or kMidi modes** — kClocked is what the host actually selects when SEED is patched.
- **No NaN-robustness tests** (feed NaN CVs, assert finite output).
- The DTC path has zero coverage (moot if deleted).
- **A `feedback = 0` control case** in `test_feedback_path.cpp` would make its single roughness threshold self-explanatory if future DSP tuning shifts the measured values.
- Stale test comments: `test_quality_modes.cpp:44` says 8 kHz tape LP (constant is 5 kHz); `test_reverb.cpp` cites 0.3 st wow (actual 0.02).

### Verified correct (don't re-investigate)

Interpolation-tail write mirroring and all tail-based index math; DTC prefetch wrap/margin arithmetic; grain envelopes reach \~0 at both edges for all SHAPE zones; decimation ratios consistent; reverb partition fits its buffer and `fb ≤ 0.9375` with in-loop `SoftClip` is stable; no heap allocation or locks anywhere in the audio path; xorshift128 PRNG deterministic and fine. Block-size independence (256 vs 4×64 bit-identical), freeze seam smoothness, and feedback per-sample continuity are now covered by tests.

---

## Plugin glue, build, metadata, tests

**All the high-risk checks pass** (verified by running both test lanes and diffing files): module slugs consistent across plugin.json, `createModel` calls, MetaModule info headers, and plugin-mm.json; the two plugin.json copies byte-identical with an auto-sync that prevents drift; all four modules registered in both targets; the 13 beads_dsp sources present in both build systems; all referenced panels/faceplates exist; every spot-checked doc claim matches the code.

Remaining items, all minor:

1. **`plugin.json` lacks `sourceUrl`/`manualUrl`** — required (`sourceUrl`) for VCV Library submission; per-module `manualUrl` could point at the per-module .md files.
2. **The two Python guard tests aren't wired into any lane** — `tests/run.sh` doesn't run them and `tests/README.md` doesn't mention them, so the identity/no-delay guards will silently stop being run. Add `python3 -m unittest discover` to `tests/run.sh`.
3. **Object files escape `build/` into `vcv/src/`** because all sources are `../src/...`, so `make clean` leaves stale objects (gitignored, but a "clean" build isn't). Extend the clean target.
4. **Module list duplicated** between plugin.json and plugin-mm.json with nothing checking they agree — extend `test_robotboy_identity.py` to assert the two slug lists are equal.
5. **Version stated twice** (plugin.json and `project(RobotBoy VERSION 2.0.1)`) — the CMake one is informational and will drift; drop it or read it from plugin.json.
6. Nits: stale test-count comment in `tests/beads/CMakeLists.txt:19`; `-std=c++20` applied to all languages in `metamodule/CMakeLists.txt:50` (use `target_compile_features`); the Makefile's `cp` sync swallows failures; empty `presets/` directory that, if ever populated, wouldn't ship (not in `DISTRIBUTABLES`); `plugin-mm.json`'s `MetaModuleBrandSlug`/`displayName` keys unverified against the 4ms schema (likely fine, worth a one-time check).

---

## Suggested order of work

1. **Loooop/Lop sample-rate-change handling** — the remaining data-loss bug.
2. **MF-20 NaN recovery** — the remaining robustness bug.
3. NaN input guards (Loooop, Particules) and the Particules UX minors (freeze light, LED colors, Schmitt gates, configButton, onReset).
4. The MetaModule CPU items (MF-20 transcendental hoisting is the biggest win) and the dead-code sweeps.
5. Low-complexity features as desired: overdub feedback for Loooop, scale-aware pitch lock for Particules, HP output for MF-20.
