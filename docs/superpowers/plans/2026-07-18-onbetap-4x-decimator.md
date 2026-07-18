# Onbetap 4x Decimator Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 4x oversampling path's boxcar-average decimator with a two-stage FIR cascade so 2x and 4x can be compared like-for-like.

**Architecture:** New 9-tap stage-A FIR decimates 192k→96k; the existing per-voice `DecimFir13` instances (unused on the 4x path today) become stage B, 96k→48k, exactly as they run on the 2x path. The 2x and 1x paths are untouched. Spec: `docs/superpowers/specs/2026-07-18-onbetap-4x-decimator-design.md`.

**Tech Stack:** C++ (header-only DSP in `src/onbetap/`), g++ test lane (`tests/run.sh`), python venv (`~/Dev/python-scripts/.venv/`, numpy/scipy) for spectral measurement, vcv-headless + MM headless simulator for end-to-end checks.

## Global Constraints

- Branch: `main`, direct commits (user-approved).
- Commit messages: one short sentence (≤15 words), **no Co-Authored-By / AI attribution**.
- Scratch files (harnesses, WAVs, scripts) go in the session scratchpad under `onbetap-4x/`, never committed.
- The 1x and 2x code paths must remain byte-identical in behavior (criterion 3 of the spec).
- Stage-A coefficients (designed 2026-07-18, `scipy.signal.firwin(9, 45500, window=("kaiser", 3.25), fs=192000)`; measured: passband dev ≤0.096 dB to 20 kHz, stopband ≤−41.1 dB over 72–96 kHz):
  `{-0.0042816f, -0.0436724f, 0.0182510f, 0.2921273f, 0.4751513f, 0.2921273f, 0.0182510f, -0.0436724f, -0.0042816f}`
- Fallback 11-tap set, ONLY if Task 4's spur measurement misses its gate (`firwin(11, 44000, ("kaiser", 4.125), fs=192000)`, stopband ≤−49.6 dB):
  `{0.0040217f, -0.0102259f, -0.0487271f, 0.0307026f, 0.2944195f, 0.4596184f, 0.2944195f, 0.0307026f, -0.0487271f, -0.0102259f, 0.0040217f}`

---

### Task 1: Baseline renders (no code changes)

Capture end-to-end renders of the CURRENT build so Task 3 can prove the default (2x) path is bit-identical after the change.

**Files:**
- Create (scratchpad only): `<scratchpad>/onbetap-4x/in.wav`, `<scratchpad>/onbetap-4x/base_2x.wav`

**Interfaces:**
- Produces: the two WAVs above, consumed by Task 3's bit-identity check.

- [ ] **Step 1: Ensure the installed VCV plugin matches HEAD**

Invoke the `build-robotboy-plugin` skill (VCV target only): `make -C vcv`, then copy the dylib/plugin.json/res into the Rack2 plugins dir as the skill directs.

- [ ] **Step 2: Generate a deterministic test input**

```bash
source ~/Dev/python-scripts/.venv/bin/activate
python - <<'EOF'
import numpy as np, scipy.io.wavfile as wf
fs = 48000; n = 2 * fs
rng = np.random.default_rng(0xB0B)
sig = 0.6 * np.sin(2*np.pi*220*np.arange(n)/fs) + 0.2 * rng.standard_normal(n)
sig = np.clip(sig, -1, 1).astype(np.float32)
wf.write("<scratchpad>/onbetap-4x/in.wav", fs, np.stack([sig, sig], axis=1))
EOF
```

- [ ] **Step 3: Render through the installed plugin at default settings (2x)**

Invoke the `test-vcv-module-headless` skill: module `Onbetap`, input `onbetap-4x/in.wav`, 48 kHz, default params, output to `onbetap-4x/base_2x.wav`. No oversample override needed — the default is 2x.

- [ ] **Step 4: Sanity-check the render is non-silent**

```bash
python - <<'EOF'
import scipy.io.wavfile as wf, numpy as np
fs, d = wf.read("<scratchpad>/onbetap-4x/base_2x.wav")
print("rms", np.sqrt((d.astype(np.float64)**2).mean()))
EOF
```
Expected: rms clearly > 0. No commit (scratch only).

---

### Task 2: `DecimFir9` struct + voice state (TDD)

**Files:**
- Modify: `src/onbetap/engine.hpp` (new struct after `DecimFir13`, ~line 65; new `OnbetapVoice` members ~line 85)
- Test: `tests/onbetap/test_engine.cpp`

**Interfaces:**
- Produces: `struct DecimFir9 { static constexpr float h[9]; float z[9]; void reset(); float push(float x); }` and `OnbetapVoice` members `fir4LpL, fir4BpL, fir4HpL, fir4LpR, fir4BpR, fir4HpR` (all `DecimFir9`), cleared by `reset()` and `sanitize()`. Task 3 consumes these exact names.

- [ ] **Step 1: Write the failing tests**

Append inside `main()` of `tests/onbetap/test_engine.cpp`, before the final `printf`:

```cpp
    // DecimFir9 (stage-A decimator, 4x path): DC gain, impulse, reset
    {
        DecimFir9 f;
        float y = 0.f;
        for (int i = 0; i < 32; i++) y = f.push(1.f);
        CHECK(std::fabs(y - 1.f) < 1e-4f, "DecimFir9 DC gain ~1");
        f.reset();
        float imp[9];
        imp[0] = f.push(1.f);
        for (int i = 1; i < 9; i++) imp[i] = f.push(0.f);
        bool match = true;
        for (int i = 0; i < 9; i++)
            match = match && std::fabs(imp[i] - DecimFir9::h[i]) < 1e-6f;
        CHECK(match, "DecimFir9 impulse response = taps");
        f.reset();
        CHECK(f.push(0.f) == 0.f, "DecimFir9 reset clears state");
    }
    // sanitize() clears stage-A FIR state on NaN recovery
    {
        OnbetapVoice v;
        float g = OnbetapFilter::cutoffToG(1000.f, 192000.f);
        v.fir4LpL.push(1.f);
        v.fL.processG(std::nanf(""), g, 0.5f);
        v.sanitize();
        CHECK(v.fir4LpL.push(0.f) == 0.f, "sanitize clears stage-A FIRs");
    }
```

- [ ] **Step 2: Run to verify failure**

Run: `cd ~/Dev/RobotBoy/tests && ./run.sh 2>&1 | grep -A2 test_engine`
Expected: compile error, `DecimFir9` not declared.

- [ ] **Step 3: Implement**

In `src/onbetap/engine.hpp`, immediately after the `DecimFir13` struct:

```cpp
// 9-tap stage-A decimation FIR for the 4x oversampling path, run at
// fsOs = 192 kHz (48 kHz host), decimating 192k -> 96k ahead of the same
// DecimFir13 stage the 2x path uses (96k -> 48k) — so the 4x passband
// matches the 2x path by construction, and the 2x/4x comparison isolates
// the oversampling factor (spec: 2026-07-18-onbetap-4x-decimator-design).
// Folding-band math: after both decimations the audible 0-24k band is
// polluted only by content at 72-96 kHz (folds straight in) — 48-72 kHz
// folds to 24-48 kHz, which stage B's stopband already covers. So stage A
// needs strong attenuation only above 72 kHz, and with a transition band
// a quarter of the sample rate wide, 9 taps suffice.
// scipy.signal.firwin(9, 45500, window=("kaiser", 3.25), fs=192000):
// passband dev <= 0.096 dB to 20 kHz, stopband <= -41 dB over 72-96 kHz.
// Group delay 4 samples at 192k = 1 host sample (stage B adds ~3 more).
struct DecimFir9 {
    static constexpr float h[9] = {
        -0.0042816f, -0.0436724f, 0.0182510f, 0.2921273f, 0.4751513f,
         0.2921273f,  0.0182510f, -0.0436724f, -0.0042816f
    };
    float z[9] = {0.f};
    void reset() { for (auto& v : z) v = 0.f; }
    float push(float x) {
        for (int i = 8; i > 0; i--) z[i] = z[i - 1];
        z[0] = x;
        float y = 0.f;
        for (int i = 0; i < 9; i++) y += h[i] * z[i];
        return y;
    }
};
```

In `OnbetapVoice`, after the existing FIR member line (`DecimFir13 firLpL, ...`):

```cpp
    // Stage-A decimation FIRs (4x path only): 192k -> 96k ahead of the
    // DecimFir13s above, which serve as stage B at 96k on the 4x path.
    DecimFir9 fir4LpL, fir4BpL, fir4HpL, fir4LpR, fir4BpR, fir4HpR;
```

In `OnbetapVoice::reset()`, after the `firLpR.reset(); ...` line:

```cpp
        fir4LpL.reset(); fir4BpL.reset(); fir4HpL.reset();
        fir4LpR.reset(); fir4BpR.reset(); fir4HpR.reset();
```

In `OnbetapVoice::sanitize()`, inside the `if`, after the existing FIR resets:

```cpp
            fir4LpL.reset(); fir4BpL.reset(); fir4HpL.reset();
            fir4LpR.reset(); fir4BpR.reset(); fir4HpR.reset();
```

- [ ] **Step 4: Run to verify pass**

Run: `cd ~/Dev/RobotBoy/tests && ./run.sh`
Expected: all tests pass, including the four new CHECKs.

- [ ] **Step 5: Commit**

```bash
git add src/onbetap/engine.hpp tests/onbetap/test_engine.cpp
git commit -m "Onbetap: add DecimFir9 stage-A decimator for the 4x path"
```

---

### Task 3: Wire the two-stage cascade into `processSide`

**Files:**
- Modify: `src/Onbetap.cpp:219-252` (`processSide`), `src/Onbetap.cpp:297-307` (both call sites)
- Test: `tests/onbetap/test_engine.cpp` (composed-path wiring test)

**Interfaces:**
- Consumes: `DecimFir9` and the six `fir4*` voice members from Task 2.
- Produces: `processSide(OnbetapFilter&, float&, DCBlock&, float, float, float, float, float, float, DecimFir13&, DecimFir13&, DecimFir13&, DecimFir9&, DecimFir9&, DecimFir9&)` — three `DecimFir9&` params appended.

- [ ] **Step 1: Write the failing wiring test**

Append to `tests/onbetap/test_engine.cpp` `main()` (mirrors the module's 4x branch; if this composition drifts from `Onbetap.cpp`, the test is wrong — keep them in sync):

```cpp
    // Composed 4x cascade: low-frequency sine passes at unity-ish gain
    {
        float fs = 48000.f, fsOs = fs * 4.f, tone = 100.f;
        float g = OnbetapFilter::cutoffToG(20000.f, fsOs);
        OnbetapFilter f; f.reset();
        DecimFir9 aLp, aBp, aHp; DecimFir13 bLp, bBp, bHp;
        float xPrev = 0.f, peak = 0.f;
        const float kTwoPiT = 6.28318530717959f;
        for (int n = 0; n < (int)fs; n++) {
            float x1 = std::sin(kTwoPiT * tone * n / fs);
            float lp = 0.f;
            for (int i = 1; i <= 4; i++) {
                float t = (float)i / 4.f;
                float x = xPrev + (x1 - xPrev) * t;
                auto o = f.processG(x, g, 1.02f);
                float al = aLp.push(o.lp);
                aBp.push(o.bp); aHp.push(o.hp);
                if ((i & 1) == 0) {
                    float fl = bLp.push(al);
                    if (i == 4) lp = fl;
                }
            }
            xPrev = x1;
            if (n > (int)fs / 2) peak = std::max(peak, std::fabs(lp));
        }
        // processG applies kGin (1.2, Erica input ratio) ahead of the core,
        // so "unity" through the raw filter is kGin, not 1.
        CHECK(peak > 0.85f * OnbetapFilter::kGin && peak < 1.15f * OnbetapFilter::kGin,
              "4x cascade passes 100 Hz at unity-ish gain (x kGin)");
    }
```

- [ ] **Step 2: Run to verify it fails meaningfully**

Run: `cd ~/Dev/RobotBoy/tests && ./run.sh 2>&1 | grep "4x cascade"`
Expected: PASS already (it composes Task 2's pieces directly) — that is fine; it exists to pin the composition the module code must match. If it FAILS, stop: the cascade math is wrong.

- [ ] **Step 3: Modify `processSide` in `src/Onbetap.cpp`**

Change the signature (line ~223) from:

```cpp
	float processSide(OnbetapFilter& flt, float& xPrev, DCBlock& dc, float inVolts,
	                  float g, float kEff, float driveScale, float makeup, float push,
	                  DecimFir13& firLp, DecimFir13& firBp, DecimFir13& firHp) {
```

to:

```cpp
	float processSide(OnbetapFilter& flt, float& xPrev, DCBlock& dc, float inVolts,
	                  float g, float kEff, float driveScale, float makeup, float push,
	                  DecimFir13& firLp, DecimFir13& firBp, DecimFir13& firHp,
	                  DecimFir9& fir4Lp, DecimFir9& fir4Bp, DecimFir9& fir4Hp) {
```

Replace the `if (oversample == 2) { ... } else { ... }` block with (the 2x branch and the trailing `else` are the existing code, character-identical; only the new leading 4x branch and the doc comment above the function change):

```cpp
		if (oversample == 4) {
			// 4x: two-stage decimation — fir4* (DecimFir9, 192k→96k) feeds
			// the same DecimFir13 stage the 2x path uses (96k→48k), so the
			// 4x passband matches 2x by construction. See engine.hpp for
			// the folding-band math and design provenance.
			for (int i = 1; i <= 4; i++) {
				float t = (float)i / 4.f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				float al = fir4Lp.push(o.lp);
				float ab = fir4Bp.push(o.bp);
				float ah = fir4Hp.push(o.hp);
				if ((i & 1) == 0) {                  // 96k instants: substeps 2, 4
					float fl = firLp.push(al);
					float fb = firBp.push(ab);
					float fh = firHp.push(ah);
					if (i == 4) { lp = fl; bp = fb; hp = fh; }
				}
			}
		} else if (oversample == 2) {
			// 2x: 13-tap decimation FIR (see engine.hpp DecimFir13) replaces
			// the crude 2-tap boxcar average, which under-attenuates the
			// alias band and both droops the top octave and lets content
			// above the new Nyquist fold back down (measured, Task 5).
			for (int i = 1; i <= 2; i++) {
				float t = (float)i / 2.f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				float fl = firLp.push(o.lp);
				float fb = firBp.push(o.bp);
				float fh = firHp.push(o.hp);
				if (i == 2) { lp = fl; bp = fb; hp = fh; }  // decimate: keep 1 of 2
			}
		} else {
			for (int i = 1; i <= oversample; i++) {
				float t = (float)i / oversample;
				float x = xPrev + (x1 - xPrev) * t;      // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				lp += o.lp; bp += o.bp; hp += o.hp;      // average = crude decimator
			}
			float inv = 1.f / oversample;
			lp *= inv; bp *= inv; hp *= inv;
		}
```

Also update the function's doc comment (line ~219-222): replace "the 1x path bypasses them entirely, and 4x keeps the original crude boxcar average (out of this task's scope — see worklog)" with "the 1x path bypasses them entirely; on the 4x path they serve as stage B behind the fir4* stage-A decimators."

Update both call sites in `process()`:

```cpp
			float outL = processSide(v.fL, v.xPrevL, v.dcL, inL, g, kEff, drive, makeup,
			                         push, v.firLpL, v.firBpL, v.firHpL,
			                         v.fir4LpL, v.fir4BpL, v.fir4HpL);
```

```cpp
				float outR = processSide(v.fR, v.xPrevR, v.dcR, inR,
				                         g * v.fRgRatio, kEff, drive, makeup, push,
				                         v.firLpR, v.firBpR, v.firHpR,
				                         v.fir4LpR, v.fir4BpR, v.fir4HpR);
```

- [ ] **Step 4: Build and install the VCV plugin**

Invoke the `build-robotboy-plugin` skill (VCV target). Expected: clean build.

- [ ] **Step 5: Verify the default (2x) path is bit-identical**

Re-render exactly as Task 1 Step 3 into `onbetap-4x/post_2x.wav`, then:

```bash
cmp <scratchpad>/onbetap-4x/base_2x.wav <scratchpad>/onbetap-4x/post_2x.wav && echo IDENTICAL
```
Expected: `IDENTICAL`. If not, STOP — the 2x path changed; find the accidental edit before proceeding.

For the 1x path: confirm via `git diff HEAD~1 -- src/Onbetap.cpp` that the trailing `else` branch (the generic loop 1x flows through) is character-identical — the only structural change is `if (oversample == 2)` becoming `else if (oversample == 2)`. Record this in the Task 4 worklog entry as the 1x verification.

- [ ] **Step 6: Run the full unit lane**

Run: `cd ~/Dev/RobotBoy/tests && ./run.sh`
Expected: all green.

- [ ] **Step 7: Commit**

```bash
git add src/Onbetap.cpp tests/onbetap/test_engine.cpp
git commit -m "Onbetap: two-stage FIR decimation on the 4x oversampling path"
```

---

### Task 4: Measure spur/droop, benchmark, worklog

**Files:**
- Create (scratchpad only): `onbetap-4x/measure.cpp`, `onbetap-4x/analyze.py`
- Modify: `docs/research/onbetap-worklog.md` (new dated section), scratchpad `bench_os.cpp`

**Interfaces:**
- Consumes: `DecimFir9`, `DecimFir13`, `OnbetapFilter`, `onbetap::driveGains` (signature per `tests/onbetap/test_drive_level.cpp:54`: `driveGains(drive, spanDb, headroom, tuneOnset, gritDb)` — copy the constants used there).

- [ ] **Step 1: Write the measurement harness**

`<scratchpad>/onbetap-4x/measure.cpp` — a host-free replica in the style of `tests/onbetap/test_drive_level.cpp::sideLP`, extended with a decimator-mode switch. Modes: `os2` (2x + DecimFir13, exactly as `sideLP`), `os4box` (4 substeps + boxcar average — the OLD 4x path, for a same-harness baseline), `os4cas` (the new cascade, exactly as the Task 3 branch). Renders two scenarios per mode, writing raw float32 to stdout files:
- **spur**: 5 kHz sine, amp 5 V, drive 1.0 (`onbetap::driveGains(1.f, onbetap::kDriveSpanDb, 1.f, 0.f, onbetap::kDefaultGritDb)`), res 0, LP, cutoff 20 kHz, fs 48k, 1 s warmup + 4 s capture.
- **droop**: 1 kHz and 18 kHz sines, amp 1 V, drive 0, res 0, LP, cutoff 20 kHz, 0.5 s warmup + 1 s capture each.

Compile: `g++ -std=c++20 -O2 -I ~/Dev/RobotBoy/src measure.cpp -o measure`

- [ ] **Step 2: Write the analyzer**

`<scratchpad>/onbetap-4x/analyze.py` (venv numpy): for **spur** captures, Blackman-Harris-windowed rFFT; report the worst bin in 0–24 kHz that is not within ±3 bins of a 5 kHz harmonic, in dB relative to the 5 kHz fundamental. For **droop**, RMS ratio 18 kHz vs 1 kHz capture in dB.

- [ ] **Step 3: Run and evaluate against gates**

Gates (spec success criteria 1–2):
- `os4cas` worst spur **< −35 dB** (hard gate) — expect ≤ −45 dB.
- `os4cas` droop at 18 kHz **≤ 1.76 dB** (hard gate).
- Report `os2` and `os4box` alongside for the worklog table (sanity: `os2` spur ≈ −29 dB, `os4box` spur ≈ −35 dB, matching Task 5's numbers).

If the spur gate fails: swap in the 11-tap fallback coefficients from Global Constraints (rename struct `DecimFir9`→`DecimFir11`, array size 9→11, loop bounds, impulse test), re-run Task 2/3 test steps and this measurement. If it still fails, STOP and report.

- [ ] **Step 4: Re-run the cost benchmark**

Extend the existing scratchpad `bench_os.cpp` `processSide<OS>` replica: `OS == 4` now runs the cascade (stage A pushes every substep, stage B on substeps 2 and 4), matching Task 3. Run as before (`c++ -std=c++17 -O3 -I ~/Dev/RobotBoy/src ...`). Record ns/sample for 1x/2x/4x and the 4x/2x ratio (pre-change baseline: 37/97/160 ns, ratio 1.66).

- [ ] **Step 5: Worklog entry**

Append a dated section to `docs/research/onbetap-worklog.md`: what changed (two-stage cascade, stage-A design parameters + criteria), the measurement table (spur + droop for os2/os4box/os4cas), the benchmark numbers, and a note that the upsampler remains the deferred later task.

- [ ] **Step 6: Commit**

```bash
git add docs/research/onbetap-worklog.md
git commit -m "Onbetap: worklog for 4x decimator measurements"
```
(If Step 3 forced the 11-tap fallback, include the `src/` and `tests/` changes in this commit with an amended message.)

---

### Task 5: MetaModule build + 4x parity check

**Files:**
- Create (scratchpad only): `onbetap-4x/patch_4x.yml`, MM/VCV 4x renders
- Modify: `docs/research/onbetap-worklog.md` (append parity result)

**Interfaces:**
- Consumes: Task 1's `in.wav`; the built plugin from Task 3.

- [ ] **Step 1: Build the MetaModule plugin**

Invoke the `build-robotboy-plugin` skill (MM target): `cd metamodule && cmake --fresh -B build -GNinja && cmake --build build`. Expected: `metamodule-plugins/RobotBoy.mmplugin` produced.

- [ ] **Step 2: MM headless render at 4x**

Invoke the `build-simulator` skill for the headless simulator (`~/Dev/metamodule/simulator`, preset `headless`, plugin compiled in via the `-Dext_builtin_brand_paths` cache var pointing at `~/Dev/RobotBoy/metamodule` — never edit ext-plugins.cmake). Patch yml: Onbetap, cutoff raw 9.550747, res 0.3, drive 0.2, mode 0, with `vcvModuleStates` JSON `{"oversample":4}` (the override mechanism verified in the worklog, line ~192). Render Task 1's `in.wav`, 96000 frames, to `onbetap-4x/mm_4x.wav`.

- [ ] **Step 3: VCV headless render at 4x**

Same input/params through vcv-headless (`test-vcv-module-headless` skill). Set oversample=4 via the host's module-state mechanism if it has one (check its README). If it has none: temporarily change `int oversample = 2;` to `4` in `src/Onbetap.cpp:77`, rebuild + reinstall, render `onbetap-4x/vcv_4x.wav`, then **revert the edit and rebuild + reinstall** (verify with `git diff --stat` → clean).

- [ ] **Step 4: Compare (mind the scale convention)**

vcv-headless writes sample = volts/5; the MM simulator writes 1:1 (worklog, "Comparison against vcv-headless"). Normalize before comparing:

```bash
source ~/Dev/python-scripts/.venv/bin/activate
python - <<'EOF'
import numpy as np, scipy.io.wavfile as wf
_, v = wf.read("<scratchpad>/onbetap-4x/vcv_4x.wav")
_, m = wf.read("<scratchpad>/onbetap-4x/mm_4x.wav")
n = min(len(v), len(m))
d = np.abs(v[:n].astype(np.float64) * 5.0 - m[:n].astype(np.float64))
print("max abs diff (volts):", d.max())
EOF
```
Expected: max abs diff ≤ 1e-4 V (precedent: 2.7e-6 relative at 2x; allow slack for the longer 4x pipeline). If it's large (>1e-2), suspect the scale convention or the override not taking effect before suspecting DSP divergence.

- [ ] **Step 5: Append parity result to the worklog and commit**

```bash
git add docs/research/onbetap-worklog.md
git commit -m "Onbetap: VCV/MM 4x parity verified after decimator upgrade"
```

- [ ] **Step 6: Leave both artifacts installed**

VCV plugin already installed (Task 3). Note the fresh `.mmplugin` path in the final report so the user can load it on hardware for the listening test and the real-device CPU check.
