# Onbetap Rate-Independent kCLag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the kCLag phase-lag damping correction a pure function of cutoff (evaluated at the 96 kHz calibration reference rate), so kEff — and with it top-octave damping and self-osc onset — no longer depends on the oversample setting or host rate.

**Architecture:** New host-free helper `onbetap::cutoffLagCorr(fc)` in `src/onbetap/engine.hpp`; `modulate()` folds `kCLag·cutoffLagCorr(fc)` into the slewed `kTarget`; `process()` drops the per-sample `g`-based term. Spec: `docs/superpowers/specs/2026-07-18-onbetap-kclag-rate-independent-design.md` (read its "Behavior changes" section — 2x is intentionally NOT bit-identical, only steady-state-convergent).

**Tech Stack:** C++ header-only DSP, `tests/run.sh` g++ lane, vcv-headless render checks, Task-4 scratch measurement harness (numpy analyzer), MM headless simulator parity.

## Global Constraints

- Branch: `main`, direct commits. Commit messages: one short sentence (≤15 words), **no Co-Authored-By / AI attribution**.
- Scratch files stay in the session scratchpad (`<scratchpad>/onbetap-4x/` from the decimator project is reusable); never committed.
- `kCLag` stays `0.25f` in `src/Onbetap.cpp`; the helper takes no fsOs parameter (rate independence by construction).
- Reference rate constant: `kCLagRefFsOs = 96000.f` (2x OS of 48 kHz host — where Task 5 calibrated kCLag).
- Pre-existing modified README.md stays unstaged.
- Success-criteria gates (spec): (1) helper matches old 2x/48k expression to 1e-6; (3) 2x render final-1 s max abs diff vs `base_2x.wav` ≤ 1e-4 V; (4) droop@18k os2↔os4cas gap ≤ 0.6 dB with os2 within 0.15 dB of −2.33 dB; (5) MM↔VCV 4x parity ≤ 1e-4 V.

---

### Task 1: `cutoffLagCorr` helper (TDD)

**Files:**
- Modify: `src/onbetap/engine.hpp` (in `namespace onbetap`, next to the existing constants near `kMismatchL1`)
- Test: `tests/onbetap/test_engine.cpp`

**Interfaces:**
- Produces: `onbetap::kCLagRefFsOs` (constexpr float, 96000.f) and `float onbetap::cutoffLagCorr(float fcHz)`. Task 2 consumes both names exactly.

- [ ] **Step 1: Write the failing test**

Append inside `main()` of `tests/onbetap/test_engine.cpp`, before the final `printf`:

```cpp
    // cutoffLagCorr: matches the calibrated 2x/48k per-sample expression
    // (g²/(1+g²) at fsOs = 96 kHz) and is a pure function of fc — rate
    // independence is by construction (no fsOs parameter).
    {
        const float fcs[] = {20.f, 200.f, 1000.f, 5000.f, 8000.f,
                             18000.f, 20000.f, 23500.f};
        bool match = true;
        for (float fc : fcs) {
            float g = OnbetapFilter::cutoffToG(fc, onbetap::kCLagRefFsOs);
            float old2x = g * g / (1.f + g * g);
            match = match && std::fabs(onbetap::cutoffLagCorr(fc) - old2x) < 1e-6f;
        }
        CHECK(match, "cutoffLagCorr matches calibrated 2x/48k correction");
        CHECK(onbetap::cutoffLagCorr(20000.f) > onbetap::cutoffLagCorr(200.f),
              "cutoffLagCorr grows with cutoff");
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd ~/Dev/RobotBoy/tests && ./run.sh 2>&1 | grep -B2 -A2 test_engine | head -20`
Expected: compile error, `cutoffLagCorr` is not a member of `onbetap`.

- [ ] **Step 3: Implement**

In `src/onbetap/engine.hpp`, inside `namespace onbetap`, directly after the `kMismatch*` constants:

```cpp
// Phase-lag damping-correction factor, a pure function of cutoff.
// Evaluated at kCLagRefFsOs — the oversampled rate the module's kCLag
// constant was calibrated at (Task 5: 2x OS, 48 kHz host) — so the
// correction, and with it kEff, self-osc onset, and top-octave damping,
// no longer depend on the oversample setting or the host rate. cutoffToG's
// own fc clamp (0.245·fsOs = 23.52 kHz here) saturates the correction
// above the calibrated range, reachable only at 4x.
inline constexpr float kCLagRefFsOs = 96000.f;
inline float cutoffLagCorr(float fcHz) {
    float gr = OnbetapFilter::cutoffToG(fcHz, kCLagRefFsOs);
    return gr * gr / (1.f + gr * gr);
}
```

- [ ] **Step 4: Run to verify pass**

Run: `cd ~/Dev/RobotBoy/tests && ./run.sh`
Expected: all green including the two new CHECKs.

- [ ] **Step 5: Commit**

```bash
git add src/onbetap/engine.hpp tests/onbetap/test_engine.cpp
git commit -m "Onbetap: add rate-independent cutoffLagCorr helper"
```

---

### Task 2: Rewire the module; render-convergence check

**Files:**
- Modify: `src/Onbetap.cpp` (three sites: the `kCLag` comment ~line 30, `modulate()` kTarget assignment ~line 182, `process()` kEff computation ~line 313)

**Interfaces:**
- Consumes: `onbetap::cutoffLagCorr`, `onbetap::kCLagRefFsOs` from Task 1.
- Produces: no new interfaces; `kEff` semantics change per spec.

- [ ] **Step 1: Edit `src/Onbetap.cpp`** (anchor on code text; line numbers may drift)

(a) The constant's comment:

```cpp
static constexpr float kCLag     = 0.25f;     // phase-lag: kEff -= cLag·g²/(1+g²)
```
becomes
```cpp
static constexpr float kCLag     = 0.25f;     // phase-lag: kTarget -= cLag·cutoffLagCorr(fc)
                                              // (engine.hpp; reference-rate, OS-independent)
```

(b) In `modulate()`:

```cpp
			float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f) + kOnsetTrim;
			v.kTarget = k;   // phase-lag term applied per sample from slewed g
```
becomes
```cpp
			float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f) + kOnsetTrim;
			// Phase-lag correction folded into the slewed target, evaluated
			// at the calibration reference rate (engine.hpp cutoffLagCorr) —
			// kEff no longer depends on oversample or host rate.
			v.kTarget = k - kCLag * onbetap::cutoffLagCorr(fc);
```
(`fc` is already in scope a few lines above, drift-inclusive.)

(c) In `process()`:

```cpp
			float kEff   = kBase - kCLag * g * g / (1.f + g * g);
			kEff = std::max(kEff, -0.31f);           // denominator guard floor
```
becomes
```cpp
			float kEff   = std::max(kBase, -0.31f);  // denominator guard floor
```
(`g` stays — it still feeds `processG`.)

- [ ] **Step 2: Full unit lane**

Run: `cd ~/Dev/RobotBoy/tests && ./run.sh`
Expected: all green (law-guard tests compute their own kEff independently at 2x/48k steady state; unaffected).

- [ ] **Step 3: Build and install the VCV plugin**

Invoke the `build-robotboy-plugin` skill (VCV target). Expected: clean build.

- [ ] **Step 4: Render-convergence check (spec criterion 3)**

Copy `<scratchpad>/onbetap-4x/spec_base_2x.json` with output changed to `<scratchpad>/onbetap-4x/post_kclag_2x.wav`, run `cd ~/Dev/vcv-headless && ./run.sh <that spec>`, then:

```bash
source ~/Dev/python-scripts/.venv/bin/activate
python - <<'EOF'
import numpy as np, scipy.io.wavfile as wf
_, a = wf.read("<scratchpad>/onbetap-4x/base_2x.wav")
_, b = wf.read("<scratchpad>/onbetap-4x/post_kclag_2x.wav")
n = min(len(a), len(b)); a, b = a[:n].astype(np.float64), b[:n].astype(np.float64)
d = np.abs(a - b) * 5.0  # sample = volts/5 → express in volts
print("full-render max abs diff (V):", d.max())
print("final-1s   max abs diff (V):", d[-48000:].max())
EOF
```
Expected: final-1 s ≤ 1e-4 V (gate). Full-render diff may be larger (startup smoother transient — record the number, don't gate it). If final-1 s exceeds the gate, STOP: steady-state 2x voicing was not preserved; re-check the folded expression against the helper before touching constants.

- [ ] **Step 5: Commit**

```bash
git add src/Onbetap.cpp
git commit -m "Onbetap: fold rate-independent kCLag correction into kTarget"
```

---

### Task 3: Measurements, MM parity, worklog

**Files:**
- Modify (scratch): `<scratchpad>/onbetap-4x/measure.cpp` (kEff formula update)
- Modify: `docs/research/onbetap-worklog.md` (new dated section)

**Interfaces:**
- Consumes: Task-4-of-decimator-project harness (`measure.cpp` + `analyze.py`, scratchpad `onbetap-4x/`), the parity method + patch yml from that project's Task 5 (worklog section "VCV↔MM parity at 4x" documents it; the MM simulator build should still be configured from that run).

- [ ] **Step 1: Update the harness's kEff to the new module formula**

In `<scratchpad>/onbetap-4x/measure.cpp`, replace its per-mode kEff computation (old: `kEff = max(k − 0.25·g²/(1+g²), −0.31)` with each mode's own fsOs-derived g) with the folded form used by the module now:

```cpp
    float corr = onbetap::cutoffLagCorr(cutoff);          // engine.hpp helper
    float kEff = std::max(k - 0.25f * corr, -0.31f);       // same for every OS mode
```
(The harness includes `onbetap/engine.hpp` already. `0.25f` mirrors the module's `kCLag`.)

- [ ] **Step 2: Re-measure droop (spec criterion 4)**

Recompile and rerun the harness droop scenario (1 kHz + 18 kHz, drive 0, res 0, LP cutoff 20 kHz) for `os2` and `os4cas`; run `analyze.py`.
Gates: os2 within 0.15 dB of −2.33 dB; |os2 − os4cas| ≤ 0.6 dB. Also record os4box for the table. If a gate fails, STOP and report BLOCKED with numbers.

- [ ] **Step 3: MM parity re-run (spec criterion 5)**

Rebuild the .mmplugin (`build-robotboy-plugin` skill, MM target), re-run the MM headless simulator 4x render and the vcv-headless 4x render exactly as the previous parity check (same patch yml / spec JSONs in the scratchpad, `{"oversample":4}` overrides on both sides), compare with the 5x normalization snippet. Gate: max abs diff ≤ 1e-4 V.

- [ ] **Step 4: Worklog entry**

Append `## 2026-07-18 — kCLag made rate-independent` to `docs/research/onbetap-worklog.md`: the problem (kEff 0.972 vs 1.039 at fc 20 kHz between 2x/4x), the fix (fold reference-rate correction into kTarget), the behavior changes from the spec (4x/1x now match 2x; 2x steady-state preserved, transient smoothing path changed; host-rate independence), the droop table before/after (before: os2 −2.33 / os4cas −3.75), the render-convergence number, the parity number, and a pointer to the spec.

- [ ] **Step 5: Commit**

```bash
git add docs/research/onbetap-worklog.md
git commit -m "Onbetap: worklog for rate-independent kCLag"
```
