# Code-review fixes — spec

Source: `code-review-2026-07-08.md`, "Suggested order of work" items 1–5. Bug fixes only — no new user-facing features, no panel/param changes, no behavior changes beyond what each fix requires.

## 1. MF-20: K35 loop saturation

**Current behavior:** `MF20Filter::processK35()` clips only the forward-path input; the resonant loop is linear in the states. The module applies `resTaper()` (max 1.025) before calling the filter, so at knob settings above \~0.844 the loop gain `k = res × 8/3` exceeds 8/3 → negative damping → output diverges to ±inf then NaN within \~0.5 s, permanently poisoning `s1`/`s2`.

**Desired behavior:** The K35 resonance loop saturates, as in the real Korg35 circuit (the loop transistor clips). Model: move the feedback term out of the lumped damping and clip it —

```
ẋ₁ = ωc·(clip_in − (8/3)·x₁ + fbClip(k·x₁) − x₂)
```

with `fbClip` the same piecewise-linear clip shape used by the OTA mode, at **fixed** normalized threshold T = 1.0 and slope 0.25 (independent of drive; drive shapes the input clip only). TPT solve region-wise, exactly like `processOTA` (try linear region, check `|k·x₁| ≤ T`, else saturated region).

**Invariants:**
- In the linear region (`|k·x₁| ≤ 1`) the math must be algebraically identical to the current code: `D1 = (1+g)² + (2/3)g − g·k` equals the old `(1+g)² − g·(k − 2/3)`. Existing K35 tests must still pass.
- At `res = resTaper(1.0) = 1.025` (i.e. `k ≈ 2.733`), output stays finite and bounded forever, and self-oscillation sustains (bounded limit cycle) after excitation.
- At `res = 1.0` exactly (k = 8/3, marginal), the existing self-oscillation test still passes.
- OTA mode untouched.

**Also in scope (same file's test suite):** fix the `test_zero_io` accumulator bug in `tests/mf20/test_mf20.cpp` (`buf[0]` overwritten per combo → only the last (fc, res) combo is asserted).

**Acceptance:** new test drives the module's exact path (resTaper → K35, drive 1, fc 1 kHz, 48 kHz) at knob values {0.85, 0.9, 1.0}, with a 0.2-amplitude sine then silence, and asserts all outputs finite and |lp| < 2.0 over several seconds; plus a sustained-oscillation assertion at `resTaper(1.0)`. Test must fail against the current code.

## 2. beads_dsp: `Process()` chunking

**Current behavior:** `BeadsProcessor::Process()` runs a per-sample input stage over all `num_frames` (clamping `dry_input_buf` writes to 64) and then a chunked wet stage that reads `dry_input_buf[offset + i]` unclamped — out-of-bounds reads for `num_frames > 64`. The test suite calls with 256. The docs contradict each other (`types.h` promises chunking; `beads_processor.h` forbids > 64).

**Desired behavior:** `Process()` chunks the **entire** pipeline: split `num_frames` into blocks of ≤ `kMaxBlockSize` (64) and run input stage + wet stage per block. `dry_input_buf` is indexed only by the intra-block index. The `types.h` "handles arbitrary num_frames" doc becomes true; the `beads_processor.h` "callers must not pass > 64" comment is removed/updated.

**Consequences accepted:** `TickClear` and the freeze-transition check now run once per 64-frame block instead of once per call — this matches the MetaModule cadence and fixes the clear/duck misalignment noted in the review.

**Invariants:**
- `Process(in, out, 256)` produces bit-identical output to four consecutive `Process` calls of 64 on an identically-initialized processor fed the same stream (fixed PRNG seeds make this deterministic).
- Hosts passing 1 (VCV) or 64 (MM) see unchanged behavior.

**Acceptance:** new Catch2 test asserting 256-vs-4×64 bit-equality (params chosen so the old OOB dry path makes it fail pre-fix: auto-gain off, manual gain +12 dB, dry_wet = 0). Existing suite still passes.

## 3. beads_dsp: per-sample feedback path

**Current behavior:** the input loop reads `feedback_sample` (a single `StereoFrame`) every sample, but it's updated only in the wet loop — so an entire block's feedback injection derives from one held sample of the previous call (a 750 Hz staircase at 64-frame blocks). VCV (1-frame calls) ≈ per-sample; MetaModule sounds different.

**Desired behavior:** keep the **previous block's full wet output** (post-quality-processing, pre-reverb, NaN-guarded per frame) in an `Impl` buffer `prev_wet_buf[kMaxBlockSize]` with `prev_wet_len`; the input stage of the next block mixes `prev_wet_buf[i]` (index clamped to `prev_wet_len − 1`; zeros before the first block) into the input per-sample, through the existing 30 Hz HP filters and smoothed-feedback gain, exactly as now. `feedback_sample` is removed. One block of feedback latency (1 sample in VCV, 64 on MM) — same as the original Mutable design.

**Invariants:**
- VCV cadence (1-frame calls): behavior identical to today (prev_wet_buf[0] == old feedback_sample).
- Feedback loop remains bounded (existing `LimitFeedback` untouched).
- Block-size bit-equality from fix 2 still holds.

**Acceptance:** a test that fails pre-fix and passes post-fix, demonstrating the feedback injection varies per-sample within a block at 64-frame cadence (implementer chooses the sharpest observable; energy-comparison between 1-frame and 64-frame cadence with a generous tolerance is acceptable if verified red-then-green). Existing feedback-divergence test still passes.

## 4a. Loooop: jitter-aware seam crossfade

**Current behavior:** `readHead()` previews the incoming loop head from the **current** window (`h.jitterOff`), but `advanceHead()` rolls new jitter at the wrap and resumes in the **new** window — the fade smoothly leads into audio that never plays. Full-amplitude discontinuity every repeat when jitter > 0 and crossfade on.

**Desired behavior:** the next window is decided **before** the fade begins. `PlayHead` gains `jitterNext`; `rollJitter()` rolls into `jitterNext`; at each wrap the head **commits** `jitterOff = jitterNext` and pre-rolls a fresh `jitterNext`. `readHead()` previews the incoming head using window bounds computed with `jitterNext`. `restartHead()` still relocates immediately (roll + commit). `setJitter()` pre-rolls `jitterNext` when jitter transitions 0 → nonzero so the first wrap after enabling jitter is already randomized.

**Invariants:**
- jitter = 0: bit-identical behavior to today (offsets are exactly 0).
- The sample after a wrap continues exactly where the fade preview ended (same window bounds).
- All existing loop-engine tests pass unchanged.

**Acceptance:** new test: record a smooth sine at 48 kHz, jitter = 1, crossfade on, size < 1; assert max sample-to-sample output delta stays below a threshold that the current code violates by an order of magnitude. Must fail pre-fix.

## 4b. beads_dsp: freeze crossfade rewrite

**Current behavior:** `StartFreezeCrossfade()` cancels the fade if any zero crossing exists in the last 64 samples (without moving the seam — a no-op check that defeats the feature on almost all material). `ProcessFreezeCrossfade()`'s arithmetic is inverted: on freeze the newest frame gets gain 1.0 and a new discontinuity is created 33 frames back; on unfreeze it multiplies one single frame down to \~0 (a click).

**Desired behavior:** replace the Start/Process pair with `void NotifyFreeze(bool frozen)`:
- **Entering freeze:** immediately (one-shot, in-place) apply a symmetric fade-to-zero at the write seam: frames `write_head_ − 1 − j` and `write_head_ + j` (wrapped) both scaled by `j / kCrossfadeSamples` for `j = 0..kCrossfadeSamples−1`, so both sides of the seam meet at silence. Keep the interpolation-tail mirror in sync for any touched frame < `kInterpolationTail`. (2 × 32 frames × 2 channels — trivially cheap.)
- **Leaving freeze:** mutate nothing; arm a write crossfade: for the next `kCrossfadeSamples` accepted writes, `Write()` blends `stored = old · (1−g) + incoming · g` with g ramping 0 → 1, so recorded content transitions smoothly from frozen audio to live input.
- Delete the zero-crossing scan, `crossfading_`, `crossfade_counter_`, `crossfading()`, and the per-sample `ProcessFreezeCrossfade()` call in `beads_processor.cpp`; the freeze-transition detection there calls `NotifyFreeze()` instead.

**Invariants:** `Write()` behavior unchanged when no ramp is active; decimation interacts with the ramp by counting only accepted writes.

**Acceptance:** Catch2 tests: (1) after `NotifyFreeze(true)` on a buffer filled with 1.0, the frames adjacent to the seam are ≈ 0 on both sides, ramp monotone, frames ≥ 32 away untouched, and the case where the fade wraps past frame 0 keeps the tail mirror consistent; (2) after `NotifyFreeze(false)`, the first accepted write ≈ old content, the ramp reaches the incoming value by write 32; (3) seam-smoothness: fill with a sine (which has zero crossings — proving the old scan-cancel path is gone), freeze, read across the seam with `ReadHermiteStereo`, assert bounded deltas. Tests must fail pre-fix (the old code leaves the seam untouched whenever a zero crossing exists).

## 5. Particules: pitch quantize, SEED latch, block-aware conditioners

Three wrapper-layer fixes:

**(a) Pitch CV quantization.** `pitch_cv_conditioner_.Init(8, 0.35f, 0.05f, 0.0f)` quantizes Pitch CV to 0.05 V (0.6 st), breaking 1 V/oct. The pitch conditioner's quantize step becomes **0.0** (no quantization). Time/size/shape keep 0.01 V.

**(b) Per-sample SEED latch.** The SEED gate is read once per wrapper block; on MetaModule (64-sample blocks) 1 ms triggers are dropped \~25% of the time. Fix: latch "gate was high at any sample this block" per-sample and feed the latched value to the engine. Implement in `ParticulesBlockRuntime` (so it's unit-testable): `NoteSeedGateSample(bool high)` ORs into a latch; `ConsumeSeedGateLatch()` returns and clears it. `process()` calls `NoteSeedGateSample` every sample; `updateSlowParams()` uses `ConsumeSeedGateLatch()` for `params_.gate`. VCV (block = 1): behavior identical to today.

**(c) Block-size-aware conditioner settings.** Conditioners step once per wrapper block, so decimation 8 and per-step smoothing 0.35/0.5 mean 512-sample sample-hold and \~64× slower settling on MetaModule. Fix: derive settings from `kWrapperBlockSize` in a new small header `src/particules/particules_cv_conditioning.h` (pattern matches `particules_density_control.h`):
- `CvDecimationForBlock(block)` = `block >= 8 ? 1 : 8 / block` (VCV → 8, MM → 1)
- `CvSmoothingForBlock(s, block)` = `1 − (1 − s)^block` (VCV → s unchanged, MM → ≈ 1)
- constants: `kCvSmoothing = 0.5f`, `kPitchCvSmoothing = 0.35f`, `kMenuCvQuantizeStep = 0.01f`, `kPitchCvQuantizeStep = 0.0f` (with a comment stating it must stay 0 for 1 V/oct).

`Particules()` and nothing else consumes these. VCV numerical behavior is unchanged except the pitch quantize step.

**Invariants:** no VCV behavior change other than un-quantized pitch CV; no engine/API changes; JSON, params, panel untouched.

**Acceptance:** new standalone test `tests/particules/test_cv_conditioning.cpp` (built by `tests/run.sh`) asserting the mapping values for block sizes 1 and 64, that `kPitchCvQuantizeStep == 0`, and that a 1/12 V step survives a pitch-configured `ControlConditioner` unquantized; new cases in `tests/beads/test_particules_block_runtime.cpp` for the seed latch (mid-block pulse is captured; consume clears; block-size-1 passthrough).

## Global constraints

- Real-time safety: no heap allocation, locks, or unbounded loops in any audio-path change. One-shot work at freeze transitions must stay O(kCrossfadeSamples).
- All existing tests in all lanes (`tests/run.sh`, `tests/beads/run.sh`, `python3 -m unittest discover -s tests`) must pass when done.
- The VCV plugin must compile (`vcv/` Makefile) after all changes.
- TDD: every fix lands with a test that failed before the fix (verified red first).
- Commit per fix, short messages (≤ 15 words), no AI attribution.
