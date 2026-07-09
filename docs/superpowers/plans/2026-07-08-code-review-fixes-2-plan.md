# Code-Review Fixes Round 2 — Implementation Plan

> **STATUS (2026-07-09): COMPLETE.** All 6 tasks implemented, task-reviewed, and merged into `code-review-fixes` (commits bf4ced8, 0c0c90f, 733e3ed, 051c6fd, c6a1189, ef0c576). Final whole-branch review: ready to merge, no Critical/Important findings. Open follow-ups were folded into `code-review-2026-07-08.md`'s categorized "Still to do" lists.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the two remaining major review findings (Loooop/Lop sample-rate data loss, MF-20 NaN poisoning), add NaN input guards and Particules UX minors, then land the MetaModule CPU optimizations and dead-code sweeps.

**Architecture:** All DSP fixes go into the platform-agnostic engine classes (`LoopEngine`, `MF20Filter`, `VoiceEngine`, beads_dsp) where they are unit-testable; module `.cpp` files only re-wire calls. Perf work moves per-sample transcendentals to the \~2.5 ms modulate rate (MF-20) or hoists constants (Loooop, beads).

**Tech Stack:** C++20, VCV Rack SDK (`~/Dev/Rack-SDK`), three test lanes: `tests/run.sh` (plain g++ unit tests), `tests/beads/run.sh` (CMake/Catch2), plus compile checks `make -C vcv` and `cmake --build metamodule/build`.

**Spec:** `docs/superpowers/plans/2026-07-08-code-review-fixes-2-spec.md`

## Global Constraints

- Branch: `code-review-fixes`. Commit at the end of each task; messages are one short sentence (≤15 words), **no Co-Authored-By / AI attribution lines**.
- Never zero-fill or reallocate the \~23 MB loop buffers on any path reachable while a loop exists (MetaModule audio-deadline hazard).
- Behavior-preserving refactors (Tasks 4–6) must keep every existing test green with unchanged tolerances.
- `MF20Filter::process(in, cutoffHz, res)` public signature must survive (tests depend on it).
- Both build targets compile the same `src/` tree — any deleted file must also leave `metamodule/CMakeLists.txt` (explicit list); `vcv/Makefile` and `tests/beads/CMakeLists.txt` use globs and follow automatically.
- Verification commands and expected results:
  - `cd tests && ./run.sh` → all `ok:`/`PASS` lines, exit 0
  - `cd tests/beads && ./run.sh` → `100% tests passed`, exit 0
  - `make -C vcv -j8` → builds `plugin.dylib`, exit 0
  - `cmake --build metamodule/build -j8` → exit 0 (if the MM toolchain is unavailable in this environment, note it in the task report rather than failing the task)

---

### Task 1: LoopEngine sample-rate change preserves the loop

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (public API + members)
- Modify: `src/loooop/dsp/LoopEngine.cpp` (reset, new setSampleRate)
- Modify: `src/loooop/Loooop.cpp:84-86` (`onSampleRateChange`)
- Modify: `src/loooop/Lop.cpp:58-60` (`onSampleRateChange`)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Produces: `void LoopEngine::setSampleRate(float sampleRate)` — retunes without destroying a recorded loop; new private member `float maxSeconds_ = 60.f;`. Task 5 later adds `minWinLen_` recomputation inside this same function.

- [ ] **Step 1: Write the failing tests**

Append to `tests/loooop/test_loop_engine.cpp` (match the file's existing `check`/`near`/`soloHead0` helpers; register each new test in `main` alongside the others):

```cpp
static void test_sample_rate_change_preserves_loop() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f}) e.process(x);
    e.toggleRecord();
    e.setSampleRate(20.f);   // retune only: loop and buffer must survive
    check(e.loopLength() == 4, "sr_change: loop length preserved");
    check(near(e.process(0.f), 1.f), "sr_change: out[0]==1");
    check(near(e.process(0.f), 2.f), "sr_change: out[1]==2");
    check(near(e.process(0.f), 3.f), "sr_change: out[2]==3");
    check(near(e.process(0.f), 4.f), "sr_change: out[3]==4");
}

static void test_sample_rate_change_mid_recording() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    e.process(1.f); e.process(2.f);
    e.setSampleRate(20.f);
    check(e.isRecording(), "sr_change mid-rec: still recording");
    e.process(3.f); e.process(4.f);
    e.toggleRecord();
    check(e.loopLength() == 4, "sr_change mid-rec: length 4");
    check(near(e.process(0.f), 1.f), "sr_change mid-rec: content preserved");
}

static void test_sample_rate_change_empty_reallocates() {
    LoopEngine e;
    e.reset(10.f, 1.f);      // maxSamples = 10
    soloHead0(e);
    e.setSampleRate(20.f);   // no loop -> full reset, maxSamples = 20
    e.toggleRecord();
    for (int i = 0; i < 25; ++i) e.process(1.f);
    check(e.loopLength() == 20, "sr_change empty: ceiling at new rate");
    check(!e.isRecording(),    "sr_change empty: auto-stopped at new ceiling");
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cd tests && ./run.sh`
Expected: compile error — `setSampleRate` is not a member of `LoopEngine`.

- [ ] **Step 3: Implement**

`LoopEngine.hpp` — after `void reset(...)`:

```cpp
    // Retune to a new sample rate WITHOUT destroying a recorded loop: the
    // loop plays back repitched. Only reallocates (full reset) when there is
    // nothing to lose (no loop, not recording) — never audio-adjacent with
    // content in the buffer (see the clear() comment for why that matters).
    void setSampleRate(float sampleRate);
```

and a private member next to `sampleRate_`:

```cpp
    float maxSeconds_ = 60.f;
```

`LoopEngine.cpp` — first line of `reset()` body: `maxSeconds_ = maxSeconds;`. New function after `reset()`:

```cpp
void LoopEngine::setSampleRate(float sampleRate) {
    if (loopLen_ == 0 && !recording_) {
        reset(sampleRate, maxSeconds_);   // nothing to lose; size for the new rate
        return;
    }
    sampleRate_ = sampleRate;
    xfadeSamples_ = static_cast<std::uint32_t>(0.005f * sampleRate + 0.5f);
}
```

`Loooop.cpp` and `Lop.cpp` — in `onSampleRateChange`, replace `engine.reset(e.sampleRate);` with `engine.setSampleRate(e.sampleRate);`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd tests && ./run.sh` — all tests pass, including the three new ones.

- [ ] **Step 5: Compile both plugin targets**

Run: `make -C vcv -j8` and `cmake --build metamodule/build -j8`. Both exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/loooop tests/loooop
git commit -m "fix: preserve recorded loop across sample-rate changes"
```

---

### Task 2: MF-20 NaN/inf recovery

**Files:**
- Modify: `src/mf20/MF20Filter.hpp` (add `stateFinite()`)
- Modify: `src/mf20/engine.hpp` (add `VoiceEngine::sanitize()`)
- Modify: `src/mf20/MF20Filter.cpp` (call `sanitize()` in `modulate()`)
- Test: `tests/mf20/test_mf20.cpp`

**Interfaces:**
- Produces: `bool MF20Filter::stateFinite() const`; `void VoiceEngine::sanitize()`. Task 4 modifies the same files and must keep both.

- [ ] **Step 1: Write the failing tests**

Append to `tests/mf20/test_mf20.cpp` (uses the file's `report()` helper; add `#include "../../src/mf20/engine.hpp"` at the top next to the existing include, and register the new tests in `main`):

```cpp
static void test_nan_recovery() {
    printf("\nNaN recovery (stateFinite + reset)\n");
    for (auto mode : {MF20Filter::Mode::OTA, MF20Filter::Mode::K35}) {
        const char* mn = (mode == MF20Filter::Mode::OTA) ? "OTA" : "K35";
        MF20Filter f;
        f.setSampleRate(48000.f);
        f.setMode(mode);
        for (int i = 0; i < 200; ++i) f.process(0.5f, 1000.f, 0.5f);
        char b1[64]; snprintf(b1, sizeof(b1), "%s: finite after normal use", mn);
        report(f.stateFinite(), b1);
        f.process(NAN, 1000.f, 0.5f);
        char b2[64]; snprintf(b2, sizeof(b2), "%s: non-finite after NaN input", mn);
        report(!f.stateFinite(), b2);
        f.reset();
        bool ok = f.stateFinite();
        for (int i = 0; i < 200; ++i) {
            auto [lp, bp, hp] = f.process(0.5f, 1000.f, 0.5f);
            ok = ok && std::isfinite(lp) && std::isfinite(bp) && std::isfinite(hp);
        }
        char b3[64]; snprintf(b3, sizeof(b3), "%s: finite output after reset", mn);
        report(ok, b3);
    }
}

static void test_voice_sanitize() {
    printf("\nVoiceEngine::sanitize recovers poisoned voice\n");
    VoiceEngine v;
    v.setSampleRate(48000.f);
    v.hpFilter.process(NAN, 1000.f, 0.5f);      // poison one filter
    v.sanitize();
    bool ok = v.hpFilter.stateFinite();
    for (int i = 0; i < 200; ++i) {
        auto [lp, bp, hp] = v.hpFilter.process(0.5f, 1000.f, 0.5f);
        ok = ok && std::isfinite(lp);
        (void)bp; (void)hp;
    }
    report(ok, "sanitize() resets non-finite filter state");
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cd tests && ./run.sh`
Expected: compile error — `stateFinite` not a member.

- [ ] **Step 3: Implement**

`MF20Filter.hpp` — next to `reset()`:

```cpp
    /** False once any non-finite value has entered the TPT state (the
        s = 2·mid − s update propagates NaN/inf forever). */
    bool stateFinite() const { return std::isfinite(s1) && std::isfinite(s2); }
```

`engine.hpp` — in `VoiceEngine`, after `reset()`:

```cpp
    // NaN/inf recovery: one bad upstream sample would otherwise poison the
    // filter state permanently. Called once per modulate block (~2.5 ms) by
    // the module. Resets only the integrator states; the slew smoothers stay
    // (their inputs are clamped params, so they are finite), avoiding a
    // spurious parameter sweep after recovery.
    void sanitize() {
        if (!lpFilter.stateFinite()  || !hpFilter.stateFinite() ||
            !lpFilterR.stateFinite() || !hpFilterR.stateFinite()) {
            lpFilter.reset();
            hpFilter.reset();
            lpFilterR.reset();
            hpFilterR.reset();
        }
    }
```

`MF20Filter.cpp` — in `modulate()`, immediately after the `if (!eng) continue;` line in the per-voice loop:

```cpp
            eng->sanitize();
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd tests && ./run.sh` — all pass.

- [ ] **Step 5: Compile both plugin targets**

Run: `make -C vcv -j8` and `cmake --build metamodule/build -j8`.

- [ ] **Step 6: Commit**

```bash
git add src/mf20 tests/mf20
git commit -m "fix: MF-20 recovers from NaN/inf filter state"
```

---

### Task 3: NaN input guards + Particules UX minors

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp` (`process()`)
- Modify: `src/particules/Particules.cpp`
- Modify: `src/plugin.hpp:27-29` (`kQualityColors`)
- Test: `tests/loooop/test_loop_engine.cpp`

**Interfaces:**
- Consumes: nothing new. Produces: no API changes (Particules gains two private `dsp::SchmittTrigger` members: `freeze_gate_`, `seed_gate_`).

- [ ] **Step 1: Write the failing LoopEngine test**

Append to `tests/loooop/test_loop_engine.cpp` (register in `main`):

```cpp
static void test_nan_input_recorded_as_zero() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    e.process(1.f); e.process(NAN); e.process(3.f); e.process(4.f);
    e.toggleRecord();
    check(near(e.process(0.f), 1.f), "nan guard: out[0]==1");
    check(near(e.process(0.f), 0.f), "nan guard: NaN recorded as 0");
    check(near(e.process(0.f), 3.f), "nan guard: out[2]==3");
    check(near(e.process(0.f), 4.f), "nan guard: out[3]==4");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests && ./run.sh`
Expected: FAIL on "NaN recorded as 0" (NaN propagates into the buffer today).

- [ ] **Step 3: Implement the LoopEngine guard**

`LoopEngine.cpp`, first lines of `process(float inL, float inR, ...)`:

```cpp
    // Module boundary NaN guard: a non-finite input would be recorded into
    // the loop (and summed forever by overdub) until the user hits Clear.
    if (!std::isfinite(inL)) inL = 0.f;
    if (!std::isfinite(inR)) inR = 0.f;
```

Run: `cd tests && ./run.sh` — passes.

- [ ] **Step 4: Particules wrapper fixes**

All in `src/particules/Particules.cpp` except the color table.

(a) Members — add next to `prev_quality_button_`:

```cpp
	dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on FREEZE gate
	dsp::SchmittTrigger seed_gate_;     // same for SEED
```

(b) `process()` — replace the first two lines and the SEED latch line:

```cpp
		bool freeze_button = params[FREEZE_PARAM].getValue() > 0.5f;
		freeze_gate_.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
		bool frozen = freeze_button || freeze_gate_.isHigh();

		// Latch the SEED gate every sample so short triggers survive the
		// block boundary on MetaModule (updateSlowParams runs once per block).
		seed_gate_.process(inputs[SEED_INPUT].getVoltage(), 0.1f, 1.f);
		block_runtime_.NoteSeedGateSample(seed_gate_.isHigh());
```

(c) NaN guard on the audio inputs (feeds a feedback recorder) — replace the two input lines:

```cpp
		float l = inputs[IN_L_INPUT].getVoltage() * 0.2f;
		if (!std::isfinite(l)) l = 0.f;
		float r = in_r_connected ? inputs[IN_R_INPUT].getVoltage() * 0.2f : l;
		if (!std::isfinite(r)) r = 0.f;
```

(d) Freeze light shows CV-driven freeze too — replace the light line:

```cpp
		lights[FREEZE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);
```

(e) Randomize protection — in the constructor replace the FREEZE/QUALITY `configParam` calls:

```cpp
		// configSwitch does NOT disable randomization (only configButton does),
		// so FREEZE needs the explicit flag or Ctrl+R randomly latches freeze.
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"})
			->randomizeEnabled = false;
		configButton<QualityParamQuantity>(QUALITY_PARAM, "Quality");
```

(f) `onReset` — add after `needs_calibration_ = true;`:

```cpp
		processor_.ClearBuffer();   // 4 s buffer, feedback path, reverb tail
```

(g) `dataFromJson` — clamp manual gain:

```cpp
		if ((j = json_object_get(root, "manualGainDb")))
			manual_gain_db_ = clamp((float)json_real_value(j), 0.f, 32.f);
```

(h) `src/plugin.hpp` — LED colors per the manual (white/cyan/amber/magenta):

```cpp
static constexpr float kQualityColors[4][3] = {
	{1.f, 1.f, 1.f}, {0.f, 1.f, 1.f}, {1.f, 0.5f, 0.f}, {1.f, 0.f, 1.f},
};
```

- [ ] **Step 5: Verify**

Run: `cd tests && ./run.sh`, then `make -C vcv -j8` and `cmake --build metamodule/build -j8`. All green (the Particules changes are compile-verified; no headless lane covers module wiring).

- [ ] **Step 6: Commit**

```bash
git add src/loooop src/particules src/plugin.hpp tests/loooop
git commit -m "fix: NaN input guards, Particules freeze/seed hysteresis and UX minors"
```

---

### Task 4: MF-20 — transcendentals to modulate rate, dead pool code

**Files:**
- Modify: `src/mf20/MF20Filter.hpp` (`cutoffToG`, `processG`, `processVCVG`, OTA hoist)
- Modify: `src/mf20/engine.hpp` (g-domain smoothers, by-value pool, delete dead code)
- Modify: `src/mf20/MF20Filter.cpp` (modulate-rate g, `_driveSqrt`, deterministic dither)
- Test: `tests/mf20/test_mf20.cpp`

**Interfaces:**
- Consumes: `stateFinite()`/`sanitize()` from Task 2 (keep them working).
- Produces:
  - `static float MF20Filter::cutoffToG(float cutoffHz, float fs)` — `tan(π·clamp(fc,1,fs·0.498)/fs)`
  - `Out MF20Filter::processG(float in, float g, float res)` and `Out processVCVG(float inVolts, float g, float res)`
  - `MF20Filter::process(in, cutoffHz, res)` unchanged signature, now `return processG(in, cutoffToG(cutoffHz, sampleRate), res);`
  - `VoiceEngine`: `lpGSlew`/`hpGSlew` + `lpGTarget`/`hpGTarget` replace the log₂-Hz cutoff smoothers/targets (res smoothers unchanged)
  - `EnginePool`: `VoiceEngine engines[16]` **by value**; `setVoices`, `setSampleRate`, `resetAll` only

- [ ] **Step 1: Write the failing test**

Append to `tests/mf20/test_mf20.cpp` (register in `main`):

```cpp
static void test_processG_matches_process() {
    printf("\nprocessG(cutoffToG(fc)) == process(fc)\n");
    for (auto mode : {MF20Filter::Mode::OTA, MF20Filter::Mode::K35}) {
        MF20Filter a, b;
        a.setSampleRate(48000.f); b.setSampleRate(48000.f);
        a.setMode(mode); b.setMode(mode);
        bool ok = true;
        for (int i = 0; i < 500; ++i) {
            float in = std::sin(2.f * float(M_PI) * 220.f * i / 48000.f);
            float fc = 200.f + 30.f * i;
            auto ra = a.process(in, fc, 0.6f);
            auto rb = b.processG(in, MF20Filter::cutoffToG(fc, 48000.f), 0.6f);
            ok = ok && ra.lp == rb.lp && ra.bp == rb.bp && ra.hp == rb.hp;
        }
        report(ok, mode == MF20Filter::Mode::OTA ? "OTA identical" : "K35 identical");
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests && ./run.sh` — compile error: no `cutoffToG`/`processG`.

- [ ] **Step 3: Implement `MF20Filter.hpp` changes**

Public section:

```cpp
    /** Bilinear prewarp gain g = tan(π·fc/fs), fc clamped to [1, fs·0.498).
        Hosts call this at modulate rate and slew g itself, keeping tan/pow
        out of the audio path entirely. */
    static float cutoffToG(float cutoffHz, float fs) {
        float fc = std::clamp(cutoffHz, 1.f, fs * 0.498f);
        return std::tan(kPi * fc / fs);
    }

    /** Process one sample with a precomputed prewarp gain (see cutoffToG). */
    Out processG(float in, float g, float res) {
        if (mode == Mode::K35)
            return processK35(in, g, res);
        return processOTA(in, g, res);
    }

    Out processVCVG(float inVolts, float g, float res) {
        auto [lp, bp, hp] = processG(inVolts * kVCVScale, g, res);
        return { lp / kVCVScale, bp / kVCVScale, hp / kVCVScale };
    }
```

`process()` becomes a wrapper: `return processG(in, cutoffToG(cutoffHz, sampleRate), res);` (`processVCV` is unchanged — it already delegates to `process`). Note `kPi` must be declared before `cutoffToG` in the class (move the `static constexpr` block above the public methods if needed).

`processOTA`/`processK35` change signature to `(float in, float g, float res)`: delete their first two lines (`fc` clamp and `g = tan(...)`); everything else identical. In `processOTA`, hoist the duplicated square:

```cpp
        const float onePlusG2 = (1.f + g) * (1.f + g);
        float rhs = s1 + g * (in - s2);
        float D1    = onePlusG2 - g * k;
        ...
            float D2     = onePlusG2 - satSlope * g * k;
```

- [ ] **Step 4: Implement `engine.hpp` changes**

`VoiceEngine`: replace the two cutoff smoothers and targets (res pair unchanged):

```cpp
    // Cutoff smoothing happens in the g (prewarp-gain) domain: modulate()
    // computes tan/exp2 at ~2.5 ms intervals and the audio path only slews g.
    // Init values ≈ the 750 Hz / 120 Hz defaults at 48 kHz; the first
    // modulate() corrects them within 2.5 ms.
    OnePoleSmoother lpGSlew { 0.0491f };
    OnePoleSmoother hpGSlew { 0.0079f };
    OnePoleSmoother lpResSlew { 0.25f };
    OnePoleSmoother hpResSlew { 0.25f };

    float lpGTarget   = 0.0491f;
    float hpGTarget   = 0.0079f;
    float lpResTarget = 0.25f;
    float hpResTarget = 0.25f;
```

`reset()` resets the renamed smoothers/targets to those same values. `sanitize()` (Task 2) is untouched.

`EnginePool` becomes by-value, dead code removed (delete `processVoice`, the `sampleRate` member, constructor, destructor; keep `resetAll`):

```cpp
// Manages the per-voice engines for polyphonic use. All 16 voices live by
// value (no heap, no null checks, better cache behavior on MetaModule);
// setVoices() only changes activeVoices, so the audio thread never allocates
// and every voice always carries the pool's current sample rate.
struct EnginePool {
    VoiceEngine engines[16];
    int activeVoices = 1;

    void setVoices(int n) { activeVoices = std::clamp(n, 1, 16); }

    void setSampleRate(float fs) {
        for (auto& e : engines) e.setSampleRate(fs);
    }

    void resetAll() {
        for (auto& e : engines) e.reset();
    }
};
```

Update the header comment at the top of the file (drop the "array of pointers / processVoice used by tests" sentences).

- [ ] **Step 5: Implement `MF20Filter.cpp` changes**

Members — replace `float _drive = 1.f;` block:

```cpp
    // Shared modulation targets (same for all voices; per-voice CV added in modulate()).
    float _drive = 1.f;
    float _driveSqrt = 1.f;          // hoisted out of the per-sample OTA pre-gain
    float _sampleRate = 44100.f;     // mirrors the engine rate for modulate-rate math
    // Deterministic denormal-prevention dither: alternates sign each sample.
    // Replaces the RNG dither — cheaper, and bit-reproducible between the
    // VCV and MetaModule builds (relevant to headless comparison testing).
    float _dither = 1e-9f;
```

`onSampleRateChange`: set `_sampleRate = e.sampleRate;` first; replace the pointer loop:

```cpp
        _sampleRate = e.sampleRate;
        _pool.setSampleRate(e.sampleRate);
        float alpha = smootherAlpha(e.sampleRate, 0.005f);
        for (auto& eng : _pool.engines) {
            eng.lpGSlew.setAlpha(alpha);
            eng.hpGSlew.setAlpha(alpha);
            eng.lpResSlew.setAlpha(alpha);
            eng.hpResSlew.setAlpha(alpha);
        }
        _modulationSteps = static_cast<int>(e.sampleRate * 0.0025f);  // 2.5 ms
```

`onReset`: body becomes `Module::onReset(e); _pool.resetAll();`.

`modulate()`: add `_driveSqrt = std::sqrt(drive);` after `_drive = drive;`. Per-voice loop uses references and computes g targets (20 Hz floor preserved from the old per-sample clamp):

```cpp
        for (int c = 0; c < _pool.activeVoices; c++) {
            VoiceEngine& eng = _pool.engines[c];
            eng.sanitize();

            float totalOffset = 0.f;
            if (totalCvConn)
                totalOffset = totalCvAtten * inputs[TOTAL_CUTOFF_INPUT].getPolyVoltage(c);

            float voiceCutoffLog = cutoffLog + totalOffset;
            if (lpCvConn)
                voiceCutoffLog += lpCvAtten * inputs[LP_CUTOFF_INPUT].getPolyVoltage(c);
            float lpHz = clamp(std::exp2(voiceCutoffLog), 20.f, _sampleRate * 0.498f);
            eng.lpGTarget = MF20Filter::cutoffToG(lpHz, _sampleRate);

            float voiceHpLog = hpLog + totalOffset;
            if (hpCvConn)
                voiceHpLog += hpCvAtten * inputs[HP_CUTOFF_INPUT].getPolyVoltage(c);
            float hpHz = clamp(std::exp2(voiceHpLog), 20.f, _sampleRate * 0.498f);
            eng.hpGTarget = MF20Filter::cutoffToG(hpHz, _sampleRate);

            eng.lpResTarget = res;
            eng.hpResTarget = hpResRaw;

            eng.lpFilter.setMode(_filterMode);
            eng.hpFilter.setMode(_filterMode);
            eng.lpFilterR.setMode(_filterMode);
            eng.hpFilterR.setMode(_filterMode);
            eng.lpFilter.setDriveCharacter(drive);
            eng.lpFilterR.setDriveCharacter(drive);
            eng.hpFilter.setDriveCharacter(drive);
            eng.hpFilterR.setDriveCharacter(drive);
        }
```

(The Task 2 `eng->sanitize();` line becomes `eng.sanitize();` here. Keep the existing explanatory comments — Total-CV bus, resonance — where they were.)

`processChannel()` — no pow/tan/sqrt/RNG remain (keep the OTA double-drive NOTE comment; update the pre-gain comment's "√drive" wording only if it references the sqrt call):

```cpp
    void processChannel(const ProcessArgs& args, int c) {
        VoiceEngine& eng = _pool.engines[c];

        auto otaPreGain = [&](float x) {
            if (_filterMode != MF20Filter::Mode::OTA) return x;
            float d = x * _driveSqrt;
            return (d >  5.f) ?  5.f + 0.25f * (d - 5.f)
                 : (d < -5.f) ? -5.f + 0.25f * (d + 5.f)
                 : d;
        };

        float in = inputs[AUDIO_INPUT].getPolyVoltage(c);
        in += _dither;
        in = otaPreGain(in);

        float gLp      = eng.lpGSlew.process(eng.lpGTarget);
        float gHp      = eng.hpGSlew.process(eng.hpGTarget);
        float res      = eng.lpResSlew.process(eng.lpResTarget);
        float hpResRaw = eng.hpResSlew.process(eng.hpResTarget);

        res = resTaper(res);
        float hpRes = resTaper(hpResRaw);

        auto hpStage = eng.hpFilter.processVCVG(in,         gHp, hpRes);
        auto lpStage = eng.lpFilter.processVCVG(hpStage.hp, gLp, res);
        outputs[LP_OUTPUT].setVoltage(lpStage.lp, c);

        if (inputs[AUDIO_INPUT_R].isConnected()) {
            float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c);
            inR += _dither;
            inR = otaPreGain(inR);
            auto hpStageR = eng.hpFilterR.processVCVG(inR,          gHp, hpRes);
            auto lpStageR = eng.lpFilterR.processVCVG(hpStageR.hp,  gLp, res);
            outputs[LP_OUTPUT_R].setVoltage(lpStageR.lp, c);
        } else {
            outputs[LP_OUTPUT_R].setVoltage(lpStage.lp, c);
        }
    }
```

(Keep the R-mirroring comment.) `process()`: flip the dither once per sample before the channel loop:

```cpp
        _dither = -_dither;
        for (int c = 0; c < voices; c++)
            processChannel(args, c);
```

`args` in `processChannel` is now unused except for the signature — keep the signature (cheap, avoids churn) but drop any `args.sampleRate` references (there are none left).

- [ ] **Step 6: Run tests**

Run: `cd tests && ./run.sh` — the entire existing mf20 suite plus both new tests pass (the `process()` wrapper preserves every old behavior; `test_voice_sanitize` from Task 2 must still compile against the renamed smoothers — it doesn't touch them).

- [ ] **Step 7: Compile both plugin targets**

Run: `make -C vcv -j8` and `cmake --build metamodule/build -j8`.

- [ ] **Step 8: Commit**

```bash
git add src/mf20 tests/mf20
git commit -m "perf: MF-20 transcendentals at modulate rate; by-value voice pool"
```

---

### Task 5: Loooop — hoist window-bounds math off the per-sample path

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.hpp` (private signatures + `minWinLen_` member)
- Modify: `src/loooop/dsp/LoopEngine.cpp`

**Interfaces:**
- Consumes: `setSampleRate()` from Task 1 (adds `minWinLen_` recompute there).
- Produces: no public API change. Private: `readInterpolated(h, buf, winStart, winLen)`, `readHead(h, winStart, winLen, outL, outR)`, `advanceHead(h, idx, winStart, winLen)`; member `double minWinLen_ = 48.0;`. **Bit-identical refactor** — the existing seam/jitter/one-shot tests are the safety net.

- [ ] **Step 1: Add `minWinLen_`**

`LoopEngine.hpp` private members (next to `xfadeSamples_`):

```cpp
    double minWinLen_ = 48.0;   // ceil(sampleRate · 1 ms); set in reset()/setSampleRate()
```

`LoopEngine.cpp`:
- in `reset()`, after `xfadeSamples_ = ...`:

```cpp
    minWinLen_ = std::ceil(
        static_cast<double>(sampleRate) * MINIMUM_LOOP_MILLISECONDS / 1000.0);
```

- in `setSampleRate()` (the retune path, after `xfadeSamples_ = ...`): same two lines with `sampleRate` the parameter.
- in `windowBounds(...)`, replace the `const double minWinLen = std::ceil(...)` computation with `const double minWinLen = minWinLen_;` (or use the member directly).

- [ ] **Step 2: Run tests**

Run: `cd tests && ./run.sh` — all green (same values, computed once).

- [ ] **Step 3: Thread window bounds through the per-sample path**

`LoopEngine.hpp` — change the three private declarations:

```cpp
    float readInterpolated(const PlayHead& h, const std::vector<float>& buf,
                           double winStart, double winLen) const;
    void readHead(const PlayHead& h, double winStart, double winLen,
                  float& outL, float& outR) const;
    void advanceHead(PlayHead& h, int idx, double winStart, double winLen);
```

`LoopEngine.cpp`:
- `readInterpolated`: delete its `windowBounds` call; take `winStart`/`winLen` as parameters (body otherwise identical).
- `readHead`: delete its `windowBounds` call; use the parameters. The seam-preview `windowBounds(h, h.jitterNext, ns, nl)` call stays (different jitter offset). Pass the bounds to both `readInterpolated` calls.
- `advanceHead`: delete the leading `windowBounds` call; use the parameters for the snap-in, wrap, and display-store logic. The post-`commitJitter()` recomputes (`double ns, nl; windowBounds(h, ns, nl);`) stay — the window legitimately changes at the wrap.
- `process()` head loop computes bounds once and passes them down:

```cpp
        for (int i = 0; i < numHeads_; ++i) {
            PlayHead& h = heads_[i];
            if (!h.playing) continue;
            double winStart, winLen;
            windowBounds(h, winStart, winLen);
            float l, r; readHead(h, winStart, winLen, l, r);
            heads[i].l = l * h.level;
            heads[i].r = r * h.level;
            advanceHead(h, i, winStart, winLen);
        }
```

- `restartHead`/`jumpHead` keep their own `windowBounds` calls (control-rate).

- [ ] **Step 4: Run tests**

Run: `cd tests && ./run.sh` — every loooop test passes unchanged (identical values flow everywhere; any diff means the refactor broke something — fix, don't loosen tolerances).

- [ ] **Step 5: Compile both plugin targets**

Run: `make -C vcv -j8` and `cmake --build metamodule/build -j8`.

- [ ] **Step 6: Commit**

```bash
git add src/loooop
git commit -m "perf: compute Loooop window bounds once per head per sample"
```

---

### Task 6: Particules/beads — hot-path hoists and dead-code sweep

**Files:**
- Modify: `src/particules/particules_block_runtime.h` (pow hoist, redundant line)
- Modify: `src/particules/Particules.cpp` (dead members, dead param writes)
- Delete: `src/particules/Particules.hpp`
- Modify: `src/vendor/beads_dsp/src/beads_processor.cpp` (pow cache, DTC removal)
- Modify: `src/vendor/beads_dsp/src/input/auto_gain.cpp` (SoftClip, silence-gain hoist)
- Modify: `src/vendor/beads_dsp/src/util/dsp_utils.h` (delete dead functions)
- Modify: `src/vendor/beads_dsp/src/grain/grain.h`, `grain.cpp`, `grain_engine.h`, `grain_engine.cpp` (DTC + use_linear removal)
- Modify: `src/vendor/beads_dsp/include/beads/beads.h`, `include/beads/parameters.h`
- Delete: `src/vendor/beads_dsp/src/wavetable/` (whole directory)
- Modify: `metamodule/CMakeLists.txt:39` (drop wavetable entry)

**Interfaces:**
- Consumes: nothing from other tasks (Particules.cpp line numbers will have shifted after Task 3 — locate edits by content, not line).
- Produces: `BeadsProcessor::Init(void* memory, size_t size, float sample_rate)` becomes the only Init (all in-repo callers already use it). `BeadsParameters` loses `seed_connected` and `stereo_input`.

- [ ] **Step 1: CPU hoists**

(a) `src/particules/particules_block_runtime.h` — `DecayGrainLed()`:

```cpp
    void DecayGrainLed() {
        if (grain_led_ <= 0.0001f) {
            grain_led_ = 0.0f;
            return;
        }
        // pow of two compile-time constants; a per-sample powf in the VCV build.
        static const float kDecay = std::pow(0.9999f, static_cast<float>(BlockSize));
        grain_led_ *= kDecay;
    }
```

(b) `src/vendor/beads_dsp/src/beads_processor.cpp` (\~line 233) — cache the closed-form smoothing coefficient. Find the `Impl` struct (the type of `s`, which holds `smoothed_dry_wet`) and add two members:

```cpp
    size_t dry_wet_coeff_frames = 0;
    float  dry_wet_coeff = 0.0f;
```

then replace the block:

```cpp
    {
        // Closed-form one-pole advance for the block; the pow only reruns
        // when the host's block size changes (it never does within a run).
        if (num_frames != s.dry_wet_coeff_frames) {
            s.dry_wet_coeff_frames = num_frames;
            s.dry_wet_coeff =
                1.0f - std::pow(1.0f - 0.002f, static_cast<float>(num_frames));
        }
        s.smoothed_dry_wet += s.dry_wet_coeff * (s.params.dry_wet - s.smoothed_dry_wet);
    }
```

(c) `src/vendor/beads_dsp/src/input/auto_gain.cpp`:
- `SoftLimit` line 35: `std::tanh(excess / kHeadroom)` → `SoftClip(excess / kHeadroom)` (from `../util/dsp_utils.h`, already included). `SoftClip` is FastTanh clamped to ±1 and exact for |x| ≥ 3 — required here because hot inputs push the argument far past FastTanh's Padé validity range. Max deviation from `std::tanh` is \~0.5% near x = 3; confirm `tests/beads/test_auto_gain.cpp` passes unchanged.
- Line 118: hoist the constant threshold —

```cpp
        static const float kSilenceGain = FastDbToGain(kMinGainDb);
        bool is_silent = peak < kSilenceGain;
```

(the `FastDbToGain(gain_db)` calls at lines 91/106 have variable arguments and stay).

- [ ] **Step 2: Run the beads suite**

Run: `cd tests/beads && ./run.sh`
Expected: 100% tests passed. If `test_auto_gain.cpp` fails on the SoftClip swap, report the delta rather than loosening the test.

- [ ] **Step 3: Dead-code sweep — wrapper**

(a) `src/particules/Particules.cpp`: delete the stray `float grain_led_ = 0.f;` member (the real one lives in `particules_block_runtime.h`).
(b) `src/particules/particules_block_runtime.h`: in `PushInputSample`, delete the line `output_index_ = input_index_ + 1;` (already advanced in lockstep by `ReadOutputSample`; the assignment transiently sets an out-of-range value).
(c) Delete `src/particules/Particules.hpp` (`git rm`) — 2-line stub, referenced nowhere.

- [ ] **Step 4: Dead-code sweep — beads_dsp**

(a) `src/vendor/beads_dsp/src/util/dsp_utils.h`: delete `EqualPowerCrossfade` (line 28) and `FastPowUnit` (line 95) — zero callers repo-wide. Keep `FastTanh`/`SoftClip` (now used by auto_gain too) and everything else.

(b) Wavetable: `git rm -r src/vendor/beads_dsp/src/wavetable/` and delete its entry at `metamodule/CMakeLists.txt:39`. (VCV Makefile and tests CMake use globs — nothing to edit.)

(c) DTC path — always-null at runtime (every in-repo caller uses the 3-arg `Init`), zero test coverage:
- `include/beads/beads.h:36-37`: delete the 5-arg `Init` declaration.
- `src/beads_processor.cpp`: make the 3-arg `Init` the real implementation (today it delegates to the 5-arg one with `nullptr, 0`); delete the 5-arg body, the `use_dtc` pointer computation (\~lines 68-78), and the `SetDTCCache` call (\~line 97). If `GetMemoryRequirements`'s result struct has DTC-only fields (`dtc_bytes` or similar), check for references (Particules.cpp uses only `total_bytes`/`alignment`) and delete them if unreferenced.
- `src/grain/grain_engine.h`: delete `SetDTCCache` (line 31) and `dtc_cache_` (line 64).
- `src/grain/grain_engine.cpp`: delete the `if (dtc_cache_)` branch (\~lines 264-276) keeping the direct `ProcessBlock` path as the unconditional body; delete the `set_use_linear` call at line 241 (keep the `render_load_tier_` assignment above it — it still feeds `normalization_stride` at \~line 293).
- `src/grain/grain.h`: delete `struct GrainDTCCache` (line 16), `Grain::ProcessBlockCached` (line 204), `set_use_linear` (line 261), `use_linear_` (line 266).
- `src/grain/grain.cpp:11`: delete the `use_linear_` reset.
- Rationale note for the commit: adaptive interpolation (`use_linear`) was only honored by the deleted DTC path; if wanted later it's in git history.

(d) `include/beads/parameters.h`: delete `seed_connected` (line 42) and `stereo_input` (line 54). In `Particules.cpp` delete their two assignments (`params_.seed_connected = ...`, `params_.stereo_input = stereo_input_;`), the `if (!params_.freeze) stereo_input_ = ...` line, and the now-dead `bool stereo_input_ = false;` member.

- [ ] **Step 5: Verify all lanes**

Run, in order:
1. `cd tests/beads && ./run.sh` → 100% pass (GLOB re-runs CMake; if stale-cache errors mention wavetable, `rm -rf tests/beads/build` and rerun)
2. `cd tests && ./run.sh` → all pass
3. `make -C vcv -j8` → exit 0
4. `cmake --build metamodule/build -j8` → exit 0 (explicit source list was edited; a stale cache may need `cmake metamodule -B metamodule/build` first)

- [ ] **Step 6: Commit**

```bash
git add -A src/particules src/vendor/beads_dsp metamodule/CMakeLists.txt
git commit -m "perf: beads hot-path hoists; delete wavetable, DTC path, dead params"
```

---

## Final verification (after Task 6)

- [ ] All four lanes green (see Global Constraints).
- [ ] `git log --oneline main..code-review-fixes` shows the six new commits.
- [ ] Update `code-review-2026-07-08.md`: remove the fixed items (Loooop #1, MF-20 #1, Loooop #4, Particules #1/#3/#4/#5/#6/#7, the swept refactor/dead-code bullets, the addressed CPU bullets) and refresh the "Suggested order of work" list; note the LED-color change (code now matches the manual) and that `use_linear`/DTC were deleted rather than wired up. Commit: `docs: prune round-2 fixes from code review notes`.
