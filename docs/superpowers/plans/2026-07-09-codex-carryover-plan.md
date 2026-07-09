# Codex Carry-Over Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the worthwhile work from branch `review-fixes-codex` onto the canonical branch: MetaModule perf (SVF divide removal, FTZ, waveform display cache), MF-20 fixes (first-modulate, R-poly, drive smoothing), the Loooop/Lop control-math dedup, and a batch of small correctness fixes.

**Architecture:** Where our files are byte-identical to codex's "before" (svf.h, the display renderer), port directly from the codex commits (`git show <sha>`). Where our branch has drifted (MF20Filter.cpp, grain_engine.cpp, particules_block_runtime.h), this plan gives the adapted code — do NOT cherry-pick those commits mechanically.

**Tech Stack:** C++20; codex commits readable via `git show <sha>` and the worktree `.worktrees/review-fixes-codex/`. Test lanes: `tests/run.sh` (g++), `tests/beads/run.sh` (Catch2/CMake), builds `make -C vcv -j8` and `cmake --build metamodule/build -j8`.

**Spec:** `docs/superpowers/plans/2026-07-09-codex-carryover-spec.md`

## Global Constraints

- Commit messages: one short sentence ≤15 words, NO Co-Authored-By or AI attribution.
- Behavior-preserving unless the spec item says otherwise (drive smoothing, LED decay, and grain-overlap smoothing are the sanctioned behavior changes; each is bounded and documented).
- All four verification lanes green after every task.
- Never cherry-pick `d17e13d` wholesale (it bundles codex's superseded reverb work) — take only its `src/loooop/dsp/LoopEngine.*` hunks.
- When porting a codex test, port it verbatim unless our API drift requires adaptation; never weaken an assertion.

---

### Task 1: beads quick wins — SVF cache, CleanLoFi bound, random < 1, pitch map

**Files:**
- Modify: `src/vendor/beads_dsp/src/util/svf.h` (port `bad157c`)
- Modify: `src/vendor/beads_dsp/src/fx/saturation.cpp` (port `6754c75`)
- Modify: `src/vendor/beads_dsp/src/random/random.h` (port `7b99f9d`)
- Modify: `src/particules/pitch_notch_map.hpp` (`static constexpr`/anon-namespace → `inline constexpr`, from `32a051c`)
- Test: `tests/beads/` — port the test additions from `git show 6754c75 -- tests/beads` and `git show 7b99f9d -- tests/beads` (monotonicity/sign-preservation for the limiter; a `test_random.cpp` asserting `NextFloat() < 1.0f` over many draws)

**Interfaces:**
- Produces: `StateVariableFilter` gains cached `denominator_recip_` and a unified `Tick()`; `ProcessLP/HP/BP` remain as thin wrappers (callers unchanged).

- [ ] **Step 1: Port the SVF change**

Our `svf.h` is byte-identical to codex's pre-change version, so apply codex's version directly: `git show bad157c:src/vendor/beads_dsp/src/util/svf.h > src/vendor/beads_dsp/src/util/svf.h`, then read the result and confirm: `denominator_recip_` recomputed only in `Init`/`SetFrequency`/`SetQ`; one `Tick()` computing hp/bp/lp; `ProcessLP/HP/BP` delegate to it; no per-sample divide remains.

- [ ] **Step 2: Port the limiter and RNG fixes**

`saturation.cpp` — apply the `AsymmetricSoftClip` hunk from `git show 6754c75 -- src/vendor/beads_dsp/src/fx/saturation.cpp`: raw `FastTanh(...)` → `SoftClip(...)` (same argument). `random.h` — replace the `NextFloat` body:

```cpp
    // Top 24 bits → exact float in [0, 1). Dividing the full 32-bit value by
    // 2^32 rounds up to exactly 1.0f for large states (float has a 24-bit
    // mantissa), breaking the half-open contract.
    float NextFloat() {
        return (NextUint32() >> 8) * (1.0f / 16777216.0f);
    }
```

Also take `7b99f9d`'s comment correction on `NextGaussian` (it is NOT unit variance; σ ≈ 0.577).

- [ ] **Step 3: pitch_notch_map**

Apply the `pitch_notch_map.hpp` hunk from `git show 32a051c -- src/particules/pitch_notch_map.hpp` (anonymous namespace + `static constexpr` → `inline constexpr`). Skip every other file in that commit.

- [ ] **Step 4: Port the tests, run the beads lane**

Port the test files/hunks named above into `tests/beads/`. Run `cd tests/beads && ./run.sh` → 100% pass (the SVF change is exercised heavily by the existing reverb/quality tests — bit-level output may legitimately differ at the last ulp because `x * (1/d)` replaces `x / d`; if any existing test fails, inspect whether its tolerance was asserting exact equality on filter output and report rather than loosen).

- [ ] **Step 5: All lanes + commit**

`cd tests && ./run.sh`; `make -C vcv -j8`; `cmake --build metamodule/build -j8`.

```bash
git add src/vendor/beads_dsp src/particules/pitch_notch_map.hpp tests/beads
git commit -m "perf: cache SVF denominator; bound CleanLoFi limiter; fix RNG range"
```

---

### Task 2: MetaModule flush-to-zero

**Files:**
- Create: `src/particules/metamodule_fpu.h` (port from `cf74abc`)
- Modify: `src/particules/Particules.cpp` (one-time call in `process()`)

**Interfaces:**
- Produces: `particules::EnableMetaModuleFlushToZero()` — idempotent, no-op off-ARM/off-MetaModule.

- [ ] **Step 1: Port the header**

`git show cf74abc:src/particules/metamodule_fpu.h > src/particules/metamodule_fpu.h`, then add (if codex's version lacks it) a comment noting the FPSCR write intentionally mutates thread-wide FPU state for the audio thread's lifetime.

- [ ] **Step 2: Wire the call**

Port the `Particules.cpp` hunk from `git show cf74abc -- src/particules/Particules.cpp`: a `bool metamodule_fpu_configured_ = false;` member and, at the top of `process()`:

```cpp
		if (!metamodule_fpu_configured_) {
			metamodule_fpu_configured_ = true;
			particules::EnableMetaModuleFlushToZero();
		}
```

(Adapt member placement to our current Particules.cpp — it drifted from codex's.)

- [ ] **Step 3: Verify + commit**

All four lanes (the change is compile-verified on macOS; the ARM branch only compiles in the MetaModule build — that lane passing is the real check).

```bash
git add src/particules
git commit -m "perf: enable flush-to-zero on MetaModule audio thread"
```

---

### Task 3: MF-20 — first-modulate init, R-channel polyphony, drive smoothing

**Files:**
- Modify: `src/mf20/MF20Filter.cpp`
- Modify: `src/mf20/MF20Filter.hpp` (add `setDriveCharacterFromThreshold`)
- Test: `tests/mf20/test_mf20.cpp`

Do NOT cherry-pick `614a6af` — our MF20Filter.cpp was rewritten in round 2 (g-domain smoothers, by-value pool). Implement as below.

**Interfaces:**
- Produces: `void MF20Filter::setDriveCharacterFromThreshold(float t)` — stores `clipThreshold = t; satSlope = 0.25f * t;` (no math). `setDriveCharacter(float drive)` stays (tests use it).

- [ ] **Step 1: First-modulate + R-poly (trivial fixes)**

`MF20Filter.cpp`:
- `int _steps = -1;` → `int _steps = 100;  // ≥ _modulationSteps: first process() modulates immediately (saved K35 mode, g targets)`
- In `onSampleRateChange`, after `_modulationSteps = ...`: add `_steps = _modulationSteps;`
- In `process()`: `int voices = std::max(1, inputs[AUDIO_INPUT].getChannels());` → `int voices = std::max({1, inputs[AUDIO_INPUT].getChannels(), inputs[AUDIO_INPUT_R].getChannels()});`

- [ ] **Step 2: Drive smoothing (zero divisions per sample)**

`MF20Filter.hpp`, next to `setDriveCharacter`:

```cpp
    /** Store an already-computed clip threshold (= 1/√drive). Lets hosts slew
        the drive character per-sample without any per-sample sqrt/divide. */
    void setDriveCharacterFromThreshold(float t) {
        clipThreshold = t;
        satSlope      = 0.25f * t;
    }
```

`MF20Filter.cpp` members — replace `float _driveSqrt = 1.f;` with:

```cpp
    // Drive smoothing: targets computed once per modulate block, slewed
    // per-sample (5 ms) so Drive sweeps don't zipper. Two smoothers so the
    // audio path needs no sqrt or divide: √drive feeds the OTA pre-gain,
    // 1/√drive feeds the diode clip character.
    OnePoleSmoother _driveSqrtSlew   { 1.f };
    OnePoleSmoother _clipThreshSlew  { 1.f };
    float _driveSqrtTarget  = 1.f;
    float _clipThreshTarget = 1.f;
    float _driveSqrt = 1.f;   // current smoothed value, used by otaPreGain
```

`modulate()`: replace `_driveSqrt = std::sqrt(drive);` with:

```cpp
        _driveSqrtTarget  = std::sqrt(drive);
        _clipThreshTarget = 1.f / _driveSqrtTarget;
```

and DELETE the four per-voice `eng.*.setDriveCharacter(drive);` calls (drive character is now pushed per-sample).

`onSampleRateChange`: set the two new smoothers' alpha alongside the g/res smoothers (`_driveSqrtSlew.setAlpha(alpha); _clipThreshSlew.setAlpha(alpha);`).

`process()`, before the voice loop (next to the dither flip):

```cpp
        _driveSqrt = _driveSqrtSlew.process(_driveSqrtTarget);
        _clipThresh = _clipThreshSlew.process(_clipThreshTarget);
```

(add `float _clipThresh = 1.f;` member), and in `processChannel()` before the filter calls:

```cpp
        eng.hpFilter.setDriveCharacterFromThreshold(_clipThresh);
        eng.lpFilter.setDriveCharacterFromThreshold(_clipThresh);
```

plus the same two calls for `hpFilterR`/`lpFilterR` inside the R-connected branch. (Plain stores — no per-sample math.)

- [ ] **Step 3: Tests**

Append to `tests/mf20/test_mf20.cpp` (register in `main`): a filter-level test that `setDriveCharacterFromThreshold(1/std::sqrt(d))` produces output identical to `setDriveCharacter(d)` for d ∈ {1, 2, 4, 8} (run 200 samples through two instances in both modes, compare exactly). The module-level smoothing behavior has no headless lane — compile-verified.

- [ ] **Step 4: Verify + commit**

All four lanes. The existing mf20 suite must stay green (filter-level `setDriveCharacter` untouched).

```bash
git add src/mf20 tests/mf20
git commit -m "fix: MF-20 immediate first modulate, R-poly count, smoothed drive"
```

---

### Task 4: Loooop/Lop shared control math

**Files:**
- Create: `src/loooop/LooperModuleDSP.hpp` (port from `806d935`)
- Modify: `src/loooop/Loooop.cpp`, `src/loooop/Lop.cpp` (call the helpers)
- Create: `tests/loooop/test_module_dsp.cpp` (port from `806d935`)

- [ ] **Step 1: Port the header and tests**

`git show 806d935:src/loooop/LooperModuleDSP.hpp > src/loooop/LooperModuleDSP.hpp` and `git show 806d935:tests/loooop/test_module_dsp.cpp > tests/loooop/test_module_dsp.cpp`. The header is Rack-free (uses `std::clamp`) — verified byte-equivalent formulas to our inline math (`knob+cv·0.4` clamp ±2; `knob·exp2(clamp(cv,±5))` clamp ±16; `knob+cv·0.1` clamp 0..1; pan `·0.2` clamp ±1; linear pan gains; dry/wet). `tests/run.sh` auto-discovers `test_*.cpp`, and this test needs no `.extra` file (header-only include).

- [ ] **Step 2: Rewire the modules**

Port the `Loooop.cpp`/`Lop.cpp` hunks from `git show 806d935 -- src/loooop/Loooop.cpp src/loooop/Lop.cpp`, adapting for our (unrelated, one-line) `onSampleRateChange` drift. Every replaced expression must be one of the verified-equivalent helpers — no formula changes.

- [ ] **Step 3: Verify + commit**

All four lanes (the loooop engine suite guards behavior; the new test file guards the helpers).

```bash
git add src/loooop tests/loooop
git commit -m "refactor: share Loooop/Lop control math in one tested header"
```

Optional follow-up inside this task if trivial (else note for later): use the same helpers in `metamodule/loooop/LoooopCore.cc`/`LopCore.cc` `updateHead`, which still inline the same math.

---

### Task 5: Waveform display cache (the MetaModule display win)

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp`, `LoopEngine.cpp` (waveform revision counter)
- Modify: `src/loooop/display/LoopWaveformRenderer.hpp` (split render; `geometry()`)
- Modify: `src/loooop/LoopDisplay.hpp` (VCV cache)
- Modify: `metamodule/loooop/LoooopCore.cc`, `metamodule/loooop/LopCore.cc` (MM cache)
- Test: `tests/loooop/test_display_renderer.cpp`, `tests/loooop/test_loop_engine.cpp` (port codex's additions)
- Optional: `tools/benchmark_loooop_renderer.cpp` + its docs (profiling scaffolding only)

**Porting sources — one squashed unit:**
1. `4f04486` (split + revision + caches) — the base.
2. `7b3824d` (geometry cap + release/acquire + bump-after-mutation) — **non-optional**: fixes an out-of-bounds lane write on small displays and sets correct memory ordering.
3. `d17e13d` — ONLY the `src/loooop/dsp/LoopEngine.*` hunks (`bumpWaveformRevision()` = non-atomic counter + single atomic store). Its other hunks are codex reverb work — superseded, do not take.

Since our branch never touched the renderer or the display widgets, start from codex's END-STATE files where possible: `git show 7b3824d:src/loooop/display/LoopWaveformRenderer.hpp`, etc. For `LoopEngine.{hpp,cpp}` — which HAS drifted on our branch (setSampleRate, NaN guard, minWinLen_, bounds threading) — apply codex's revision-counter additions by hand:

- [ ] **Step 1: LoopEngine revision counter**

Add to `LoopEngine.hpp` (private, near the display atomics), matching codex's post-`7b3824d`/`d17e13d` end state:

```cpp
    // Waveform-cache invalidation: bumped (release) after any change to the
    // peak arrays so display hosts re-render the static waveform only when
    // recorded audio actually changed. Only the audio thread writes; the
    // counter is kept non-atomic and published with a single atomic store.
    std::uint32_t waveformRevisionCounter_ = 0;
    std::atomic<std::uint32_t> waveformRevision_{0};
    void bumpWaveformRevision() {
        waveformRevision_.store(++waveformRevisionCounter_, std::memory_order_release);
    }
public:
    std::uint32_t waveformRevision() const {
        return waveformRevision_.load(std::memory_order_acquire);
    }
```

Call `bumpWaveformRevision()` at the END of `reset()`, `clear()`, and `writePeak()` (after the mutations — the release-store publishes the non-atomic peak writes). Verify against `git show 7b3824d -- src/loooop/dsp/` for exact placement. Our `setSampleRate()` retune path correctly does NOT bump (peaks unchanged); its empty path delegates to `reset()` which does.

- [ ] **Step 2: Renderer split + geometry**

Take codex's end-state `LoopWaveformRenderer.hpp` (from `7b3824d`): `geometry(height, numHeads)` (caps `laneH` to `height/numHeads`, guarantees `waveHeight + lanesHeight == height`), `renderWaveform()`, `renderLanes()`, thin composed `render()`.

- [ ] **Step 3: Host caches**

Port the `LoopDisplay.hpp` and `LoooopCore.cc`/`LopCore.cc` hunks (from `4f04486` + `7b3824d` end state). MetaModule specifics to preserve: `cachedWaveRevision_ = UINT32_MAX` reset in `show_graphic_display` (guards buffer swap), lanes rendered into the persistent canvas below the cached waveform region. Adapt the core files for our `ROBOTBOY_BRAND` rename (cache logic is in `draw_graphic_display`, away from those lines). Improvement worth making during the port (flagged by analysis, optional): render the waveform straight into the destination buffer's wave region on invalidation instead of keeping the redundant `waveCache_` vector + copy.

- [ ] **Step 4: Tests**

Port codex's test additions: `test_split_render_matches_composed_render` and the tiny-heights canary test (`git show 7b3824d d7d892e -- tests/loooop/test_display_renderer.cpp`), and `test_waveform_revision_tracks_peak_changes_only` (`git show d7d892e -- tests/loooop/test_loop_engine.cpp`). Optional: `tools/benchmark_loooop_renderer.cpp` + `docs/code-review/loooop-renderer-benchmark.md` (`7dece9d`/`f934a25` end state) — profiling only, not load-bearing.

- [ ] **Step 5: Simulator smoke test**

The cache's one correctness dependency is canvas persistence between `draw_graphic_display` calls on MetaModule. After all lanes pass, run the MetaModule simulator (`build-simulator` skill) with Loooop on screen: record a loop, freeze, confirm the waveform still draws correctly across frames and after switching modules (buffer-swap path).

- [ ] **Step 6: Verify + commit**

All four lanes + the simulator check.

```bash
git add src/loooop metamodule/loooop tests/loooop tools docs/code-review
git commit -m "perf: cache static looper waveform, re-render only playhead lanes"
```

---

### Task 6: Grain-timing unification (deliberate port, listening test)

**Files:**
- Modify: `src/vendor/beads_dsp/src/grain/grain_engine.cpp` (and header if codex adds decls)
- Test: port relevant additions from `ed7beb9`'s tests

- [ ] **Step 1: Port with drift care**

Study `git show ed7beb9`. Three sub-changes: (a) extract `NormalizedGrainSize`/`GrainDurationSeconds` helpers replacing the two duplicated SIZE→duration blocks (in our file at roughly `grain_engine.cpp:86-100` and `:205-213`); (b) replace the strided overlap-count `OnePole` loop (\~`:271-272`) with the exact block coefficient `1-(1-c)^n`; (c) hoist `DensityToRate`'s exponent to `static const`. Our `grain_engine.cpp` drifted (per-sample feedback, chunked Process, DTC deletion) — apply by hand, not cherry-pick. Sub-change (b) alters audio (smoother trajectory) — keep it in this task but verify with the listening step; if it proves audible in a bad way, land (a)+(c) alone and drop (b).

- [ ] **Step 2: Verify + listen**

Beads lane green (`test_density_rate.cpp`, `test_grain.cpp` cover this area). Then a listening check in VCV: dense grain cloud (Density high, Size mid), sweep Density — no pumping or stepping in overlap loudness.

- [ ] **Step 3: Commit**

```bash
git add src/vendor/beads_dsp tests/beads
git commit -m "refactor: unify grain SIZE mapping, exact block overlap smoothing"
```

---

### Task 7: Small-batch — LED decay SR-independence, deferred ClearBuffer

**Files:**
- Modify: `src/particules/particules_block_runtime.h`
- Modify: `src/particules/Particules.cpp`
- Test: `tests/beads/test_particules_block_runtime.cpp` (decay-rate test, adapt from `ee606e6`)

- [ ] **Step 1: LED decay (replaces the magic static, too)**

`particules_block_runtime.h` — replace the `static const` in `DecayGrainLed` with a member configured from the sample rate (per-second decay constant matches the old behavior at 48 kHz):

```cpp
    // Per-block LED decay factor for the current sample rate. Old code used a
    // fixed 0.9999^BlockSize, which decayed twice as fast (wall-clock) at 96 kHz.
    // f = 0.9999^(48000·BlockSize/sr) keeps the per-second decay constant.
    void ConfigureSampleRate(float sample_rate) {
        float sr = sample_rate > 0.f ? sample_rate : 48000.f;
        grain_led_decay_ = std::pow(0.9999f, 48000.f * static_cast<float>(BlockSize) / sr);
    }
```

with member `float grain_led_decay_ = 0.9999f;` (≈ the 48 kHz/BlockSize=1 value; MM's 64-block value arrives via ConfigureSampleRate) and `DecayGrainLed` using `grain_led_ *= grain_led_decay_;`. Call `ConfigureSampleRate` from the Particules constructor (with `APP->engine->getSampleRate()`, already fetched there) and from `onSampleRateChange` (note: `onSampleRateChange` currently resets `block_runtime_` by assignment — call `ConfigureSampleRate(e.sampleRate)` AFTER that reset). Compare against `git show ee606e6` for the test shape.

- [ ] **Step 2: Deferred ClearBuffer**

`Particules.cpp` — add member `std::atomic<bool> clear_requested_{false};`. Menu item body `module->processor_.ClearBuffer();` → `module->clear_requested_.store(true);`. In `process()`, inside the `BlockReady()` branch before `updateSlowParams`:

```cpp
			if (clear_requested_.exchange(false))
				processor_.ClearBuffer();
```

`onReset` keeps its direct call (engine lock held there).

- [ ] **Step 3: Verify + commit**

All four lanes.

```bash
git add src/particules tests/beads
git commit -m "fix: SR-independent grain LED decay; defer menu ClearBuffer to audio thread"
```

---

## Final verification

- [ ] All four lanes green from a clean state (`rm -rf tests/beads/build` first).
- [ ] MetaModule simulator smoke test for the display cache (Task 5 Step 5) done.
- [ ] Update `code-review-2026-07-08.md`: mark closed items (SVF triplication, FTZ, drive smoothing, first-modulate, R-poly, SIZE→duration dup, Loooop/Lop dedup, magic-static kDecay) and add any new findings. Commit docs.
