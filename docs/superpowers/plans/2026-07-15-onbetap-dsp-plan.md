# Onbetap DSP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the Polivoks-emulation filter engine behind the existing Onbetap scaffold, per `docs/superpowers/specs/2026-07-15-onbetap-dsp-spec.md`.

**Architecture:** Header-only nonlinear TPT two-integrator core (`src/onbetap/OnbetapFilter.hpp`) modeled on the Polivoks circuit (tanh-window integrators = slew limiting, asymmetric rail clamps on states, damping-removal resonance), driven by an MF-20-style module wrapper (modulate() at 2.5 ms, g-domain smoothing, poly, true stereo, 2× oversampling). Character menu (Tamed/Vintage) plus new Resonance-limiting / Oversampling / Tuning menu items.

**Tech Stack:** VCV Rack SDK (plugin builds via `vcv/`), plain g++ test lane (`tests/run.sh`), MetaModule SDK build (`metamodule/`), vcv-headless + MM headless simulator for end-to-end audio.

## Global Constraints

- Work ONLY in `/Users/gabrielroth/Dev/RobotBoy/.worktrees/worktree-polivoks`. Never touch the main checkout or other worktrees. (Exception: the MetaModule simulator build environment `~/Dev/metamodule` is used read-only/configure-only per its own CLAUDE.md rules; never leave tracked changes there.)
- Do NOT change the panel: `res/Onbetap.svg`, widget positions in `src/Onbetap.cpp` (`mm2px(Vec(...))` calls), or `panel-specs/onbetap.yaml`.
- No Rack headers in `src/onbetap/*.hpp` (host-free, testable with plain g++, like `src/mf20/MF20Filter.hpp`).
- No per-sample transcendentals in the audio path (`tan`/`exp2`/`pow`/`sqrt` only at modulate rate, 2.5 ms). The rational `tanhish` is allowed per-sample.
- Deterministic DSP: no `rand()`/`std::random`; fixed constants or fixed-seed xorshift only. Alternating ±1e-9 dither (MF-20 pattern) for denormals/self-osc seed.
- Sample-rate independent: all coefficients derived from the host rate.
- Commit after each task; messages short (≤15 words), no AI attribution, no Co-Authored-By.
- Escape literal tildes as `\~` in Markdown docs.

**Key spec numbers (used throughout):** window W = 1 (≈2.4 V physical); rails +4.4/−4.1; soft knee 3.4→4.0; negative window ratio 0.93; Gin = 1.2; k(res) = −0.06 + 1.08·(1−res)^2.3; phase-lag cLag = 0.25 via g²/(1+g²); cutoff clamp [0.5 Hz, 0.49·Nyquist_os]; drive −12..+24 dB; OS default 2×.

---

### Task 1: OnbetapFilter.hpp — nonlinear core

One task (not split linear/nonlinear): the solve is gain-linearized, so the linear path is just the nonlinear code with unity secant gains — splitting would mean writing the solve twice.

**Files:**
- Create: `src/onbetap/OnbetapFilter.hpp`
- Create: `tests/onbetap/test_onbetap.cpp`

**Interfaces (later tasks rely on exactly these):**
```cpp
class OnbetapFilter {
public:
  enum class Limit { Hard, Soft };
  struct Out { float lp, bp, hp; };
  static float cutoffToG(float fcHz, float fsOs);   // clamps fc to [0.5, 0.49·fsOs/2]
  static float tanhish(float x);                    // rational tanh, hard-limited ±1
  static float sat(float v);                        // asymmetric window saturator
  static float satGain(float v);                    // secant gain sat(v)/v, →1 at 0
  void  setLimit(Limit m);
  void  setMismatch(float m1, float m2);            // per-stage g scale = 1+m
  void  setOffset(float off);                       // DC injected at the node
  Out   processG(float in, float g, float kEff);    // in: normalized core units
  void  reset();
  bool  stateFinite() const;
};
```

- [ ] **Step 1: Write the failing test file**

Create `tests/onbetap/test_onbetap.cpp`. Plain-assert style matching `tests/mf20/test_mf20.cpp` (PASS/FAIL counters, exit non-zero on failure). Complete content:

```cpp
// tests/onbetap/test_onbetap.cpp — OnbetapFilter core tests (host-free)
#include "onbetap/OnbetapFilter.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

static constexpr float kFs = 96000.f;  // core runs at the oversampled rate
static constexpr float kPi = 3.14159265358979f;

// Run a sine of amplitude a at freq f through a fresh filter; return output
// RMS of the chosen tap over the last half of n samples (steady state).
enum Tap { LP, BP, HP };
struct SineResult { float rms, peak; };
static SineResult runSine(float f, float a, float fc, float k, Tap tap,
                          OnbetapFilter::Limit lim = OnbetapFilter::Limit::Hard,
                          int n = 48000) {
    OnbetapFilter flt;
    flt.setLimit(lim);
    float g = OnbetapFilter::cutoffToG(fc, kFs);
    double sumSq = 0; float peak = 0; int count = 0;
    for (int i = 0; i < n; i++) {
        float x = a * std::sin(2 * kPi * f * i / kFs);
        auto o = flt.processG(x, g, k);
        float y = (tap == LP) ? o.lp : (tap == BP) ? o.bp : o.hp;
        if (i >= n / 2) { sumSq += (double)y * y; peak = std::max(peak, std::fabs(y)); count++; }
    }
    return { (float)std::sqrt(sumSq / count), peak };
}

// Self-oscillation run: no input except a tiny alternating seed; returns RMS,
// peak and zero-crossing frequency estimate over the last half.
struct OscResult { float rms, peak, freq; };
static OscResult runOsc(float fc, float k, OnbetapFilter::Limit lim, int n = 96000) {
    OnbetapFilter flt;
    flt.setLimit(lim);
    float g = OnbetapFilter::cutoffToG(fc, kFs);
    double sumSq = 0; float peak = 0; int count = 0, crossings = 0;
    float prev = 0, dither = 1e-9f;
    for (int i = 0; i < n; i++) {
        auto o = flt.processG(dither, g, k);
        dither = -dither;
        if (i >= n / 2) {
            sumSq += (double)o.bp * o.bp; peak = std::max(peak, std::fabs(o.bp));
            if (prev <= 0.f && o.bp > 0.f) crossings++;
            prev = o.bp; count++;
        }
    }
    return { (float)std::sqrt(sumSq / count), peak, crossings * kFs / (float)count };
}

int main() {
    // ---- linear regime (tiny signals: secant gains ~1) ----
    // LP passband gain ≈ Gin = 1.2 (input resistor ratio 47k/39k)
    {
        auto r = runSine(100.f, 0.01f, 2000.f, 1.0f, LP);
        CHECK(std::fabs(r.rms / (0.01f / std::sqrt(2.f)) - 1.2f) < 0.06f,
              "LP passband gain ~1.2 at Q=1");
    }
    // LP slope: 12 dB/oct well above cutoff -> one octave = x4 amplitude
    {
        auto a1 = runSine(4000.f, 0.01f, 500.f, 1.0f, LP);
        auto a2 = runSine(8000.f, 0.01f, 500.f, 1.0f, LP);
        float ratio = a1.rms / a2.rms;
        CHECK(ratio > 3.3f && ratio < 4.8f, "LP slope ~12 dB/oct");
    }
    // BP skirts: 6 dB/oct -> one octave = x2
    {
        auto a1 = runSine(4000.f, 0.01f, 500.f, 1.0f, BP);
        auto a2 = runSine(8000.f, 0.01f, 500.f, 1.0f, BP);
        float ratio = a1.rms / a2.rms;
        CHECK(ratio > 1.7f && ratio < 2.4f, "BP skirt ~6 dB/oct");
    }
    // HP: passes highs, kills lows (12 dB/oct below cutoff)
    {
        auto hi = runSine(8000.f, 0.01f, 500.f, 1.0f, HP);
        auto lo1 = runSine(100.f, 0.01f, 2000.f, 1.0f, HP);
        auto lo2 = runSine(50.f,  0.01f, 2000.f, 1.0f, HP);
        CHECK(hi.rms > 0.008f, "HP passes highs");
        float ratio = lo1.rms / lo2.rms;
        CHECK(ratio > 3.3f && ratio < 4.8f, "HP slope ~12 dB/oct");
    }
    // Resonance peak at fc grows as k shrinks (Q = 1/k)
    {
        auto q1 = runSine(1000.f, 0.005f, 1000.f, 1.0f, BP);
        auto q4 = runSine(1000.f, 0.005f, 1000.f, 0.25f, BP);
        CHECK(q4.rms / q1.rms > 3.f, "BP peak scales with 1/k");
    }
    // ---- self-oscillation ----
    {
        auto osc = runOsc(1000.f, -0.06f, OnbetapFilter::Limit::Hard);
        CHECK(osc.rms > 0.5f, "hard self-osc sustains");
        CHECK(osc.peak < 4.6f, "hard self-osc bounded by rails");
        CHECK(osc.freq > 700.f && osc.freq < 1400.f, "hard self-osc near fc");
        auto soft = runOsc(1000.f, -0.06f, OnbetapFilter::Limit::Soft);
        CHECK(soft.rms > 0.3f, "soft self-osc sustains");
        CHECK(soft.peak < 4.2f, "soft self-osc bounded below hard rails");
        // hard limiting -> squarer wave -> lower crest factor than soft
        float crestH = osc.peak / osc.rms, crestS = soft.peak / soft.rms;
        printf("info: crest hard=%.3f soft=%.3f freqH=%.0f freqS=%.0f\n",
               crestH, crestS, osc.freq, soft.freq);
        CHECK(crestH < crestS + 0.15f, "hard crest <= soft crest (square-ish)");
    }
    // no oscillation at k = 1 (Q = 1)
    {
        auto osc = runOsc(1000.f, 1.0f, OnbetapFilter::Limit::Hard);
        CHECK(osc.rms < 1e-3f, "no self-osc at Q=1");
    }
    // ---- drive suppresses resonance (the signature interaction) ----
    {
        float k = 0.15f;  // high resonance, below self-osc
        auto quiet = runSine(1000.f, 0.02f, 1000.f, k, BP);
        auto loud  = runSine(1000.f, 2.0f,  1000.f, k, BP);
        float gQuiet = quiet.rms / 0.02f, gLoud = loud.rms / 2.0f;
        CHECK(gLoud < 0.5f * gQuiet, "drive suppresses resonance gain");
    }
    // ---- mismatch and offset hooks ----
    {
        OnbetapFilter flt;
        flt.setMismatch(0.06f, -0.05f);
        float g = OnbetapFilter::cutoffToG(1000.f, kFs);
        for (int i = 0; i < 1000; i++) flt.processG(0.01f, g, 0.5f);
        CHECK(flt.stateFinite(), "mismatch stays finite");
        OnbetapFilter f2;
        f2.setOffset(0.05f);
        float dc = 0;
        for (int i = 0; i < 48000; i++) dc = f2.processG(0.f, g, 1.0f).lp;
        CHECK(std::fabs(dc) > 0.005f && std::fabs(dc) < 0.5f, "offset shifts LP DC");
    }
    // ---- stability torture: max res, huge input, cutoff sweep ----
    {
        OnbetapFilter flt;
        bool finite = true;
        float dither = 1e-9f;
        for (int i = 0; i < 96000; i++) {
            float fc = 20.f * std::exp2(10.f * (0.5f + 0.5f * std::sin(2 * kPi * 3.f * i / kFs)));
            float g = OnbetapFilter::cutoffToG(fc, kFs);
            float x = 20.f * std::sin(2 * kPi * 55.f * i / kFs) + dither;
            dither = -dither;
            auto o = flt.processG(x, g, -0.31f);   // worst-case kEff floor
            finite = finite && std::isfinite(o.lp) && std::isfinite(o.bp) && std::isfinite(o.hp);
        }
        CHECK(finite && flt.stateFinite(), "torture sweep stays finite");
    }
    // ---- determinism: two identical runs bit-match ----
    {
        auto a = runOsc(2000.f, -0.02f, OnbetapFilter::Limit::Hard, 24000);
        auto b = runOsc(2000.f, -0.02f, OnbetapFilter::Limit::Hard, 24000);
        CHECK(a.rms == b.rms && a.peak == b.peak, "deterministic");
    }
    // ---- reset / sanitize ----
    {
        OnbetapFilter flt;
        float g = OnbetapFilter::cutoffToG(1000.f, kFs);
        for (int i = 0; i < 100; i++) flt.processG(1.f, g, 0.5f);
        flt.reset();
        auto o = flt.processG(0.f, g, 1.0f);
        CHECK(o.lp == 0.f && o.bp == 0.f, "reset clears state");
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
```

- [ ] **Step 2: Run it to verify it fails to compile**

Run: `cd tests && g++ -std=c++17 -I../src -o /tmp/test_onbetap onbetap/test_onbetap.cpp && /tmp/test_onbetap`
Expected: compile error, `onbetap/OnbetapFilter.hpp: No such file or directory`.

- [ ] **Step 3: Implement the core**

Create `src/onbetap/OnbetapFilter.hpp` with exactly this content (header comment included — it documents the model derivation the way `MF20Filter.hpp` does):

```cpp
#pragma once
/**
 * OnbetapFilter.hpp — Polivoks VCF core (Onbetap)
 *
 * Model (docs/superpowers/specs/2026-07-15-onbetap-dsp-spec.md,
 * docs/research/polivoks-*.md):
 *
 * The Polivoks filter core is two К140УД12 programmable op-amps run open-loop
 * as current-controlled integrators (internal 30 pF comp caps; GBW ∝ Iset),
 * closed into a two-integrator state-variable loop by six resistors:
 *
 *   node = Gin·x + y₂ + k·y₁        current sum into the 1 kΩ node
 *   ẏ₁  = −ωc · N(node)            stage 1, inverting
 *   ẏ₂  = +ωc · N(y₁)              stage 2, non-inverting
 *
 * Everything is referred to stage-output scale and normalised so the diff-pair
 * linear window (±2·V_T at the pins × the 47k:1k divider ≈ ±2.4 V at outputs)
 * is ±1. N is the diff-pair saturator: slew limiting IS its rate ceiling
 * (SR = ωc·window), so one tanh-shaped N reproduces both regimes. The negative
 * window is 7% smaller (class-B output asymmetry → even harmonics).
 *
 * Linearised: s² + k·ωc·s + ωc², i.e. Q = 1/k. Resonance on the hardware
 * REMOVES damping (BP fed back through the pot): k spans ≈1.02 (pot at
 * minimum — the filter is never flatter than Q≈1) down past 0 (self-osc).
 *
 * States are clamped at the output-swing rails (Wasp DAFx-2022 result: clamp
 * the STATES, not the output, or high-resonance behavior is wrong):
 *   Hard (factory/Erica): asymmetric hard clamp at +4.4/−4.1 → square-ish
 *     rail-to-rail self-osc ("suddenly harsh").
 *   Soft (diode-clamp mod, Elta "soft"): saturating knee 3.4→4.0 → sine-ish
 *     self-osc, slightly higher pitch.
 *
 * Discretization: TPT trapezoidal integrators; the two embedded saturators are
 * handled by secant-gain linearisation (N(v) ≈ n·v, n = N(v*)/v*), two fixed
 * passes (predict from states, refine at solved mids). With per-stage gains
 * g₁ = g·(1+m₁), g₂ = g·(1+m₂) (integrator mismatch, vintage mode):
 *
 *   y₁ = (s₁ − g₁n₁(Gin·x + off + s₂)) / (1 + g₁n₁(k + g₂n₂))
 *   y₂ = s₂ + g₂n₂·y₁
 *   HP = −N(node) (the saturated node signal = ẏ₁/ωc — physical, gritty)
 *   state update s = 2·mid − s, then rail clamp.
 *
 * Denominator guard: n ∈ (0,1], k ≥ −0.31 (module clamps) keeps D > 0 for all
 * g ≤ tan(0.49π)·…; a 0.05 floor is cheap insurance regardless.
 *
 * off is a DC current injected at the node (per-unit offsets, ×48 noise gain;
 * vintage mode scales it with cutoff → sweep thump).
 *
 * Signal convention: normalised core units, window = ±1 ≈ 2.4 V physical.
 * The module wrapper owns volts↔core scaling, drive, oversampling, taps.
 */

#include <cmath>
#include <algorithm>

class OnbetapFilter {
public:
    enum class Limit { Hard, Soft };
    struct Out { float lp, bp, hp; };

    static constexpr float kRailPos  = 4.4f;   // +10.5 V / 2.4 V window
    static constexpr float kRailNeg  = 4.1f;   // asymmetric per-unit clip
    static constexpr float kSoftKnee = 3.4f;   // soft-limit knee start
    static constexpr float kSoftMax  = 4.0f;   // soft-limit asymptote
    static constexpr float kAsymNeg  = 0.93f;  // negative window ratio
    static constexpr float kGin      = 1.2f;   // Erica 47k/39k input ratio

    static float cutoffToG(float fcHz, float fsOs) {
        float fc = std::clamp(fcHz, 0.5f, fsOs * 0.245f);
        return std::tan(kPi * fc / fsOs);
    }

    // Rational tanh approximation, exact ±1 at |x| = 3, hard-limited beyond.
    static float tanhish(float x) {
        x = std::clamp(x, -3.f, 3.f);
        float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    // Diff-pair saturator, asymmetric window (+1 / −kAsymNeg).
    static float sat(float v) {
        return (v >= 0.f) ? tanhish(v) : kAsymNeg * tanhish(v / kAsymNeg);
    }

    // Secant gain sat(v)/v — the linearised per-sample gain. →1 as v→0.
    static float satGain(float v) {
        float a = std::fabs(v);
        if (a < 1e-4f) return 1.f;
        return sat(v) / v;
    }

    void setLimit(Limit m) { limit = m; }
    void setMismatch(float m1, float m2) { gs1 = 1.f + m1; gs2 = 1.f + m2; }
    void setOffset(float o) { off = o; }
    void reset() { s1 = s2 = 0.f; }
    bool stateFinite() const { return std::isfinite(s1) && std::isfinite(s2); }

    Out processG(float in, float g, float kEff) {
        float g1 = g * gs1, g2 = g * gs2;
        float xin = kGin * in + off;

        // Pass 1: secant gains predicted from current states.
        float n1 = satGain(xin + s2 + kEff * s1);
        float n2 = satGain(s1);
        float y1 = solveY1(xin, g1, g2, kEff, n1, n2);
        float y2 = s2 + g2 * n2 * y1;
        // Pass 2: refine at the solved mids.
        n1 = satGain(xin + y2 + kEff * y1);
        n2 = satGain(y1);
        y1 = solveY1(xin, g1, g2, kEff, n1, n2);
        y2 = s2 + g2 * n2 * y1;

        float hp = -sat(xin + y2 + kEff * y1);

        s1 = 2.f * y1 - s1;
        s2 = 2.f * y2 - s2;
        clampStates();

        return { y2, y1, hp };
    }

private:
    static constexpr float kPi = 3.14159265358979f;

    float s1 = 0.f, s2 = 0.f;      // BP, LP states
    float gs1 = 1.f, gs2 = 1.f;    // per-stage g scale (mismatch)
    float off = 0.f;               // node DC offset
    Limit limit = Limit::Hard;

    float solveY1(float xin, float g1, float g2, float kEff,
                  float n1, float n2) const {
        float D = 1.f + g1 * n1 * (kEff + g2 * n2);
        D = std::max(D, 0.05f);
        return (s1 - g1 * n1 * (xin + s2)) / D;
    }

    static float softLimitOne(float v) {
        float av = std::fabs(v);
        if (av <= kSoftKnee) return v;
        float t = (av - kSoftKnee) / (kSoftMax - kSoftKnee);
        float lim = kSoftKnee + (kSoftMax - kSoftKnee) * t / (1.f + t);
        return v > 0.f ? lim : -lim;
    }

    void clampStates() {
        if (limit == Limit::Hard) {
            s1 = std::clamp(s1, -kRailNeg, kRailPos);
            s2 = std::clamp(s2, -kRailNeg, kRailPos);
        } else {
            s1 = softLimitOne(s1);
            s2 = softLimitOne(s2);
        }
    }
};
```

- [ ] **Step 4: Run the tests until green**

Run: `cd tests && g++ -std=c++17 -I../src -o /tmp/test_onbetap onbetap/test_onbetap.cpp && /tmp/test_onbetap`
Expected: all CHECKs PASS, exit 0. If a threshold narrowly fails (these are
measured behaviors, not exact math), first verify the behavior is qualitatively
right (print the measured values), then widen the test bound only if the
behavior is correct — never "fix" by weakening a behavior that's actually
missing. The self-osc frequency and crest bounds are the likeliest to need a
nudge; the slope, suppression, stability and determinism tests must not change.

- [ ] **Step 5: Run the whole suite via the lane and commit**

Run: `cd tests && ./run.sh` — expected: existing tests still green plus the new
binary (run.sh auto-discovers `tests/onbetap/test_*.cpp`). Then:

```bash
git add src/onbetap/OnbetapFilter.hpp tests/onbetap/test_onbetap.cpp
git commit -m "Onbetap: add nonlinear Polivoks filter core with tests"
```

---

### Task 2: engine.hpp — voice pool, smoothing, character state

**Files:**
- Create: `src/onbetap/engine.hpp`
- Create: `tests/onbetap/test_engine.cpp`

**Interfaces:**
- Consumes: `OnbetapFilter` (Task 1), `OnePoleSmoother`/`smootherAlpha` from `src/mf20/dsp_utils.hpp` (existing; include as `#include "../mf20/dsp_utils.hpp"`).
- Produces (used verbatim by Task 3):

```cpp
struct OnbetapVoice {
  OnbetapFilter fL, fR;
  OnePoleSmoother gSlew, kSlew, driveSlew, makeupSlew;  // per-sample slews
  float gTarget, kTarget, driveTarget, makeupTarget;
  float xPrevL, xPrevR;              // oversampling input history
  void setAlpha(float a);
  void reset();
  void sanitize();                   // NaN recovery, filters only
};
struct OnbetapPool {
  OnbetapVoice voices[16];
  int activeVoices = 1;
  void setVoices(int n);             // entering voices reset() first
  void resetAll();
};
// Deterministic drift/mismatch constants (module-level, per side):
namespace onbetap {
  constexpr float kMismatchL1 = 0.06f,  kMismatchL2 = -0.045f;
  constexpr float kMismatchR1 = -0.05f, kMismatchR2 = 0.055f;
  // OU drift, updated at modulate rate: returns log2-octave offset.
  struct DriftWalker {
    uint32_t rng; float value = 0.f;
    explicit DriftWalker(uint32_t seed) : rng(seed) {}
    float step(float dtSec, float depthOct);   // deterministic xorshift OU
    void reset() { value = 0.f; }
  };
}
```

- [ ] **Step 1: Write the failing test**

Create `tests/onbetap/test_engine.cpp`:

```cpp
// tests/onbetap/test_engine.cpp — OnbetapVoice/Pool + DriftWalker
#include "onbetap/engine.hpp"
#include <cmath>
#include <cstdio>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

int main() {
    // Pool: activating voices resets them
    {
        OnbetapPool pool;
        float g = OnbetapFilter::cutoffToG(1000.f, 96000.f);
        for (int i = 0; i < 100; i++) pool.voices[3].fL.processG(1.f, g, 0.2f);
        pool.setVoices(2);
        pool.setVoices(8);   // voice 3 re-enters: must be clean
        auto o = pool.voices[3].fL.processG(0.f, g, 1.0f);
        CHECK(o.lp == 0.f && o.bp == 0.f, "re-entering voice starts clean");
        CHECK(pool.activeVoices == 8, "activeVoices tracks setVoices");
    }
    // sanitize clears NaN states but leaves smoother values alone
    {
        OnbetapVoice v;
        float g = OnbetapFilter::cutoffToG(1000.f, 96000.f);
        v.fL.processG(std::nanf(""), g, 0.5f);
        CHECK(!v.fL.stateFinite(), "NaN poisons state");
        v.gSlew.reset(0.123f);
        v.sanitize();
        CHECK(v.fL.stateFinite(), "sanitize restores filters");
        CHECK(v.gSlew.value == 0.123f, "sanitize leaves smoothers");
    }
    // DriftWalker: deterministic, zero-mean-ish, bounded by depth
    {
        onbetap::DriftWalker a(0x1234), b(0x1234), c(0x5678);
        float maxAbs = 0; bool same = true, differs = false;
        for (int i = 0; i < 100000; i++) {           // ~4 min of 2.5 ms steps
            float va = a.step(0.0025f, 0.3f);
            float vb = b.step(0.0025f, 0.3f);
            float vc = c.step(0.0025f, 0.3f);
            same = same && (va == vb);
            differs = differs || (va != vc);
            maxAbs = std::max(maxAbs, std::fabs(va));
        }
        CHECK(same, "same seed -> identical walk");
        CHECK(differs, "different seed -> different walk");
        CHECK(maxAbs > 0.02f && maxAbs < 0.9f, "drift wanders but stays bounded");
        CHECK(std::isfinite(a.value), "drift finite");
    }
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cd tests && g++ -std=c++17 -I../src -o /tmp/test_engine onbetap/test_engine.cpp && /tmp/test_engine`
Expected: compile error, `onbetap/engine.hpp` not found.

- [ ] **Step 3: Implement engine.hpp**

Create `src/onbetap/engine.hpp`:

```cpp
#pragma once
/**
 * engine.hpp — per-voice DSP state for Onbetap polyphony (MF-20 pattern).
 *
 * OnbetapVoice: L+R filter cores plus per-sample smoothers. Cutoff smoothing
 * happens in the g (prewarp-gain) domain: modulate() computes tan/exp2/pow at
 * ~2.5 ms intervals; the audio path only slews. xPrevL/R hold the previous
 * host-rate input sample for oversampling interpolation.
 *
 * DriftWalker: deterministic OU (Ornstein–Uhlenbeck) random walk for the
 * Vintage cutoff drift, xorshift32-seeded — bit-reproducible across runs and
 * across the VCV/MetaModule builds (headless comparison relies on this).
 */

#include "OnbetapFilter.hpp"
#include "../mf20/dsp_utils.hpp"
#include <algorithm>
#include <cstdint>

struct OnbetapVoice {
    // Defaults ≈ 750 Hz at 96 kHz (2× OS of 48 kHz); corrected by the first
    // modulate() within 2.5 ms.
    static constexpr float kDefaultG = 0.0245f;

    OnbetapFilter fL, fR;
    OnePoleSmoother gSlew      { kDefaultG };
    OnePoleSmoother kSlew      { 1.02f };
    OnePoleSmoother driveSlew  { 0.25f };
    OnePoleSmoother makeupSlew { 1.f };
    float gTarget = kDefaultG, kTarget = 1.02f;
    float driveTarget = 0.25f, makeupTarget = 1.f;
    float xPrevL = 0.f, xPrevR = 0.f;

    void setAlpha(float a) {
        gSlew.setAlpha(a); kSlew.setAlpha(a);
        driveSlew.setAlpha(a); makeupSlew.setAlpha(a);
    }
    void reset() {
        fL.reset(); fR.reset();
        gSlew.reset(kDefaultG); kSlew.reset(1.02f);
        driveSlew.reset(0.25f); makeupSlew.reset(1.f);
        gTarget = kDefaultG; kTarget = 1.02f;
        driveTarget = 0.25f; makeupTarget = 1.f;
        xPrevL = xPrevR = 0.f;
    }
    // NaN recovery per modulate block: filters only (smoother inputs are
    // clamped params, always finite) — avoids a parameter sweep on recovery.
    void sanitize() {
        if (!fL.stateFinite() || !fR.stateFinite()) { fL.reset(); fR.reset(); }
    }
};

struct OnbetapPool {
    OnbetapVoice voices[16];
    int activeVoices = 1;

    void setVoices(int n) {
        n = std::clamp(n, 1, 16);
        for (int i = activeVoices; i < n; i++) voices[i].reset();
        activeVoices = n;
    }
    void resetAll() { for (auto& v : voices) v.reset(); }
};

namespace onbetap {

// Fixed per-side integrator mismatch (Vintage). Deterministic by design.
constexpr float kMismatchL1 = 0.06f,  kMismatchL2 = -0.045f;
constexpr float kMismatchR1 = -0.05f, kMismatchR2 = 0.055f;

// Deterministic OU random walk. step() advances one modulate block and
// returns the current value in octaves (log2 cutoff offset). depthOct sets
// the stationary standard deviation; tau fixes the wander timescale.
struct DriftWalker {
    uint32_t rng;
    float value = 0.f;
    explicit DriftWalker(uint32_t seed) : rng(seed ? seed : 1u) {}

    float step(float dtSec, float depthOct) {
        // xorshift32 → uniform in [-1, 1]
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float u = (int32_t)rng * (1.f / 2147483648.f);
        constexpr float tau = 45.f;              // seconds
        float a = dtSec / tau;
        // OU: sigma chosen so stationary std ≈ depthOct
        float sigma = depthOct * std::sqrt(2.f * a);
        value += a * (0.f - value) + sigma * u;
        value = std::clamp(value, -3.f * depthOct, 3.f * depthOct);
        return value;
    }
    void reset() { value = 0.f; }
};

} // namespace onbetap
```

- [ ] **Step 4: Run both test binaries, then the lane**

Run: `cd tests && g++ -std=c++17 -I../src -o /tmp/test_engine onbetap/test_engine.cpp && /tmp/test_engine && ./run.sh`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add src/onbetap/engine.hpp tests/onbetap/test_engine.cpp
git commit -m "Onbetap: add voice pool and deterministic drift walker"
```

---

### Task 3: Module wrapper — process(), modulate(), oversampling, taps

**Files:**
- Modify: `src/Onbetap.cpp` (replace the passthrough `process()` and the module body; keep every `addParam`/`addInput`/`addOutput` position line and the enums exactly as they are)

**Interfaces:**
- Consumes: `OnbetapFilter`, `OnbetapVoice`, `OnbetapPool`, `onbetap::DriftWalker`, `onbetap::kMismatch*` (Tasks 1–2), `OnePoleSmoother`/`smootherAlpha`/`resTaper`-style helpers from `src/mf20/dsp_utils.hpp`.
- Produces: module fields used by Task 4's menu: `limitMode` (`OnbetapFilter::Limit`), `oversample` (int 1/2/4), tuning floats `tuneDriveDb`, `tuneHeadroom`, `tuneOnset`, `tuneOutDb` (all defined below).

**Design constants (initial values; Task 5 calibrates):**

```cpp
// volts → core units: 1 / 2.4 V window, times base trim
static constexpr float kVoltsToCore = 1.f / 2.4f;
static constexpr float kBaseTrim    = 0.4f;   // drive=0 → mild warmth at ±5 V
// drive knob [0,1] → gain exp2(lerp(-2, +4, d)) = 0.25×…16× (−12…+24 dB)
// makeup = 1/sqrt(driveGain/0.25): unity at drive=0, −18 dB at max
// output: volts = core × kOutScale × makeup, then VCA sat 9·tanhish(v/9)
static constexpr float kOutScale = 10.5f;     // calibrated in Task 5
static constexpr float kCLag     = 0.25f;     // phase-lag: kEff -= cLag·g²/(1+g²)
static constexpr float kVintageDriftOct = 0.18f;  // OU stationary std
static constexpr float kVintageOffset   = 0.03f;  // node offset at 750 Hz, scales with log2 fc
```

- [ ] **Step 1: Replace the module body**

Keep: includes, `struct Onbetap : Module` name, all three enums, `configParam`/`configInput`/`configOutput`/`configBypass` lines, `vintageDrift` field, and the entire `OnbetapWidget` position block. Replace `process()`, add fields and methods. The complete new module body (widget unchanged except Task 4's menu):

```cpp
#include "plugin.hpp"
#include "onbetap/OnbetapFilter.hpp"
#include "onbetap/engine.hpp"
#include "mf20/dsp_utils.hpp"

// (module doc comment: update to describe the implemented engine, citing the
// spec doc; drop the "SHELL ONLY" paragraph.)

struct Onbetap : Module {
    // ... enums and ctor configParam/configInput/configOutput exactly as now ...

    OnbetapPool pool;
    onbetap::DriftWalker driftL { 0x0B617A01u };
    onbetap::DriftWalker driftR { 0x0B617A02u };

    bool vintageDrift = false;                      // existing Character toggle
    OnbetapFilter::Limit limitMode = OnbetapFilter::Limit::Hard;
    int oversample = 2;                             // 1 / 2 / 4
    // Tuning menu (Task 4): defaults = spec values
    float tuneDriveDb = 36.f;    // drive span in dB (−12 → +24)
    float tuneHeadroom = 1.f;    // scales kBaseTrim
    float tuneOnset = 0.f;       // added to k before phase-lag term (±0.1)
    float tuneOutDb = 0.f;       // output trim ±12 dB

    int modulationSteps = 100, steps = 100;
    float sampleRate = 44100.f;
    float dither = 1e-9f;

    // Mode crossfade (5 ms): current/target mode + ramp position
    int modeCurrent = 0, modeTarget = 0;
    float modeXf = 1.f, modeXfStep = 1.f;

    // constructor: as now, plus
    //   onSampleRateChange is triggered by Rack after construction; still call
    //   configureRates(44100.f) here for host-free safety.

    void configureRates(float fs) {
        sampleRate = fs;
        float alpha = smootherAlpha(fs, 0.005f);
        for (auto& v : pool.voices) v.setAlpha(alpha);
        modulationSteps = (int)(fs * 0.0025f);
        steps = modulationSteps;                     // modulate on first process()
        modeXfStep = 1.f / (0.005f * fs);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        configureRates(e.sampleRate);
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        pool.resetAll();
        driftL.reset(); driftR.reset();
    }

    void modulate() {
        float fsOs = sampleRate * oversample;

        // Drift (Vintage only) — module-level, per side; both walkers advance
        // every block so toggling Character mid-patch is deterministic.
        float dL = driftL.step(0.0025f, kVintageDriftOct);
        float dR = driftR.step(0.0025f, kVintageDriftOct);
        if (!vintageDrift) dL = dR = 0.f;

        float cutoffLog = params[CUTOFF_PARAM].getValue();
        float resKnob   = params[RES_PARAM].getValue();
        float driveKnob = params[DRIVE_PARAM].getValue();
        float cutAtt = params[CUTOFF_CV_PARAM].getValue();
        float resAtt = params[RES_CV_PARAM].getValue();
        float drvAtt = params[DRIVE_CV_PARAM].getValue();
        bool cutCv = inputs[CUTOFF_INPUT].isConnected();
        bool resCv = inputs[RES_INPUT].isConnected();
        bool drvCv = inputs[DRIVE_INPUT].isConnected();

        // Mode target (snap knob); start crossfade on change
        int m = (int)std::round(params[MODE_PARAM].getValue());
        if (m != modeTarget) { modeCurrent = modeTarget; modeTarget = m; modeXf = 0.f; }

        for (int c = 0; c < pool.activeVoices; c++) {
            OnbetapVoice& v = pool.voices[c];
            v.sanitize();

            float voiceLog = cutoffLog;
            if (cutCv) voiceLog += cutAtt * inputs[CUTOFF_INPUT].getPolyVoltage(c);
            // per-side drift handled at the filter level via g scale below;
            // g target uses the L drift (R applies the delta as a ratio)
            float fc = std::exp2(voiceLog + dL);
            v.gTarget = OnbetapFilter::cutoffToG(fc, fsOs);

            float res = resKnob;
            if (resCv) res += resAtt * inputs[RES_INPUT].getPolyVoltage(c) / 10.f;
            res = clamp(res, 0.f, 1.f);
            // rev-log damping map; onset ~res 0.72; min resonance Q≈1
            float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f) + tuneOnset;
            v.kTarget = k;   // phase-lag term applied per sample from slewed g

            float drive = driveKnob;
            if (drvCv) drive += drvAtt * inputs[DRIVE_INPUT].getPolyVoltage(c) / 10.f;
            drive = clamp(drive, 0.f, 1.f);
            float spanOct = tuneDriveDb / 6.0206f;               // dB → octaves
            float driveGain = std::exp2(-2.f + spanOct * drive); // 0.25 → …
            v.driveTarget  = driveGain * kBaseTrim * kVoltsToCore * tuneHeadroom;
            v.makeupTarget = std::sqrt(0.25f / driveGain)
                           * kOutScale * std::exp2(tuneOutDb / 6.0206f);

            // Character: mismatch + cutoff-scaled offset (vintage), R drift as
            // a relative g ratio so poly voices share one target.
            if (vintageDrift) {
                v.fL.setMismatch(onbetap::kMismatchL1, onbetap::kMismatchL2);
                v.fR.setMismatch(onbetap::kMismatchR1, onbetap::kMismatchR2);
                float offs = kVintageOffset * (voiceLog - std::log2(750.f)) * 0.25f;
                v.fL.setOffset(offs);
                v.fR.setOffset(-0.8f * offs);
                v.fRgRatio = std::exp2(dR - dL);   // add field float fRgRatio = 1.f;
            } else {
                v.fL.setMismatch(0.f, 0.f);
                v.fR.setMismatch(0.f, 0.f);
                v.fL.setOffset(0.f);
                v.fR.setOffset(0.f);
                v.fRgRatio = 1.f;
            }
            v.fL.setLimit(limitMode);
            v.fR.setLimit(limitMode);
        }
    }

    // One stereo side through the oversampled core. Returns output volts.
    float processSide(OnbetapFilter& flt, float& xPrev, float inVolts,
                      float g, float kEff, float driveScale, float makeup) {
        float lp = 0, bp = 0, hp = 0;
        float x1 = inVolts * driveScale;
        for (int i = 1; i <= oversample; i++) {
            float t = (float)i / oversample;
            float x = xPrev + (x1 - xPrev) * t;      // linear interp upsample
            auto o = flt.processG(x, g, kEff);
            lp += o.lp; bp += o.bp; hp += o.hp;      // average = crude decimator
        }
        xPrev = x1;
        float inv = 1.f / oversample;
        lp *= inv; bp *= inv; hp *= inv;

        // taps + 5 ms crossfade on mode change (Vintage: hard switch, DC step
        // and all, like the factory panel switch)
        auto tap = [&](int mode) {
            switch (mode) {
                case 0: return lp;
                case 1: return bp;
                case 2: return hp;
                case 3: return lp + hp;      // notch
                default: return lp - hp;     // peak
            }
        };
        float y = (modeXf >= 1.f || vintageDrift)
                ? tap(modeTarget)
                : tap(modeCurrent) + (tap(modeTarget) - tap(modeCurrent)) * modeXf;

        float v = y * makeup;
        return 9.f * OnbetapFilter::tanhish(v / 9.f);   // "overdriven VCA" stage
    }

    void process(const ProcessArgs& args) override {
        int voices = std::max({1, inputs[AUDIO_INPUT].getChannels(),
                                  inputs[AUDIO_INPUT_R].getChannels()});
        pool.setVoices(voices);
        outputs[AUDIO_OUTPUT].setChannels(voices);
        outputs[AUDIO_OUTPUT_R].setChannels(voices);

        if (++steps >= modulationSteps) { steps = 0; modulate(); }
        if (modeXf < 1.f) modeXf = std::min(1.f, modeXf + modeXfStep);
        dither = -dither;

        bool rConnected = inputs[AUDIO_INPUT_R].isConnected();
        bool outRConnected = outputs[AUDIO_OUTPUT_R].isConnected();

        for (int c = 0; c < voices; c++) {
            OnbetapVoice& v = pool.voices[c];
            float g      = v.gSlew.process(v.gTarget);
            float kBase  = v.kSlew.process(v.kTarget);
            float drive  = v.driveSlew.process(v.driveTarget);
            float makeup = v.makeupSlew.process(v.makeupTarget);
            float kEff   = kBase - kCLag * g * g / (1.f + g * g);
            kEff = std::max(kEff, -0.31f);           // denominator guard floor

            float inL = inputs[AUDIO_INPUT].getPolyVoltage(c) + dither;
            float outL = processSide(v.fL, v.xPrevL, inL, g, kEff, drive, makeup);
            outputs[AUDIO_OUTPUT].setVoltage(outL, c);

            if (rConnected) {
                float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c) + dither;
                float outR = processSide(v.fR, v.xPrevR, inR,
                                         g * v.fRgRatio, kEff, drive, makeup);
                outputs[AUDIO_OUTPUT_R].setVoltage(outR, c);
            } else {
                // R normalled to L → mirror L (skips a full core solve; in
                // Vintage this loses the L/R decorrelation, which is fine for
                // a mono patch)
                outputs[AUDIO_OUTPUT_R].setVoltage(outL, c);
            }
        }
    }

    // dataToJson/dataFromJson: extend the existing vintageDrift pair with
    // limitMode (int), oversample (int), and the four tune* floats. Missing
    // keys keep defaults (backward compatible with the shell's patches).
};
```

Also add `float fRgRatio = 1.f;` to `OnbetapVoice` in `src/onbetap/engine.hpp` (reset to 1.f in `reset()`).

Note on `processSide`: `modeXf`/`modeTarget` are module fields — implement `processSide` as a member function so it reads them directly (as written above).

- [ ] **Step 2: Update the DC path**

The VCA `tanhish` plus asymmetric core can leave DC on the output under heavy
drive (rectification — the hardware does this too; Erica AC-couples). Add a
per-side, per-voice DC blocker in `OnbetapVoice` and apply it in `processSide`
just before the VCA saturator:

```cpp
// engine.hpp — inside OnbetapVoice:
struct DCBlock { float x1 = 0.f, y1 = 0.f;
    float process(float x, float R) { float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0.f; } };
DCBlock dcL, dcR;   // reset() them in OnbetapVoice::reset()
```

Module: `float dcR_coef = 1.f - 2.f * M_PI * 1.6f / fs;` computed in
`configureRates` (a field), and in `processSide` (pass the right DCBlock in):
`v = dc.process(v, dcRCoef);` before the tanhish line.

- [ ] **Step 3: Build the VCV plugin**

Run: `make -C vcv -j8` (build dir per repo layout; if the Makefile lives at
`vcv/Makefile` this is correct — check `ls vcv/Makefile` first and use the
repo's documented build otherwise). Expected: compiles clean, `plugin.dylib`
produced. Fix warnings that indicate real problems (sign conversions in enums
etc. are pre-existing noise).

- [ ] **Step 4: Install and smoke-test in vcv-headless**

Install per the repo convention (no build-install.sh in this repo):

```bash
make -C vcv -j8
PLUG=~/Library/Application\ Support/Rack2/plugins-mac-arm64/RobotBoy
mkdir -p "$PLUG"
cp vcv/plugin.dylib "$PLUG/" && cp plugin.json "$PLUG/" && cp -R res "$PLUG/"
```

Write `/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/168cdb10-1fe9-49fd-b698-5e1aa42f219f/scratchpad/onbetap-smoke.json` (vcv-headless spec; port/param indices from the enums — CUTOFF_PARAM=0, CUTOFF_CV=1, RES=2, RES_CV=3, DRIVE=4, DRIVE_CV=5, MODE=6; AUDIO_INPUT=0, AUDIO_INPUT_R=1; AUDIO_OUTPUT=0, AUDIO_OUTPUT_R=1):

```json
{
  "plugin": "~/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy",
  "model": "Onbetap",
  "numSamples": 96000,
  "sampleRate": 48000,
  "input": "<scratchpad>/onbetap-in.wav",
  "output": "<scratchpad>/onbetap-out.wav",
  "audioInput": 0,
  "audioOutput": 0,
  "params": { "0": 9.55, "2": 0.3, "4": 0.2, "6": 0 }
}
```

(Expand `~` and `<scratchpad>` to absolute paths in the actual file.) Generate
a 2-second white-noise-plus-100Hz-saw input WAV with Python
(`~/Dev/python-scripts/.venv/`), run `~/Dev/vcv-headless/run.sh <spec>`, and
assert in Python: output RMS > 0.01, no NaN, and spectral centroid of the
output is well below the input's (LP mode at ~750 Hz does that). Repeat with
`"6": 2` (HP): centroid must be above the input's.

- [ ] **Step 5: Commit**

```bash
git add src/Onbetap.cpp src/onbetap/engine.hpp
git commit -m "Onbetap: implement filter engine in module (oversampled, stereo, poly)"
```

---

### Task 4: Context menu — resonance limiting, oversampling, tuning sliders

**Files:**
- Modify: `src/Onbetap.cpp` (`OnbetapWidget::appendContextMenu` and the dataToJson/dataFromJson pair)

**Interfaces:**
- Consumes: module fields from Task 3 (`limitMode`, `oversample`, `tuneDriveDb`, `tuneHeadroom`, `tuneOnset`, `tuneOutDb`).

- [ ] **Step 1: Extend persistence**

In `dataToJson`/`dataFromJson`, alongside `vintageDrift`: `limitMode` as int
(0=Hard, 1=Soft), `oversample` as int, the four `tune*` floats. On load,
clamp `oversample` to {1,2,4} (default 2 for anything else).

- [ ] **Step 2: Extend the menu**

After the existing Character items:

```cpp
menu->addChild(new MenuSeparator);
menu->addChild(createMenuLabel("Resonance limiting"));
menu->addChild(createMenuItem("Hard (factory rails)",
    m->limitMode == OnbetapFilter::Limit::Hard ? "✓" : "",
    [m]() { m->limitMode = OnbetapFilter::Limit::Hard; }));
menu->addChild(createMenuItem("Soft (diode clamp)",
    m->limitMode == OnbetapFilter::Limit::Soft ? "✓" : "",
    [m]() { m->limitMode = OnbetapFilter::Limit::Soft; }));

menu->addChild(new MenuSeparator);
menu->addChild(createIndexSubmenuItem("Oversampling",
    {"1x", "2x", "4x"},
    [m]() { return m->oversample == 1 ? 0 : m->oversample == 2 ? 1 : 2; },
    [m](int i) { m->oversample = (i == 0) ? 1 : (i == 1) ? 2 : 4; }));
```

For the Tuning submenu use a `createSubmenuItem` whose children are four
slider quantities (Rack `MenuSlider` over a small local `Quantity` subclass —
pattern: define `struct TuneQuantity : Quantity` with getters/setters bound to
a float* plus min/max/label/unit, then `menu->addChild(new MenuSlider(new
TuneQuantity(&m->tuneDriveDb, 24.f, 48.f, "Drive span", " dB")))` etc.):

- Drive span: 24–48 dB, default 36
- Core headroom: 0.5–2.0 ×, default 1.0
- Self-osc onset trim: −0.10–+0.10, default 0
- Output trim: −12–+12 dB, default 0

Changing `oversample` live changes fsOs used by the NEXT modulate() (g targets
recompute within 2.5 ms) — no extra plumbing needed, but call `pool.resetAll()`
in the setter to avoid a state discontinuity at the new rate.

- [ ] **Step 3: Build, install, verify persistence**

`make -C vcv -j8`, reinstall (Task 3 step 4 commands). Verify JSON roundtrip
compiles and defaults hold — in vcv-headless the module constructs with
defaults; menu behavior itself is GUI-only and goes on the user checklist
(per repo memory: no agent-driven GUI testing).

- [ ] **Step 4: Commit**

```bash
git add src/Onbetap.cpp
git commit -m "Onbetap: add resonance limiting, oversampling, tuning menu"
```

---

### Task 5: Calibration against acceptance targets

**Files:**
- Create: `/private/tmp/.../scratchpad/onbetap-cal/*.py|json|wav` (scratch only)
- Modify: `src/Onbetap.cpp` (constants), possibly `src/onbetap/OnbetapFilter.hpp` (kAsymNeg/rails only if a target demands it)
- Modify: `tests/onbetap/test_onbetap.cpp` (update bounds if constants moved)

Calibration is measurement-driven: render through vcv-headless, measure in
Python (venv at `~/Dev/python-scripts/.venv/`), adjust the module constants,
re-render. Document each measured number in the worklog.

- [ ] **Step 1: Level targets**

- 1 kHz sine, 10 Vpp, drive=0, res=0, LP mode, cutoff max (20 kHz): output
  within ±2 dB of input level. Adjust `kOutScale`.
- Same but drive=1: output RMS no more than +6 dB over the drive=0 case
  (makeup keeps sweeps usable), audibly saturated (THD proxy: harmonic RMS
  above 3 kHz > −20 dB relative).
- Self-osc (no input, res=1, cutoff 1 kHz): output peak 8–10 V.

- [ ] **Step 2: Behavior targets (each rendered + measured)**

- Self-osc onset: sweep res 0→1 over 20 s at cutoff 1 kHz; oscillation
  (output RMS with no input > 0.5 V) must begin at res 0.65–0.80. Adjust the
  k-map exponent/`tuneOnset` default if outside.
- Onset moves earlier at 8 kHz cutoff than at 200 Hz (phase-lag term working):
  compare onset res values; expect ≥ 0.03 earlier. Adjust `kCLag`.
- Drive suppresses resonance end-to-end: BP mode, res=0.6, 1 kHz sine at fc;
  gain(in=0.5 V) vs gain(in=8 V) — the loud gain must be ≥ 6 dB lower.
- Hard vs Soft: render both self-osc; hard crest factor < soft crest factor;
  note frequencies in the worklog (hardware lore: soft sits slightly higher).
- Vintage: 60 s render at fixed settings; measured self-osc frequency wanders
  (std of per-second pitch estimates > 0.2%, < 3%); L and R outputs decorrelate
  (correlation < 0.99 with both jacks driven by the same input).
- Aliasing sanity: 5 kHz sine, max drive, LP 20 kHz, 2× OS: strongest
  non-harmonic spur < −40 dB relative to fundamental. If badly worse, check
  the decimator before reaching for 4×.

- [ ] **Step 3: Re-run the unit lane, update bounds if constants moved**

`cd tests && ./run.sh` — green.

- [ ] **Step 4: Record results and commit**

Append the measured table to `docs/research/onbetap-worklog.md`.

```bash
git add src/Onbetap.cpp src/onbetap/OnbetapFilter.hpp tests/onbetap/test_onbetap.cpp docs/research/onbetap-worklog.md
git commit -m "Onbetap: calibrate levels, onset, drive interaction"
```

---

### Task 6: MetaModule build + headless simulator verification

**Files:**
- No repo file changes expected (Onbetap is already in `metamodule/CMakeLists.txt`, `metamodule/plugin-mm.json`, `init_RobotBoy`); fixes only if the MM compile finds portability issues (e.g. libm usage, `std::pow` fine; no `malloc.h`).
- Create: `<scratchpad>/onbetap-mm/patch.yml` (test patch)

- [ ] **Step 1: Build the .mmplugin**

Per `vcv-to-metamodule` conventions already set up in `metamodule/`:

```bash
cd metamodule && cmake --fresh -B build -GNinja && cmake --build build
```

Expected: `metamodule-plugins/RobotBoy.mmplugin` produced. Fix any Onbetap
compile issues (the adapter builds `src/Onbetap.cpp` with `METAMODULE_BUILTIN`).

- [ ] **Step 2: Build the headless simulator with RobotBoy built in**

Per the build-simulator skill and ~/Dev/metamodule/CLAUDE.md (keep that
checkout clean; never edit ext-plugins.cmake):

```bash
cd ~/Dev/metamodule/simulator
cmake --fresh --preset headless -Dext_builtin_brand_paths="$HOME/Dev/RobotBoy/.worktrees/worktree-polivoks/metamodule" -Dext_builtin_brand_libname="RobotBoy"
cmake --build build-headless
```

(If the headless preset ignores ext plugins, fall back to the GUI-simulator
build path documented in the skill and use `make headless` semantics — read
`simulator/CMakePresets.json` to confirm the preset accepts the cache vars.)

- [ ] **Step 3: Write a test patch and render**

Copy an existing simple patch from `~/Dev/metamodule/patches/default/` as a
YAML template; replace with: audio In 1/2 → Onbetap L/R in → Onbetap L/R out →
Out 1/2, knobs at cutoff=0.5, res=0.3, drive=0.2, mode LP. Render:

```bash
build-headless/simulator -p <scratchpad>/onbetap-mm/patch.yml --in <the same input wav as Task 5> --out <scratchpad>/onbetap-mm/out-mm.wav -n 96000
```

Note the printed "Effective load" percentage → worklog (this is the MM CPU
proxy; record it at 2× and 1× oversampling by re-rendering with a patch that
has the menu setting changed — MM patch files store module state; if setting
oversample via patch YAML is awkward, record 2× only and note it).

- [ ] **Step 4: Compare against vcv-headless**

Render the identical input through vcv-headless (Task 5 spec, same knob
values) and compare in Python: same length ±1 sample, per-sample max abs
difference after aligning (both are the same C++ code at the same rate —
expect near-bit-identical; the pass bar is max |diff| < 1e-3 of full scale,
investigate anything above).

- [ ] **Step 5: Commit (worklog + any portability fixes)**

```bash
git add -A docs/research/onbetap-worklog.md src/ metamodule/ 2>/dev/null
git commit -m "Onbetap: verify MetaModule build in headless simulator"
```

---

### Task 7: Docs, changelog, final sweep

**Files:**
- Create: `Onbetap.md` (repo root, sibling of `MF20.md` — follow its structure: what it is, controls, menu options, CV behavior, character modes, credits/references)
- Modify: `CHANGELOG.md` (unreleased section: "Onbetap: implement Polivoks filter DSP …")
- Modify: `docs/research/onbetap-worklog.md` (final summary + user checklist)

- [ ] **Step 1: Write Onbetap.md** — document every control, menu item
(Character, Resonance limiting, Oversampling, Tuning sliders and what each
does), the model's provenance (one paragraph + pointer to
`docs/research/`), and known behaviors (drive suppresses resonance, onset
moves with cutoff, Vintage drift is deterministic per session).

- [ ] **Step 2: User checklist** — append to the worklog a GUI checklist for
the user (repo memory: GUI-sim checks are user-run): load in VCV Rack, sweep
each mode, self-osc at max res, Character toggle audibly drifts, menu sliders
respond, MM GUI simulator spot-check.

- [ ] **Step 3: Final verification sweep**

- `cd tests && ./run.sh` — all green
- `make -C vcv -j8` — clean build
- `cd metamodule && cmake --build build` — clean build

- [ ] **Step 4: Commit**

```bash
git add Onbetap.md CHANGELOG.md docs/research/onbetap-worklog.md
git commit -m "Onbetap: add module docs and changelog entry"
```

---

## Self-review notes

- Spec §2 core model → Task 1; §3 discretization → Tasks 1 (core) + 3
  (oversampling, g-domain smoothing); §4 module behavior → Task 3; §5
  character → Tasks 2 (drift/mismatch machinery) + 3 (wiring); §6 menu →
  Task 4; §7 layout/tests → Tasks 1–2; §8 acceptance → Tasks 5–7. No gaps.
- Type/name consistency: `OnbetapFilter::{Limit,Out,processG,cutoffToG,sat,
  satGain,tanhish,setLimit,setMismatch,setOffset,reset,stateFinite}`,
  `OnbetapVoice::{fL,fR,gSlew,kSlew,driveSlew,makeupSlew,*Target,xPrevL,
  xPrevR,fRgRatio,dcL,dcR,setAlpha,reset,sanitize}`, `OnbetapPool`,
  `onbetap::DriftWalker`, module fields `limitMode/oversample/tune*` — used
  identically across tasks.
- The Task 1 test bounds are behavioral measurements; the plan explicitly
  says which may be nudged (osc freq/crest) and which may not (slopes,
  suppression, stability, determinism).
