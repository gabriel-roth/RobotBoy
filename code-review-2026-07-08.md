# RobotBoy code review — July 8, 2026

Full-repo review at commit `f222f8a` (clean working tree): all four modules (Loooop, Lop, MF-20, Particules), the vendored `beads_dsp` library, plugin glue/metadata/build files, and tests. Five independent review passes were run in parallel (one per domain), and every critical/major finding below was then re-verified directly against the source; those are marked **[verified]**. Findings the reviewers flagged as uncertain are marked as such.

**Headline:** the plugin-level glue (slugs, registration, build parity, assets, docs) is unusually clean — no action needed there beyond nits. The real findings are in the DSP: one filter-destabilizing bug in MF-20's K35 mode, a memory-safety bug in the Beads processor that taints most of its test suite, a feedback path that behaves differently on MetaModule vs VCV, a freeze de-clicker whose math is inverted, a jitter/crossfade interaction in Loooop that produces the exact click it's meant to prevent, and a pitch-CV quantizer that breaks Particules' documented 1 V/oct tracking.

---

## Top priorities, ranked

1. **MF-20 [critical, verified]** — K35 mode diverges to NaN at resonance knob settings above \~84%. (§ MF-20 #1)
2. **beads_dsp [critical, verified]** — `Process()` reads out of bounds for `num_frames > 64`; the test suite runs at 256 frames, so most processor-level test results are undefined behavior. (§ beads_dsp #1)
3. **beads_dsp [major, verified]** — the feedback path is a block-rate zero-order hold, so FEEDBACK sounds wrong (and different) on MetaModule vs VCV. (§ beads_dsp #2)
4. **beads_dsp [major, verified]** — the freeze crossfade is inverted in both directions; the freeze de-click feature is effectively a no-op that sometimes *adds* a click. (§ beads_dsp #3)
5. **Loooop [major, verified]** — jitter defeats the seam crossfade: the fade previews the old window, playback resumes in the new one — a full-amplitude discontinuity every repeat in the granular use case. (§ Loooop #1)
6. **Particules [major, verified]** — Pitch CV is quantized to 0.05 V (0.6 semitone), contradicting the documented 1 V/oct tracking. (§ Particules #1)
7. **Loooop/Lop [major]** — a sample-rate change erases the recorded loop and zero-fills \~23 MB of buffers. (§ Loooop #2)
8. **Particules [major]** — on MetaModule, SEED triggers are sampled once per 64-sample block; standard 1 ms triggers are dropped about a quarter of the time. (§ Particules #2)
9. **MF-20 [major]** — no NaN recovery in filter state; one bad input sample silences the module until re-initialization. (§ MF-20 #2)

---

## MF-20

### Bugs

1. **[critical, verified] K35 mode diverges to ±inf and then NaN above \~84% resonance.**
   `src/mf20/MF20Filter.hpp:171-211`, `src/mf20/dsp_utils.hpp:20-22`, `src/mf20/MF20Filter.cpp:199`.
   The K35 nonlinearity clips only the *input* (forward path); the resonant loop itself is purely linear in the states. The module applies `resTaper()` — `(2r − r²) × 1.025`, max 1.025 — before calling the filter, so at full resonance `k = 1.025 × 8/3 ≈ 2.733 > 8/3`, i.e. negative damping in a linear loop → unbounded exponential growth. The taper exceeds 1.0 for knob values above \~0.844. Confirmed by simulation of the exact code path: with a 0.2-amplitude sine at 1 kHz cutoff, 48 kHz, any knob setting ≥ 0.85 diverges (0.90–1.00 → NaN); 0.84 stays bounded at 0.25 peak. Once NaN, `s1`/`s2` are poisoned forever. This contradicts MF20.md's "both self-oscillate cleanly at maximum resonance"; OTA mode at the same settings stays bounded (its clip is *inside* the solve).
   *Why tests missed it:* `test_k35_self_oscillation` calls `process()` with `res = 1.0` directly, bypassing `resTaper` (k = 8/3 exactly, marginally stable), and the extreme-resonance test only exercises OTA.
   **Fix:** put a saturating nonlinearity inside the K35 resonance loop — clip the `k·x₁` feedback term with the same piecewise-linear clip, solved region-wise exactly as `processOTA` already does (this is also what the real Korg35 does; the loop transistor clips). A stopgap clamp of `k ≤ 8/3 − ε` prevents the blowup but kills the promised self-oscillation. Add a K35 test at `res = resTaper(1.f)`.

2. **[major] No NaN/inf recovery in filter state.**
   `src/mf20/MF20Filter.hpp:165-166, 207-208`. Any non-finite input (an upstream module emitting one NaN, or bug #1) permanently poisons `s1`/`s2`. **Fix:** per modulate-block guard, e.g. `if (!std::isfinite(lpStage.lp)) eng->reset();` — one branch per \~110 samples per voice, standard practice for feedback filters.

3. **[minor] Polyphony channel count taken from the L input only.**
   `src/mf20/MF20Filter.cpp:222`. A poly cable patched only into `AUDIO_INPUT_R` runs mono. **Fix:** `std::max({1, inputs[AUDIO_INPUT].getChannels(), inputs[AUDIO_INPUT_R].getChannels()})`.

4. **[minor] First `modulate()` is delayed \~2.5 ms after construction/patch load.**
   `src/mf20/MF20Filter.cpp:40, 228`. `_steps = -1` means modulate first runs at sample \~110; until then the filters run with default drive and **OTA mode even if the patch saved K35** (`dataFromJson` sets `_filterMode` but modes are only pushed in `modulate()`). **Fix:** initialize `_steps = _modulationSteps` so the first `process()` modulates immediately (the standard Bogaudio idiom).

5. **[minor] Voices activated by a channel-count increase carry stale state.**
   `src/mf20/engine.hpp:73-75`. A voice that rang at high resonance, went inactive, then reactivates emits its old ringing state. **Fix:** in `setVoices()`, reset engines in `[oldActive, newActive)` when the count grows.

6. **[minor] Drive is stepped at the 2.5 ms modulate rate with no smoothing.**
   `src/mf20/MF20Filter.cpp:114-115, 160-163, 180`. `_drive` (gain up to √8 ≈ 2.83×) and the clip threshold jump every \~110 samples → zipper noise when sweeping Drive with hot audio. **Fix:** a fifth `OnePoleSmoother` for drive, advanced per-sample.

7. **[minor, judgment call] K35's asymmetric clip creates DC that the LP output passes.**
   `src/mf20/MF20Filter.hpp:186-193`. Hot input + high drive in K35 puts a DC offset on the LP outputs. The hardware arguably does this too; if unwanted, a one-pole DC blocker on the LP output fixes it.

8. **[nit] Cutoff floor is 20 Hz even under CV** (`MF20Filter.cpp:196-197`) though the core supports 1 Hz — deep negative CV can't close the filter the way an MS-20 can. Intentional? If not, lower the clamp.

9. **[test bug] `tests/mf20/test_mf20.cpp:98-115` (`test_zero_io`):** `buf[0]` is overwritten per (fc, res) combo, so only the last combo is actually asserted. Fix the accumulator (`allOk &= ok`).

### Refactorings

- `src/mf20/engine.hpp:56-71` — `VoiceEngine* engines[16]` heap indirection is dead weight: all 16 are allocated in the constructor and never freed until destruction. A by-value array removes `new`/`delete`, every null check, and improves cache behavior on MetaModule.
- Dead code: `EnginePool::processVoice()` (unused; the "used by tests" comment is stale) and `resetAll()` (unused while `onReset` reimplements it inline). Also `EnginePool::sampleRate` is stored but never read.
- `(1.f + g) * (1.f + g)` computed twice per sample in the OTA path; hoist.

### DSP improvements

- **Per-sample transcendentals dominate MetaModule CPU:** per voice per sample there are 2× `std::pow(2.f, x)`, up to 4× `std::tan`, `std::sqrt(_drive)` recomputed inside a per-sample lambda, and RNG calls — \~1.5M pow+tan/sec at 16 voices. In order of effort: hoist `sqrt(_drive)` to `modulate()`; replace `pow(2,x)` with `rack::dsp::approxExp2_taylor5`; either approximate `tan` with a Padé polynomial or compute `g` at modulate rate and slew `g` itself (moves both transcendentals out of the audio loop entirely).
- **RNG dither → deterministic constant.** The ±1e-6 V dither (`MF20Filter.cpp:187, 209`) costs cycles and breaks bit-reproducibility between VCV and MetaModule builds (relevant to headless comparison testing). An alternating ±1e-9 constant is cheaper and deterministic.
- **Hard clip knee aliases** at high drive/cutoff. Rounding the knee with a small quadratic transition region kills the worst high-order harmonics for a few ops; 2× oversampling of the core could be a VCV-only context-menu "HQ" option.
- The K35 loop-saturation fix for bug #1 is also the biggest quality win: self-oscillation becomes a clean, input-interactive sine instead of relying on marginal stability.

### Low-complexity features

- The core already computes HP and BP (`struct Out { lp, bp, hp; }`) but only the cascade LP is exposed. One HP-stage output jack would give MS-20-style band-reject/HP patching for near-zero DSP cost — only if there's panel room.
- Verified correct, for the record: TPT algebra in both modes (re-derived), OTA region classification, V/oct CV math, `onSampleRateChange` propagation, JSON round-trip, denormal avoidance via input dither.

---

## Loooop / Lop

### Bugs

1. **[major, verified] Jitter defeats the seam crossfade.**
   `src/loooop/dsp/LoopEngine.cpp:181-204` (`readHead`) vs `:218-224` (`advanceHead`).
   During the fade, `readHead` previews the incoming head from the *current* window (`windowBounds` uses the current `h.jitterOff`); `rollJitter(h)` runs only at the actual wrap, after which the resume position uses the *new* bounds. With Jitter > 0 and crossfade on (the default), every repeat smoothly fades in audio from the old window's start, reaches full gain at the seam — and the next sample plays from the new, randomly relocated window: a full-amplitude discontinuity at every repeat, exactly in the "granular cloud" use case Loooop.md advertises. With jitter = 0 the math is continuous.
   **Fix:** decide the next window before the fade begins — roll and cache `nextJitterOff` when a head enters the fade region (or at the previous wrap), preview from the *next* window's start, commit the cached offset at the wrap. Affects both VCV and MetaModule cores. Add a test: record a ramp, jitter = 1, crossfade on, assert bounded sample-to-sample deltas (there is currently no test covering jitter + crossfade together).

2. **[major] Sample-rate change erases the recorded loop and reallocates \~23 MB.**
   `src/loooop/Loooop.cpp:84-86`, `src/loooop/Lop.cpp:58-60` → `LoopEngine::reset` (`LoopEngine.cpp:13-14`) does `bufL_.assign(...)`/`bufR_.assign(...)` and zeroes `loopLen_`. Changing Rack's engine sample rate mid-performance destroys the loop with no warning, and the zero-fill is the same class of stall that the `clear()` comment (`LoopEngine.cpp:52-57`) says crashed a MetaModule patch.
   **Fix (minimal):** split "reallocate" from "retune" — on SR change recompute only `xfadeSamples_` (and the precomputed min-window length below) and keep the buffer; the loop plays back repitched, far less destructive than erasure. If reallocation is wanted, only when `maxSamples_` must grow, never audio-adjacent.

3. **[minor] Seam interpolation wraps to `floor(winStart)` instead of the fractional window start.**
   `LoopEngine.cpp:142` — the cast truncates, so with crossfade off and a fractional window the last read before the wrap interpolates toward the wrong sample (up to half a step of error → small extra click on seam-exact windowed sub-loops). **Fix:** interpolate the wrap target at fractional `winStart` (two reads + lerp).

4. **[minor] "Initialize" (Ctrl+I) resets knobs but leaves the recorded loop playing.**
   Neither module overrides `onReset`. **Fix:** `void onReset(const ResetEvent& e) override { Module::onReset(e); engine.clear(); }` in both (`clear()` is already audio-safe).

5. **[minor, judgment call] A NaN on the audio input is recorded into the loop** and, with overdub (`bufL_[writeIdx_] += inL`), persists until Clear. The param-CV paths are safe (Rack's `clamp` converts NaN to a bound) and reads can't go out of bounds. **Fix (cheap):** `if (!std::isfinite(inL)) inL = 0.f;` at the module boundary.

6. **[nit] Short-circuited `||`** between button and CV Schmitt triggers (`Loooop.cpp:91-96`, `Lop.cpp:65-70`) merges same-sample events and skips one sample of the CV trigger's processing. Evaluate both into locals first.

### Refactorings

- **\~80 lines of `process()` and the context menu are duplicated line-for-line between Loooop and Lop** (`Loooop.cpp:88-134` vs `Lop.cpp:62-104`; Lop is literally the head-0 body). Any fix — including the Schmitt nit above — must currently be made twice. Extract shared free functions (`loopProcessGlobalControls`, `loopProcessHeadControls`) and a shared menu builder. No behavior change.
- **`windowBounds` runs 4× per head per sample**, each time re-deriving `std::ceil(sampleRate_ * MINIMUM_LOOP_MILLISECONDS / 1000.0)` (`LoopEngine.cpp:122-123`) — 16 redundant ceil+divide chains per sample for Loooop on a Cortex-A7. Precompute `minWinLen_` in `reset()`; compute the window once per head per sample and pass it down (also makes read/fade/advance structurally consistent within a sample).

### DSP improvements

- **Overdub punch-out records a permanent step into the loop** (`LoopEngine.cpp:264-269`): input sums at full gain from the first sample and stops dead at `toggleRecord()`; punch-out lands mid-loop where the seam crossfade can't mask it, clicking on every subsequent pass. **Fix:** ramp an overdub-input gain 0→1 over `xfadeSamples_` at start and 1→0 at stop (defer `recording_ = false` by the ramp length). One multiply per sample while recording.
- **Linear → 4-point cubic (Hermite) interpolation** (`LoopEngine.cpp:134-160`). At fractional speeds and V/oct melodies, linear interpolation costs audible HF and adds intermodulation. Cubic is \~4 reads + a few MACs per channel — the single biggest fidelity win available, still modest on MetaModule.
- **Head level/pan/dry-wet are applied unsmoothed** (`LoopEngine.cpp:278-279`, `Loooop.cpp:141-145`): a square LFO into Level CV produces hard steps. A 1–5 ms one-pole on each removes it for a few ops.
- **One-shots end with a hard cut** (`fadeLen` returns 0 for one-shots, `LoopEngine.cpp:163`; `advanceHead` stops dead at `:214`). A short gain-ramp fade-out over the last `xfadeSamples_` (no second read needed) de-clicks the spec's "rhythmic re-slicer" patch.
- No anti-aliasing above 1× speed (speed 2 folds everything above SR/4). A proper fix isn't cheap; a pragmatic half-measure is averaging two reads spaced `sp/2` when `|speed| > 1`. Optional.
- Constant-power pan would be nicer than linear balance, but note the center-unity law is load-bearing for the "four heads at 0.25 sum to unity" default (`Loooop.cpp:45-47`).

### Display

- No defects found in `LoopDisplay.hpp` / `LoopWaveformRenderer` (degenerate-size clipping, silent-loop paths, image lifetime, and the documented bounded cross-thread reads all check out). One quality note: `peakBinSize_` is fixed by `maxSamples_`, so a 1 s loop uses \~68 of 4096 bins stretched across the display — blocky. Rebinning the peaks over `loopLen_` once at freeze time (off the per-sample path) fixes it.

### Low-complexity features

- **Overdub feedback/decay (sound-on-sound):** `buf = buf * feedback + in` instead of `buf += in`. One multiply per sample while recording plus one context-menu param; gives the classic Frippertronics decay *and* bounds the currently unbounded overdub sum. This is the one feature looper users will actually go looking for.
- Considered and rejected as *not* low-complexity: overdub phase alignment (writes always start at index 0 — fixing it needs a canonical loop-phase clock; explicitly marked "v1" in the code), undo-overdub (a second 23 MB buffer), saving the loop with the patch.

### Verified correct

`clear()` not zeroing the buffer is safe (first pass fully overwrites before overdub is reachable); all division-by-zero paths guarded; audio↔GUI snapshot uses atomics with the non-atomic peak reads documented and bounded; no dataToJson needed (all persistent state lives in params); every CV scaling matches the spec's "±10 V spans the range" claims.

---

## Particules (wrapper)

### Bugs

1. **[major, verified] Pitch CV is quantized to a 0.05 V (0.6 semitone) grid, breaking 1 V/oct tracking.**
   `src/particules/Particules.cpp:172` — `pitch_cv_conditioner_.Init(8, 0.35f, 0.05f, 0.0f)`; the third argument is `quantize_step` in volts (`control_conditioner.h`), and line 276 multiplies by 12 for semitones. One semitone = 1/12 V ≈ 0.083 V, which rounds to 0.10 V = 1.2 st: a chromatic sequence plays every other step 0.2 st off, and arbitrary notes are up to ±0.3 st out of tune. Particules.md explicitly promises 1 V/oct.
   **Fix:** `quantize_step = 0` for the pitch conditioner (or 1/1200 V if de-noising is wanted). The time/size/shape steps (0.01 V) are harmless.

2. **[major] On MetaModule, the SEED trigger/gate is sampled once per 64-sample block.**
   `Particules.cpp:293` with `:356-357` — the gate is read inside `updateSlowParams()`, which runs only when `BlockReady()`. A standard 1 ms trigger at 48 kHz is 48 samples wide, shorter than the 64-sample sampling interval, so roughly a quarter of triggers land between samples and spawn no grain — including this module's own 48-sample "Grain trigger on R output" feeding another unit. Clock-period measurement is also quantized to ±64 samples. VCV (block size 1) is unaffected.
   **Fix:** latch per-sample in `process()` (`seed_gate_seen_ |= v > 1.f` plus an edge count) and hand the latched value to `updateSlowParams()`, clearing after each block.

3. **[major] CV conditioner timing differs 64× between VCV and MetaModule.**
   `Particules.cpp:169-172, 273-276` — conditioners step once per wrapper block, so with `decimation = 8` the CV is sample-held every 8 samples in VCV but every 512 samples (\~10.7 ms) on MetaModule, and smoothing lag scales the same way (\~13 ms extra for pitch). The same patch behaves audibly differently on the two platforms.
   **Fix:** derive conditioner settings from `kWrapperBlockSize`: `decimation = max(1, 8 / kWrapperBlockSize)` and convert smoothing to a per-block coefficient `1 - powf(1 - s, kWrapperBlockSize)`.

4. **[minor] Freeze light ignores CV-driven freeze** (`Particules.cpp:378` uses only the button; use `frozen` from line 315). The manual says the light shows "when freeze is active."

5. **[minor] Quality LED colors don't match the manual** — code (`plugin.hpp:27-29`): white/green/yellow/red; manual: white/**cyan**/amber/**magenta**. Fix one side.

6. **[minor] Gate comparators have no hysteresis** (`Particules.cpp:293, 315` — plain `> 1.f`). A noisy gate crossing 1 V chatters freeze (audible buffer glitching) or floods clocked mode with spurious edges. Use Schmitt thresholds per the VCV standard (high ≥ 1 V, low ≤ 0.1 V).

7. **[minor] FREEZE and QUALITY are `configParam`, so patch Randomize hits them** — Ctrl+R randomly latches freeze and phantom-presses quality. Use `configButton`/`configSwitch` (which disable randomization).

8. **[minor] `manualGainDb` not clamped on JSON load** (`Particules.cpp:243-244`) — a hand-edited patch can inject 500 dB. Clamp to [0, 32].

9. **[minor] `onReset` doesn't clear the engine's audio state** (`Particules.cpp:195-207`) — the 4-second buffer, feedback path, and reverb tail survive "Initialize" (`stereo_input_` is also left stale). Call `processor_.ClearBuffer()` and reset `stereo_input_`.

10. **[minor] No NaN guard on audio input feeding a feedback recorder** (`Particules.cpp:351-353`) — with feedback up, one upstream NaN poisons buffer, reverb, and auto-gain until the user finds "Clear buffer". Two `isfinite` branches per sample fix it.

11. **[nit] Allocation failure is silent** (`Particules.cpp:160-168`) — the engine null-guards everything (verified), so it degrades to silence, but a `WARN()` would help debugging.

12. **[nit] Doc mismatches in Particules.md:** SEED input described as "bottom right" but placed top-right; the quality-mode table states fixed rates (48/32/24/24 kHz) but the engine applies decimation factors 1/2/8/4 to the *host* rate (`types.h:37-45`) — 48/24/6/12 kHz at a 48 kHz host, different again at 96 kHz.

### Refactorings

- `particules_block_runtime.h:17` — redundant `output_index_ = input_index_ + 1;` in `PushInputSample` (already advanced in lockstep by `ReadOutputSample`; transiently sets an out-of-range value that's never dereferenced but is a trap). Remove.
- `Particules.cpp:104` — dead member `grain_led_`. Delete.
- `particules_block_runtime.h:73` — `std::pow(0.9999f, BlockSize)` recomputed every call; in VCV that's a per-sample `powf` for a constant. Make it `static const`.
- `particules_density_control.h` — takes *raw* voltage but names the param `conditioned_density_cv`, and includes three headers it doesn't use. Rename or inline the `* 0.2f`.
- `Particules.cpp:3` includes a vendored-library internal (`src/util/control_conditioner.h`); promote it to `include/beads/`.
- `Particules.hpp` is an empty file; delete.

### DSP improvements

- **Batch the VCV path.** With `kWrapperBlockSize = 1`, every VCV sample pays full `updateSlowParams()` (\~12 `getVoltage()` calls, 4 conditioner steps), a whole-struct `SetParameters()`, and per-call engine setup. A 16–32 sample VCV block amortizes all of it for \~0.3–0.7 ms latency (the engine mixes dry internally, so no comb risk — verified feedback and dry/wet are already smoothed engine-side) and shrinks the VCV/MM behavioral gap in findings #2/#3.
- **Grain trigger pulses merge on retrigger** (`particules_block_runtime.h:53-55`) — two grains < 1 ms apart produce one continuous high. Force one low sample between pulses so downstream counters see one edge per grain.

### Low-complexity features

- **Scale-aware pitch lock:** the engine already exposes `LoadScale`/`ClearScale`/`SetScaleRoot` (`beads.h:49-51`) and the wrapper already has the "Lock pitch" submenu and JSON field. "Chromatic / Major / Minor pentatonic" entries are a few tables plus one call — high musical value, zero panel clutter.
- **Grain-count-aware LED:** `ActiveGrainCount()` (`beads.h:41`) is unused; scaling the grain light by active count makes Density visually legible (the current flash-and-2-second-decay reads as a fade, not the per-grain flash the manual promises).
- **Input level readout in the gain menu:** `InputLevel()` (`beads.h:43`) is unused; showing it next to "Manual gain" makes gain staging without auto-gain far less blind.
- **Undo for context-menu options:** the seed/pitch-lock/grain-trigger/gain settings bypass VCV's history — Ctrl+Z does nothing. `history::ModuleChange` wrapping is boilerplate-cheap (VCV-only).

### Verified correct

Block-runtime read-before-push ordering and indexing at both block sizes (hand-traced, matches tests); the pitch notch map (monotonic, symmetric, exact at endpoints and center); engine null-guards after failed `Init`; `GetMemoryRequirements` genuinely sample-rate-independent today (the re-`Init` in `onSampleRateChange` is safe, but assert the size to future-proof).

---

## beads_dsp (vendored library)

### Bugs

1. **[critical, verified] `Process()` reads `dry_input_buf` out of bounds when `num_frames > 64` — and the test suite does exactly that.**
   `src/beads_processor.cpp:173` clamps writes (`if (i < kMaxBlockSize) s.dry_input_buf[i] = in;`) but `:266-267` reads unclamped (`s.dry_input_buf[offset + i]` with `offset` up to `num_frames − block`); the buffer is `StereoFrame[64]`. Every processor-level test calls `Process(..., 256)`, so frames 64–255 of the dry path read past the end of `Impl` — formally UB. The "Dry pass-through" test passes only because the OOB memory happens to contain a near-copy of the input. The docs contradict each other: `types.h:65-67` promises arbitrary `num_frames` via chunking; `beads_processor.h:57` forbids > 64. Both real hosts pass 64 (MM) or 1 (VCV), so production is unaffected — but every test result above 64 frames is tainted.
   **Fix:** chunk the whole pipeline — move the per-sample input stage (steps 1–4) inside the per-block loop so `dry_input_buf` is indexed `i` — which also makes the documented API true. Then add the block-size-independence test below.

2. **[major, verified] The feedback path is a one-sample zero-order hold at block rate.**
   `beads_processor.cpp:181-193` (input loop, runs over the whole call first) reads `s.feedback_sample` every sample, but it's only updated later in the wet loop (`:259-263`). The entire block's feedback injection derives from one held sample — the last wet frame of the *previous* call. At 64-sample blocks (MetaModule) the feedback signal is a 750 Hz staircase: aliased, spectrally unrelated to the wet signal, and much weaker than intended at high FEEDBACK. In VCV (`kWrapperBlockSize = 1`) the bug mostly vanishes — so the two platforms sound different at high feedback. Original Clouds/Beads mixes the previous block's *full* output buffer per-sample.
   **Fix:** keep the previous block's wet output in an `Impl` buffer and mix `prev_wet[i]` into `input[i]` per-sample. Biggest audible win in the library.

3. **[major, verified] The freeze crossfade is wrong in both directions.**
   `recording_buffer.cpp:247-278` (`ProcessFreezeCrossfade`): with `gain = counter/kCrossfadeSamples` and `frame = write_head_ − 1 − (kCrossfadeSamples − counter)`:
   - **Freeze** (writes stop): the newest frame gets gain 32/32 = 1.0 (a no-op) and the ramp *decreases* going back in time — the actual seam discontinuity at `write_head_` is untouched, and a new discontinuity is created 33 samples back where gain jumps from 1/32 to untouched 1.0.
   - **Unfreeze** (writes resume): `write_head_` and the offset advance in lockstep, so `frame` is the *same frame every call*; it gets multiplied by 1 × 31/32 × … × 1/32 ≈ 0 — a single zeroed sample punched into the audio, i.e. a click.
   Additionally, `StartFreezeCrossfade` (`:214-245`) cancels the crossfade if any zero crossing exists in the last 64 samples — but never *moves* the freeze point there, so the cancellation fixes nothing, and since almost all material has such a crossing, the (already broken) fade almost never runs. Net: the freeze de-click feature is a no-op that occasionally adds a click.
   **Fix:** on freeze, fade toward zero *at the seam* (gain 0 at `write_head_ − 1` rising to \~1 at `write_head_ − 32`); on unfreeze, don't mutate the buffer — ramp the incoming writes 0→1 instead. Either use the zero-crossing scan to actually relocate the loop endpoint, or delete it. Also re-sync the interpolation-tail mirror copies when faded frames fall within `kInterpolationTail`. The existing tests only check the `crossfading()` flag — add a test that freezes a sine mid-cycle and asserts bounded deltas across `write_head_`.

4. **[minor] Deferred buffer-clear drains per *call* while the quality-transition duck is per *sample*** (`beads_processor.cpp:158-160`, `beads_processor.h:44-47`) — they only align at 64-sample blocks. At other block sizes grains audibly read half-cleared stale content after the duck ends. Scale the chunk by `num_frames`.

5. **[minor] Grain kill fallback fade never reaches zero** (`grain.h:309-338`): `fade = counter/4` runs 1.0, 0.75, 0.5, 0.25 then cuts — a 0.25-amplitude step (audible tick when the pool saturates on DC-heavy content). Use `(counter − 1)/kFallbackFadeSamples`.

6. **[minor] Reverb `DelayLine`s would dereference null on undersized buffers** (`fx_engine.h:21-29`, `reverb.cpp:33-41`) — unreachable today (needs ≈ 11,996 of 16,384 floats), but `Reverb::kMinBufferSize` is declared and never checked. Guard `Init`.

7. **[minor] Defensive wrap loops can hang on huge finite positions** (`recording_buffer.cpp:104-107` etc.): `while (position >= size_f) position -= size_f;` never terminates once `ulp(position) > size_f` (\~3e12 with this buffer). Currently unreachable, but it *is* the stated defensive layer — use `fmod`/`isfinite`. Related: unguarded float→int casts in `ReadHermiteStereoFast` and the DTC cache make a NaN grain position UB; one `isfinite` fence in `ComputeGrainParams` covers the whole path.

8. **[minor] "Kill the oldest grain" kills an arbitrary one** (`grain_engine.cpp:61-72`) — it marks the first non-pending grain by array index, which is frequently the *newest* (slot 0 is reused first). Under saturation, fresh grains die while old ones ring on. Track start order or pick the highest `envelope_phase_`.

9. **[minor] Scheduler dead state / mode-change glitch** (`grain_scheduler.cpp:122-131, 146`): `clock_period_` is measured and smoothed but never read; `BeadsParameters::clock_interval_samples` is never read by anyone (host doesn't set it). `gate_phase_` doubles as the clock-division counter without reset on trigger-mode change, so the first division after switching gated→clocked can be off by one.

10. **[minor, uncertain] No FTZ/denormal measures** in the reverb/SVF/smoother tails. VCV sets FTZ on its engine threads and MetaModule firmware presumably sets FPSCR flush-to-zero (unverified), but the *test binary* doesn't — silent-tail CPU spikes on x86 are possible. Cheap insurance: quash values below 1e-20 in the reverb feedback.

11. **[nit] `NextGaussian()` isn't unit variance** (`random.h:43-51`): it's a CLT sum with σ ≈ 0.577 and hard range ±2, despite the comment. Only matters if spread math was designed in unit-sigma terms.

### Refactorings

- **Duplicated SIZE→duration mapping** — `grain_engine.cpp:86-101` and `:205-216` implement the same boundary/exp2 mapping twice with subtly different inputs. Extract one helper; divergence here is a future bug.
- **`StateVariableFilter` triplication** (`svf.h:33-60`) — `ProcessLP/HP/BP` are identical except the return expression.
- **Quality-mode crossfade duplication** — `quality_processor.cpp:66-79` vs `:130-143`.
- **Dead code:** `EqualPowerCrossfade` and `FastPowUnit` (`dsp_utils.h`) have no callers; `Saturation::Process` is test-only; the **entire wavetable module** (`src/wavetable/`) is referenced by nothing; the **entire DTC path** (`GrainDTCCache`, `ProcessBlockCached`, the 5-arg `Init`) is never used by this repo's host and has zero test coverage — keep only if it serves another port, else delete; `BeadsParameters::seed_connected` and `stereo_input` are set by the host but never read.
- Dead store `output[i] = {0,0}` at `beads_processor.cpp:203` (unconditionally overwritten at `:279`).
- `RecordingBuffer` read-path duplication: only `ReadHermiteStereoFast` is used in production; `ReadHermite`/`ReadHermiteStereo`/`ReadLinear` share identical guard/wrap code — demote to test-only or consolidate.

### DSP improvements

- Fixing the feedback ZOH (#2) is by far the largest audible improvement and restores parity with hardware Beads.
- **Dry path taps pre-auto-gain input** (`beads_processor.cpp:172-173`, deliberate so DRY/WET = 0 equals bypass) — but with auto-gain boosting +32 dB, mid-knob mixes have a big dry/wet level mismatch; hardware Beads crossfades post-gain. Consider tapping post-gain, or document the trade-off.
- **Adaptive interpolation is inert where it's needed** (`grain_engine.cpp:241`): `use_linear` is set under load but only `ProcessBlockCached` honors it; the direct path (the only one VCV uses) always calls Hermite. Wire `use_linear_` into `Grain::Process`/`ProcessBlock`.
- **kMidi burst mode clumps** (`grain_scheduler.cpp:211-221`): up to 15 burst grains land inside one ≤1.3 ms block — effectively one thick grain. Spread the burst across blocks with a pending counter.
- **Reverb never sleeps** (`reverb.cpp:126` gates input at `amount_ == 0` but the full 12-delay-line tank runs forever). On the MetaModule budget, add an energy-based sleep once the tail decays.
- `AutoGain::SoftLimit` uses `std::tanh` per channel (`auto_gain.cpp:35`) while `FastTanh` exists and is used elsewhere; `FastDbToGain(kMinGainDb)` recomputed every locked sample (`:118`) — hoist.
- `std::pow(0.998f, block)` per block (`beads_processor.cpp:228`) is a per-sample `pow` in the VCV build; special-case `block == 1` or precompute.
- Grain-rate cap at 80 Hz (`grain_scheduler.h:24`) is a deliberate, documented retirement of the old high-rate path, but hardware Beads reaches higher perceived densities at full CW — revisit only if "faithful recreation" is the goal.
- The overlap-normalization "slow fall" coefficient (`grain_engine.cpp:288-296`) is 0.2/sample ≈ 5-sample time constant — not slow; if pumping is ever heard, it wants \~0.001.

### Test coverage gaps

- **Block-size independence** — no test compares `Process` at 1 vs 64 vs 256 frames. This single test would have caught findings #1, #2, and #4. Add it first, after fixing #1.
- **Freeze seam smoothness** — only the `crossfading()` flag is asserted today.
- **Feedback waveform continuity** — only divergence is asserted; a staircase-detection or cross-correlation test would catch #2.
- **Interpolation-tail sync** — no test writes at `write_head_ < kInterpolationTail` then reads fractionally across the `size_` boundary (the trickiest indexing in the buffer).
- **No scheduler tests for kClocked or kMidi modes** — kClocked is what the host actually selects when SEED is patched.
- **No NaN-robustness tests** (feed NaN CVs, assert finite output).
- The DTC path has zero coverage (moot if deleted).
- Stale test comments: `test_quality_modes.cpp:44` says 8 kHz tape LP (constant is 5 kHz); `test_reverb.cpp` cites 0.3 st wow (actual 0.02).

### Verified correct

Interpolation-tail write mirroring and all tail-based index math; DTC prefetch wrap/margin arithmetic; grain envelopes reach \~0 at both edges for all SHAPE zones; decimation ratios consistent; reverb partition fits its buffer and `fb ≤ 0.9375` with in-loop `SoftClip` is stable; no heap allocation or locks anywhere in the audio path; xorshift128 PRNG deterministic and fine.

---

## Plugin glue, build, metadata, tests

**All the high-risk checks pass** (verified by running both test lanes and diffing files): module slugs consistent across plugin.json, `createModel` calls, MetaModule info headers, and plugin-mm.json; the two plugin.json copies byte-identical with an auto-sync that prevents drift; all four modules registered in both targets; the 13 beads_dsp sources present in both build systems; all referenced panels/faceplates exist; `tests/run.sh` and the Python tests pass; every spot-checked doc claim matches the code.

Remaining items, all minor:

1. **`plugin.json` lacks `sourceUrl`/`manualUrl`** — required (`sourceUrl`) for VCV Library submission; per-module `manualUrl` could point at the per-module .md files.
2. **The two Python guard tests aren't wired into any lane** — `tests/run.sh` doesn't run them and `tests/README.md` doesn't mention them, so the identity/no-delay guards will silently stop being run. Add `python3 -m unittest discover` to `tests/run.sh`.
3. **Object files escape `build/` into `vcv/src/`** because all sources are `../src/...`, so `make clean` leaves stale objects (gitignored, but a "clean" build isn't). Extend the clean target.
4. **Module list duplicated** between plugin.json and plugin-mm.json with nothing checking they agree — extend `test_robotboy_identity.py` to assert the two slug lists are equal.
5. **Version stated twice** (plugin.json and `project(RobotBoy VERSION 2.0.1)`) — the CMake one is informational and will drift; drop it or read it from plugin.json.
6. Nits: stale "16 test files" comment in `tests/beads/CMakeLists.txt:19` (there are 15); `-std=c++20` applied to all languages in `metamodule/CMakeLists.txt:50` (use `target_compile_features`); the Makefile's `cp` sync swallows failures; empty `presets/` directory that, if ever populated, wouldn't ship (not in `DISTRIBUTABLES`); `plugin-mm.json`'s `MetaModuleBrandSlug`/`displayName` keys unverified against the 4ms schema (likely fine, worth a one-time check).

---

## Suggested order of work

1. **MF-20 K35 loop saturation** — the only finding that NaN-bombs a patch in normal use. Fix + the missing `resTaper`-path K35 test + the `test_zero_io` accumulator bug.
2. **beads_dsp `Process()` chunking** — makes the test suite meaningful again; then add the block-size-independence test and re-run everything.
3. **beads_dsp feedback path** — biggest audible improvement, and it un-forks the VCV/MetaModule sound.
4. **Loooop jitter-aware crossfade + freeze-crossfade rewrite** — the two "the de-clicker is the clicker" bugs.
5. **Particules pitch quantize step → 0, per-sample SEED latch, block-size-aware conditioners** — restores documented 1 V/oct and MM trigger reliability.
6. The minor/NaN-hardening/CPU items and the low-complexity features (overdub feedback for Loooop, scale-aware pitch lock for Particules, HP output for MF-20) as time allows.
