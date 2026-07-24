# Loopers + Beads-family CPU Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the MetaModule CPU optimizations recorded in `cpu-optimization-other-modules-2026-07-24.md` (repo root) for Loooop/Löp, Ondes, Particules, and Retours.

**Architecture:** Per-concern commits on branch `cpu-opt-2`, mirroring the filter pass (`cpu-optimization-2026-07-24.md` §10). Each task is one commit. Caches are implemented as **value-compare** (compare inputs each call, recompute on mismatch) wherever possible — self-invalidating, no dirty-flag paths to miss.

**Tech Stack:** C++20; test lanes: `tests/run.sh` (looped dirs: mf20, loooop, particules, onbetap, vespid + Python guards), `tests/particules_dsp/run.sh` (Catch2), `tests/retours_delay_dsp/run.sh` (Catch2). Builds: `make -C vcv` (desktop), `cmake --build build-mm` per `build-robotboy-plugin` skill (MetaModule).

## Global Constraints

- The MetaModule SDK compiles **without `-ffast-math`** (only `-fno-math-errno`): `x / constant` is a real VDIV; `x * (1.0f/constant)` must be written explicitly. Cortex-A7 has no VRINT: `floor/lround/round/fmod` are out-of-line libm calls.
- Scope decisions (user-approved 2026-07-24): **implement everything in the findings doc EXCEPT** Particules M2 / Retours M4 (dead-SVF-tick skip — dropped, savings too small) and Loooop M4 (double→float lerp in `readRaw` — dropped to preserve bit-exact A/B testability of Task 7). **Include** Ondes' fast exp2 (H1 option 3) — user explicitly wants audio-rate FM performance.
- Equivalence classes per change are stated in the findings doc; anything marked *exact* must not change output bits. Float-noise items (\~1 ulp) are acceptable. Feedback-path changes (saturation, reverb) cannot be verified by free-running A/B (chaos) — rely on the behavioral Catch2 suites.
- Commit messages: short, one sentence, ≤15 words, **no AI attribution lines**.
- Do NOT commit the user's pre-existing modified files (`Loooop.md`, `Ondes.md`, `Particules.md`, `README.md`, `Retours.md`). Stage files explicitly by path; never `git add -A`.
- Shared files (`dsp_utils.h`, `saturation.cpp`, `sample_codec.h`, `quality_processor.*`, `recording_buffer.*`) affect both Particules and Retours: run BOTH Catch2 suites after touching them.
- Every task ends with its named test lane(s) passing. Task 16 is the full gate: all suites + both builds.

---

### Task 1: Branch, docs, fast exp2 utility + accuracy test

**Files:**
- Commit: `cpu-optimization-other-modules-2026-07-24.md`, `docs/superpowers/plans/2026-07-24-cpu-optimization-other-modules.md`
- Create: `src/particules/dsp/src/util/fast_exp2.h`
- Test: `tests/particules_dsp/test_fast_exp2.cpp`

**Interfaces:**
- Produces: `particules_dsp::Exp2Fast(float y) -> float` (max rel err ≤ 2e-5 for y in [-11, 11]) and `particules_dsp::SemitonesToRatioFast(float st) -> float` = `Exp2Fast(st * (1.0f/12.0f))`.

- [ ] **Step 1:** `git checkout -b cpu-opt-2`, then commit the two docs: `git add cpu-optimization-other-modules-2026-07-24.md docs/superpowers/plans/2026-07-24-cpu-optimization-other-modules.md && git commit -m "Record loopers/Beads-family CPU findings and implementation plan"`
- [ ] **Step 2:** Write the failing test. Look at `tests/particules_dsp/test_sample_codec.cpp` for the suite's Catch2 include/style conventions and match them. Content:

```cpp
#include "catch2/catch_amalgamated.hpp"
#include "../../src/particules/dsp/src/util/fast_exp2.h"
#include <cmath>

TEST_CASE("Exp2Fast matches exp2f within 2e-5 over the pitch range") {
    float max_rel = 0.f;
    for (float y = -11.f; y <= 11.f; y += 1e-3f) {
        float ref = std::exp2(y);
        float got = particules_dsp::Exp2Fast(y);
        max_rel = std::max(max_rel, std::abs(got - ref) / ref);
    }
    REQUIRE(max_rel < 2e-5f);
}

TEST_CASE("Exp2Fast is exact at integer exponents") {
    for (int n = -10; n <= 10; ++n)
        REQUIRE(particules_dsp::Exp2Fast((float)n) == std::exp2((float)n));
}
```

Register the file in `tests/particules_dsp/CMakeLists.txt` following how the other test files are listed.

- [ ] **Step 3:** Run `cd tests/particules_dsp && ./run.sh` — expect compile failure (header missing).
- [ ] **Step 4:** Create `src/particules/dsp/src/util/fast_exp2.h` — same construction as the proven `src/vespid/fastsinh.hpp` `exp2Fast` (degree-6 polynomial for the fraction, exponent via bit add; see that file and findings-doc §9.4 of `cpu-optimization-2026-07-24.md`):

```cpp
#pragma once
#include <cstdint>
#include <cstring>

namespace particules_dsp {

// Divide-free, libm-free exp2. Max relative error ~1.5e-5. Same construction
// as src/vespid/fastsinh.hpp's exp2Fast (kept separate: this dsp tree is
// self-contained). Argument must be finite and within float exponent range.
inline float Exp2Fast(float y) {
    int n = (int)y;
    n -= ((float)n > y);          // floor for negative y
    float f = y - (float)n;       // f in [0,1)
    float p = 1.f + f * (0.69314718056f + f * (0.24022650696f
              + f * (0.05550410866f + f * (0.00961812911f
              + f * (0.00133335581f + f * 0.00015403530f)))));
    uint32_t bits;
    std::memcpy(&bits, &p, 4);
    bits += (uint32_t)n << 23;
    float r;
    std::memcpy(&r, &bits, 4);
    return r;
}

inline float SemitonesToRatioFast(float semitones) {
    return Exp2Fast(semitones * (1.0f / 12.0f));
}

} // namespace particules_dsp
```

- [ ] **Step 5:** Run `./run.sh` — expect PASS (both new tests, no regressions).
- [ ] **Step 6:** Commit: `git add src/particules/dsp/src/util/fast_exp2.h tests/particules_dsp/test_fast_exp2.cpp tests/particules_dsp/CMakeLists.txt && git commit -m "Add divide-free fast exp2 to particules dsp tree with accuracy test"`

---

### Task 2: Ondes oscillator hot-path rework

**Files:**
- Modify: `src/particules/dsp/src/wavetable/wavetable_oscillator.h` and `.cpp`
- Test: `tests/particules_dsp/test_wavetable_oscillator.cpp` (extend)

**Context:** Findings §3 (Ondes H1–H3, M1). `Process` is called with `num_frames = 1` per sample from `src/particules/Ondes.cpp:73`. Current per-sample cost: `exp2f`, `/12`, `/sample_rate_`, `fmodf`, 7 virtual provider calls, bank/wave region math.

**Interfaces:**
- Consumes: `particules_dsp::Exp2Fast` / `SemitonesToRatioFast` from Task 1.
- Produces: same public `Process()` signature — no caller changes.

- [ ] **Step 1:** Read `wavetable_oscillator.cpp` fully (86 lines) and `test_wavetable_oscillator.cpp` to see existing coverage.
- [ ] **Step 2:** Extend the test FIRST with equivalence cases (they must pass against the OLD code — run once to confirm — and still pass after the rework):

```cpp
// (adapt includes/fixture to the existing test file's conventions;
//  it already constructs an oscillator with a test provider)
TEST_CASE("Process(N frames) equals N x Process(1 frame)") {
    // Two oscillators, same provider. One renders 256 frames in one call;
    // the other renders 256 calls of 1 frame (the Ondes usage pattern).
    // pitch=7.3f, bank=0.4f, wave=0.6f held constant.
    // REQUIRE bit-identical outputs. This pins the setup-vs-loop split.
}
TEST_CASE("Static-pitch output is periodic after rework (phase wrap exact)") {
    // pitch = 12.0f (integer octave: Exp2Fast exact), render 4096 frames,
    // assert no NaN and output within [-1.5, 1.5], and that phase never
    // produces a discontinuity bigger than the max sample-to-sample delta
    // of the raw table (guards the fmod-removal edge).
}
```

- [ ] **Step 3:** Run the suite; new tests must pass against current code (fix the tests if not — they encode current behavior, except pitch handling which changes by ≤2e-5 in frequency; do NOT assert absolute frequency).
- [ ] **Step 4:** Rework the oscillator:

Header — add members:

```cpp
    float phase_scale_ = 0.0f;        // kWavetableSize / sample_rate_, set in Init()
    // Value-compare caches (self-invalidating): recompute only when the
    // corresponding input actually changes.
    float last_pitch_ = -1e9f;
    float last_bank_ = -1.0f, last_wave_ = -1.0f;
    int   num_banks_ = 0, waveforms_per_bank_ = 0;    // cached in SetProvider()
    const float *w_ll_ = nullptr, *w_lh_ = nullptr, *w_hl_ = nullptr, *w_hh_ = nullptr;
    float bank_frac_ = 0.0f, wave_frac_ = 0.0f;
```

`Init()`: set `phase_scale_ = static_cast<float>(kWavetableSize) / sample_rate;` (one divide, init-time) and reset `last_pitch_`/`last_bank_`/`last_wave_` to the sentinels. `SetProvider()`: cache `num_banks_`/`waveforms_per_bank_` (0 if null provider), reset `last_bank_`/`last_wave_` sentinels.

`Process()`:
1. Pitch block becomes:
```cpp
    if (pitch_semitones != last_pitch_) {
        last_pitch_ = pitch_semitones;
        float clamped = Clamp(pitch_semitones, -120.0f, 120.0f);
        phase_increment_ = kBaseFreq * SemitonesToRatioFast(clamped) * phase_scale_;
    }
```
2. Bank/wave block: wrap the existing region math + 4 `GetWaveform` calls in `if (bank != last_bank_ || wave != last_wave_) { ... }`, storing the four pointers and both fracs in the new members; drop the duplicate `NumBanksAvailable()` call (use `num_banks_`, `waveforms_per_bank_`).
3. Frame loop: replace `phase_ = std::fmod(phase_, kWavetableSize)` with reconstruction from values the loop already computes (bit-exact — only masked int + frac ever reach the lookup):
```cpp
        phase_ = static_cast<float>(phase_int) + phase_frac + phase_increment_;
        // wrap the integer part next iteration via the existing mask; keep
        // phase_ bounded with an exact conditional subtract (increment <= ~1429):
        while (phase_ >= static_cast<float>(kWavetableSize))
            phase_ -= static_cast<float>(kWavetableSize);
```
Note: `phase_int` must be masked BEFORE this reconstruction (it already is, at the top of the loop). Preserve the null-provider early-outs using `num_banks_`.

- [ ] **Step 5:** Run `tests/particules_dsp/run.sh` — all green (including Task 1 and Step 2 tests).
- [ ] **Step 6:** Commit: `git add -u src/particules/dsp tests/particules_dsp && git commit -m "Ondes: cache pitch and bank setup, fast exp2, exact phase wrap"`

---

### Task 3: Looper trivial wins (engine reciprocal, core lround/divide swaps, V/Oct memoize)

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp` + `.hpp`, `metamodule/loooop/LoooopCore.cc`, `metamodule/loooop/LopCore.cc`, `src/loooop/LooperModuleDSP.hpp`, `src/loooop/Loooop.cpp`, `src/loooop/Lop.cpp`

**Context:** Findings §2 H4, M1–M3 + desktop Low items.

- [ ] **Step 1 (H4):** Add `float invLoopLen_ = 0.f;` to `LoopEngine.hpp` members. Set it at every `loopLen_` assignment: `reset()` (`loopLen_ = 0` → set 0), `toggleRecord` (`loopLen_ = writeIdx_`), the auto-end in `process()` (`loopLen_ = maxSamples_`), `clear()` (`loopLen_ = 0`), computing `1.f / static_cast<float>(loopLen_)` only when `loopLen_ > 0`. Replace the two `const float invL = 1.f / static_cast<float>(loopLen_);` computations (in `advanceHead` and the parked-head branch of `process()`) with `invLoopLen_`. Exact.
- [ ] **Step 2 (M1):** In both cores, `(int)std::lround(getState<GridKnob>() * 5.f)` → `(int)(getState<GridKnob>() * 5.f + 0.5f)` (knob state is 0..1, non-negative — exact here). Same swap for the desktop equivalents `std::round(params[...].getValue())` patterns in `Loooop.cpp:141,145` and `Lop.cpp:93,97` (verify those are also non-negative param ranges before swapping; if a range includes negatives, use `std::lround` → skip).
- [ ] **Step 3 (M3):** In both cores: `in.l / 5.f` → `in.l * 0.2f` (float-noise); in `Loooop.cpp` and `Lop.cpp` hoist the duplicated `inL/5.f`/`inR/5.f` into single `const float` locals and use `* 0.2f`.
- [ ] **Step 4 (M2):** In `LooperModuleDSP.hpp`, next to `speedFromVOct`, add:

```cpp
// Per-head memo for speedFromVOct: exp2f is a libm call on MetaModule; the
// (knob, cv) pair is static or slow-moving in practice. Exactly equivalent.
struct VOctSpeedMemo {
    float knob = -1e9f, cv = -1e9f, out = 0.f;
    float get(float k, float c) {
        if (k != knob || c != cv) { knob = k; cv = c; out = speedFromVOct(k, c); }
        return out;
    }
};
```

In each MM core add `loooop::VOctSpeedMemo voctMemo_[LoopEngine::NUM_HEADS];` (Löp: single) and route the `speedFromVOct` call in `updateHead` through it (the head index is the `h` argument). Do the same in the desktop modules if their per-head speed code calls `speedFromVOct` per sample (read `Loooop.cpp:159-191` to confirm; apply identically).
- [ ] **Step 5:** Run `tests/run.sh` (at minimum the loooop dir must pass; run the full script). Expect green.
- [ ] **Step 6:** Build check both targets per the `build-robotboy-plugin` skill (VCV `make -C vcv`, MM cmake). Expect clean.
- [ ] **Step 7:** Commit: `git add -u && git status --short` (verify ONLY intended files staged; unstage any user doc files) then `git commit -m "Loopers: reciprocal loop length, integer rounding, V/Oct speed memo"`

---

### Task 4: LoopEngine revision-bump throttle

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp` + `.hpp`
- Test: `tests/loooop/test_loop_engine.cpp` (extend)

**Context:** Findings §2 H1. `bumpWaveformRevision()` (release store = `dmb ish` on ARMv7) is called per recorded sample at `LoopEngine.cpp:473` and `:500`, which also forces the GUI to re-scan the whole waveform every frame while recording.

- [ ] **Step 1:** Add to the header: `std::uint32_t revThrottle_ = 0;` and `static constexpr std::uint32_t REV_THROTTLE_MASK = 2047;` (\~23 Hz display refresh at 48 kHz).
- [ ] **Step 2:** In both per-sample record paths (initial pass `:473`, overdub `:500`) replace the unconditional `bumpWaveformRevision();` with:

```cpp
            if ((++revThrottle_ & REV_THROTTLE_MASK) == 0)
                bumpWaveformRevision();   // throttled: content changes every sample while recording
```

- [ ] **Step 3:** Guarantee a final bump at every recording-state transition so the display converges: audit and add an unconditional `bumpWaveformRevision()` (if not already present) at: the auto-end branch (already bumps), `toggleRecord()` where a record/overdub pass starts or stops, `clear()`, and the `stopPending_` completion path in `process()`. Reset `revThrottle_ = 0` when a record pass starts (so the first written sample bumps within 2048 samples of starting — plus the transition bump makes the start visible immediately).
- [ ] **Step 4:** Extend `test_loop_engine.cpp`: record N=10000 samples, assert `waveformRevision()` changed vs before recording AND changed again after `toggleRecord()` ends the pass; assert the revision did NOT increment 10000 times (e.g. delta < 100) — pins the throttle.
- [ ] **Step 5:** Run `tests/run.sh` → green. Commit: `git add -u src/loooop tests/loooop && git commit -m "Loooop: throttle waveform revision bumps during recording"`

---

### Task 5: LoopEngine windowBounds value-compare cache

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp` + `.hpp`

**Context:** Findings §2 H5. `windowBounds` (grid path: 3 double divides + 2 `lround` libm) runs per head per sample; all inputs are control-rate. Setters are called every sample by hosts, so the cache MUST be value-compare, not dirty-flag. Two call flavors exist: `jitterOff` (main) and `jitterNext` (seam preview at `:404`).

- [ ] **Step 1:** In the header add a mutable per-head, per-flavor cache:

```cpp
    struct WinCache {
        float size = -1.f, centre = -2.f, jitterOff = -2.f;
        int grid = -1; bool gridExclude = false;
        std::size_t loopLen = static_cast<std::size_t>(-1);
        double winStart = 0.0, winLen = 1.0;
    };
    // [head][flavor]: flavor 0 = h.jitterOff calls, 1 = h.jitterNext calls.
    mutable std::array<std::array<WinCache, 2>, NUM_HEADS> winCache_{};
```

- [ ] **Step 2:** Rename the existing worker `windowBounds(h, jitterOff, ws, wl)` to `windowBoundsUncached(...)` (keep it byte-identical). Add:

```cpp
void LoopEngine::windowBoundsCached(const PlayHead& h, int headIdx, int flavor,
                                    float jitterOff, double& ws, double& wl) const {
    WinCache& c = winCache_[headIdx][flavor];
    if (h.size != c.size || h.centre != c.centre || jitterOff != c.jitterOff
        || grid_ != c.grid || h.gridExclude != c.gridExclude || loopLen_ != c.loopLen) {
        c.size = h.size; c.centre = h.centre; c.jitterOff = jitterOff;
        c.grid = grid_; c.gridExclude = h.gridExclude; c.loopLen = loopLen_;
        windowBoundsUncached(h, jitterOff, c.winStart, c.winLen);
    }
    ws = c.winStart; wl = c.winLen;
}
```

`minWinLen_` changes only in `reset()`/`setSampleRate()`: invalidate all caches there (set `loopLen = size_t(-1)`).
- [ ] **Step 3:** Route the hot call sites through the cache **with the head index available**: `process()` playing branch, `process()` parked branch, `readHead`'s preview (needs `headIdx` — thread it through as a new parameter or compute the head index in `process()` and pass down; `readHead` is only called from `process()`), and `advanceHead`'s two post-wrap recomputes. Non-hot sites (`restartHead`, `jumpHead`, `displaySnapshot` paths) may keep calling the uncached form via the old 2-arg wrapper — keep that wrapper delegating to `windowBoundsUncached`.
- [ ] **Step 4:** Run `tests/run.sh` — the loooop suite has grid/jitter/window coverage; all green. Both builds clean.
- [ ] **Step 5:** Commit: `git add -u src/loooop && git commit -m "Loooop: value-compare cache for per-head window bounds"`

---

### Task 6: LoopEngine fade/seam divide reordering

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp`

**Context:** Findings §2 H3. `fadeLen`/`oneShotFadeLen` divide `winLen / sp` every call; `readHead` divides to compute `outToSeam` just to test seam proximity; `oneShotFadeGain` similar. Reorder so the divide only executes inside the (rare) fade window. Classification compare `dist < F*sp` vs `dist/sp < F` can differ by 1 ulp at the boundary — one sample earlier/later entering a fade whose gain is \~0 at the edge (smoothstep): float-noise class, note in commit.

- [ ] **Step 1:** Rework `fadeLen` (and `oneShotFadeLen` identically):

```cpp
int LoopEngine::fadeLen(const PlayHead& h, double winLen) const {
    if (!crossfade_ || xfadeSamples_ == 0 || h.oneShot) return 0;
    const double sp = std::fabs(static_cast<double>(h.speed));
    if (sp < 1e-9) return 0;
    int F = static_cast<int>(xfadeSamples_);
    // cap = (winLen / sp) * 0.5; only divide when the cap actually binds.
    if (static_cast<double>(F) * sp * 2.0 > winLen)
        F = static_cast<int>((winLen / sp) * 0.5);
    return F < 1 ? 0 : F;
}
```

- [ ] **Step 2:** In `readHead`, replace the always-computed `outToSeam` with a multiply-first proximity test:

```cpp
    const double sp = std::fabs(static_cast<double>(h.speed));
    const double distToSeam = (h.speed >= 0.f) ? (winStart + winLen - h.pos)
                                               : (h.pos - winStart);
    if (distToSeam < 0.0 || distToSeam >= static_cast<double>(F) * sp) return;
    const double outToSeam = distToSeam / sp;   // rare: only inside the fade window
```

Apply the same pattern in `oneShotFadeGain` (test `outToEnd`'s bounds via `dist` vs `F*sp` before dividing).
- [ ] **Step 3:** Run `tests/run.sh` → green (seam/crossfade tests exist in the loooop suite). Commit: `git add -u src/loooop && git commit -m "Loooop: divide only inside fade windows, multiply-first seam tests"`

---

### Task 7: LoopEngine interpolated-read interior fast path

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp` + `.hpp`
- Test: `tests/loooop/test_loop_engine.cpp` (extend)

**Context:** Findings §2 H2 — the deepest looper change. Current: per channel per head per sample, `readInterpolated` does `std::floor(double)` + 4 `readRaw`/`tapWrapped` calls, and `readRaw` floors twice more; ≥10 double-`floor` libm calls per head per sample, duplicated across L/R. Interior taps are integral → direct loads. **Must be bit-identical** (fast path reads the same buffer values; `frac` from cast == `frac` from floor for p ≥ 0; `readRaw` at integral p returns exactly `buf[i0]`).

- [ ] **Step 1:** Write the pinning test FIRST (against current code): copy the current `readInterpolated`/`readRaw`/`tapWrapped` into the test file as `refReadInterpolated` (free functions over a `std::vector<float>` + explicit `loopLen`). Fill a 4096-sample buffer with deterministic pseudo-random floats. For a grid of windows — full window, fractional grid-style window (`winStart = 341.333, winLen = 1365.333`), tiny window (`winLen = minWinLen`), window at buffer start and end — and positions marching across the whole window in steps of 0.37 (crossing both edges), `REQUIRE` the engine's public `process()` head output... **Correction:** `readInterpolated` is private. Instead: temporarily make the test a friend? No — the loooop suite already links `LoopEngine.cpp` directly (see `test_loop_engine.cpp.extra`) and tests through the public API. So pin end-to-end: drive `process()` with a recorded deterministic buffer (record 4096 known samples via the public record path), configure one head per scenario above (size/position/grid/speed incl. negative and fractional speeds 0.73, -1.31), capture 8192 output samples into a vector, and store its FNV-1a hash as the expected value in the test. Run once BEFORE the rework to fill in the hashes; the reworked code must reproduce them bit-exactly.
- [ ] **Step 2:** Run the new test against unmodified code; paste the printed hashes into the REQUIREs; re-run → PASS.
- [ ] **Step 3:** Implement. In `readHead`, replace the two `readInterpolated` calls with one shared-index read:

```cpp
// Shared L/R Catmull-Rom read. Interior fast path: all four taps in
// [winStart, winStart+winLen) and inside [0, loopLen) -> direct loads,
// no floor/libm, indices shared across channels. Falls back to the
// original per-channel path within 2 samples of a window edge.
void LoopEngine::readInterpolatedLR(const PlayHead& h, double winStart, double winLen,
                                    float& outL, float& outR) const {
    double p = h.pos;
    if (p < winStart || p >= winStart + winLen) p = winStart;
    const double ip = std::floor(p);                     // one floor per head, not 10
    const float frac = static_cast<float>(p - ip);
    const long long i = static_cast<long long>(ip);
    const bool interior =
        (ip - 1.0 >= winStart) && (ip + 2.0 < winStart + winLen) &&
        (i >= 1) && (static_cast<std::size_t>(i + 2) < loopLen_);
    if (interior) {
        const std::size_t i0 = static_cast<std::size_t>(i);
        auto cr = [frac](float t0, float t1, float t2, float t3) {
            const float c1 = 0.5f * (t2 - t0);
            const float c2 = t0 - 2.5f * t1 + 2.f * t2 - 0.5f * t3;
            const float c3 = 1.5f * (t1 - t2) + 0.5f * (t3 - t0);
            return ((c3 * frac + c2) * frac + c1) * frac + t1;
        };
        outL = cr(bufL_[i0-1], bufL_[i0], bufL_[i0+1], bufL_[i0+2]);
        outR = cr(bufR_[i0-1], bufR_[i0], bufR_[i0+1], bufR_[i0+2]);
    } else {
        outL = readInterpolated(h, bufL_, winStart, winLen);
        outR = readInterpolated(h, bufR_, winStart, winLen);
    }
}
```

**Bit-exactness caveat to verify while implementing:** the original computes taps as `(1.0-frac')*buf[i0]+frac'*buf[i1]` in double then casts; for integral taps `frac' == 0.0` so the value is exactly `buf[i]` — confirmed. The base tap `readRaw(ip)` uses the same `buf[i]` when interior. The Catmull-Rom float arithmetic is IDENTICAL to the original (`:318-321`) — copy it exactly, do not reassociate.
- [ ] **Step 4:** Run the pinning test → must pass with the SAME hashes. Run full `tests/run.sh` → green. Both builds clean.
- [ ] **Step 5:** Commit: `git add -u src/loooop tests/loooop && git commit -m "Loooop: shared-index interior fast path for head reads"`

---

### Task 8: LoopEngine display-mirror throttle

**Files:**
- Modify: `src/loooop/dsp/LoopEngine.cpp` + `.hpp`

**Context:** Findings §2 M5. `advanceHead` does 3 float converts/multiplies + 4 relaxed stores per head per sample; the parked-head branch does a windowBounds + 3 stores per armed head per sample.

- [ ] **Step 1:** Add `std::uint32_t dispThrottle_ = 0;` to the header. In `process()`, once per call: `const bool dispTick = ((dispThrottle_++ & 63) == 0);` (750 Hz at 48 kHz). Pass `dispTick` into `advanceHead` (new parameter) and gate the three float stores (`dispPos01_`, `dispWinStart01_`, `dispWinEnd01_`) on it. Keep `dispPlaying_` unconditional (state transitions must publish immediately). Gate the parked-head branch's stores the same way (its windowBounds is already cached from Task 5; skip the whole block when `!dispTick`).
- [ ] **Step 2:** Check `tests/loooop/test_display_renderer.cpp` and `test_loop_engine.cpp` for assertions that read `displaySnapshot()` immediately after a single `process()` call — if any exist, tick the engine ≥64 samples in those tests instead (document why in a comment).
- [ ] **Step 3:** `tests/run.sh` → green. Commit: `git add -u src/loooop tests/loooop && git commit -m "Loooop: throttle display mirror stores to 750 Hz"`

---

### Task 9: Particules + Retours wrapper hot-path fixes (Quality gate, clock light)

**Files:**
- Modify: `src/particules/Particules.cpp`, `src/retours_delay/Retours.cpp`

**Context:** Findings §4 H1 / §5 H3, H4. Both wrappers run `quality_state_ = std::clamp((int)std::lround(params[QUALITY_PARAM].getValue()), 0, 3);` per sample inside `#ifdef METAMODULE` (Particules `:406`, Retours `:327`), consumed only at block rate. Retours also pays a `/kClockFlashDecaySeconds` divide + 2 `setBrightness` per sample (`:388-405`).

- [ ] **Step 1:** In both files, move the METAMODULE quality read inside the existing `if (block_runtime_.BlockReady())` branch (next to `updateSlowParams()`), and replace `std::lround(x)` with `(int)(x + 0.5f)` (param range 0..3). Keep the desktop `#else` branch untouched.
- [ ] **Step 2:** Retours clock light: move the decay block to block rate. Inside `BlockReady()`: `clock_light_level_ = std::max(0.f, clock_light_level_ - kBlockSize * args.sampleTime * 12.5f);` (12.5f = 1/0.08, exactly representable; use the runtime's actual block-size constant name). The flash SET (on clock edge) stays where edges are detected. `setBrightness` calls move into the same block. Read the surrounding code first — preserve the flash-on-edge semantics exactly; only the decay/publish rate changes (≤1.3 ms light latency).
- [ ] **Step 3:** Verify Particules' FREEZE light (`Particules.cpp:477`) — if it's a plain `setBrightness(state)` per sample, move it under BlockReady too (L7).
- [ ] **Step 4:** `tests/run.sh` → green (particules dir + python guards). Both builds clean (the `#ifdef METAMODULE` arm only compiles in the MM build — the MM cmake build MUST be run to compile-check it).
- [ ] **Step 5:** Commit: `git add -u src/particules/Particules.cpp src/retours_delay/Retours.cpp && git commit -m "Particules/Retours: block-rate Quality reads and light updates"`

---

### Task 10: Retours EchoEngine per-sample rework

**Files:**
- Modify: `src/retours_delay/dsp/src/engine/echo_engine.cpp` + `.h`
- Test: `tests/retours_delay_dsp/test_echo_engine.cpp` (extend)

**Context:** Findings §5 H1, H2, M3, L4. `ReadWet` per sample: 1–3 `fmodf` via `WrapPosition`, 1–2 `/decimation` divides, `std::round` when multi-tap, frozen-seam `1/fade_len` divide + block-invariant seam-start Hermite read.

- [ ] **Step 1:** Extend `test_echo_engine.cpp` FIRST with a pinning test (against current code): drive the engine's public API through a deterministic scenario matrix — tape mode with a delay-time retarget mid-run, crossfade mode ditto, multi-tap on, frozen with a slice (cover the seam window), 4096 samples each with a known input pattern written per sample — and REQUIRE outputs match a stored FNV-1a hash per scenario (fill hashes from a first run, same technique as Task 7). These pin the wrap/advance changes as bit-exact. Run → PASS against current code.
- [ ] **Step 2:** Add the bounded wrapper next to `WrapPosition`:

```cpp
// Wrap for positions already within (-size, 2*size): one conditional each
// way, exactly equal to fmod there (Sterbenz: x-s exact for s<=x<=2s).
// ReadWet's positions are bounded by construction (read_subsample_ resyncs
// to write_head() < size every block and advances <= 64/decimation; delay
// <= size-1). The general WrapPosition stays for block-rate callers.
inline float WrapBounded(float p, float size_f) {
    if (p >= size_f) p -= size_f;
    else if (p < 0.f) p += size_f;
    return p;
}
```

Replace the `WrapPosition` calls inside `ReadWet` (tape read, crossfade old/new, multi-tap, frozen main + seam start) with `WrapBounded`. Keep `WrapPosition` for `NotifyFreeze`/`SetTargets`.
- [ ] **Step 3 (H2):** Add member `float inv_decimation_ = 1.f;` — set in `SetTargets` (which already reads `buf_->decimation_factor()`): `inv_decimation_ = 1.f / static_cast<float>(decimation);` (block-rate divide; exact for 1 and 2). In `ReadWet`, replace both `read_rate_scale_ / static_cast<float>(decimation)` sites with `read_rate_scale_ * inv_decimation_` and delete the per-sample `std::max`/`decimation` locals. Guard: if `ReadWet` can run before any `SetTargets` call, the default 1.f matches decimation 1 — verify `Configure`/init order and set it there too if needed.
- [ ] **Step 4 (M3):** Multi-tap: wrap first, then snap without libm:

```cpp
        float t2 = WrapBounded(write_pos_continuous - delay_used * kTap2Ratio, size_f);
        float tap2_pos = static_cast<float>(static_cast<int>(t2 + 0.5f));  // t2 >= 0
        if (tap2_pos >= size_f) tap2_pos -= size_f;   // snap can land exactly on size
```

(Differs from `round` only on exact-tie fractions — float-noise on a coarse texture tap.)
- [ ] **Step 5 (L4):** Frozen-seam hoists: in `NotifyFreeze`, precompute members `slice_fade_len_` (= `std::min(kSeamCrossfadeFrames, slice_len_frames_ * 0.5f)`), `inv_slice_fade_len_`, and the wrapped `slice_start_pos_`; in the frozen branch use them (the `1/fade_len` divide and one `WrapBounded` disappear; the seam-start Hermite read stays per-sample — its position is fixed but the buffer content changes only when unfrozen, so it MAY be hoisted only if `NotifyFreeze` guarantees content is static while frozen — read `recording_buffer` freeze semantics; if writes continue while frozen, keep the per-sample read).
- [ ] **Step 6:** Run `tests/retours_delay_dsp/run.sh` — the pinning hashes must be UNCHANGED for steps 2–3 scenarios (exact) ; the multi-tap scenario hash MAY change only if an exact-tie occurred (regenerate that one hash if so, with a comment). All other echo/freeze tests green.
- [ ] **Step 7:** Commit: `git add -u src/retours_delay tests/retours_delay_dsp && git commit -m "Retours: bounded wraps, reciprocal decimation, libm-free tap snap"`

---

### Task 11: Retours block-rate polish

**Files:**
- Modify: `src/retours_delay/dsp/src/retours_processor.cpp` (+ `.h` if members needed), `src/retours_delay/dsp/src/engine/echo_engine.cpp`, `src/retours_delay/dsp/src/mod/slow_random_lfo.h`, `src/retours_delay/Retours.cpp`

**Context:** Findings §5 M5–M7, L1, L2.

- [ ] **Step 1 (M5):** In `retours_processor.cpp` `:222` area, cache the input gain: members `float cached_trim_db_ = 1e9f, cached_input_gain_ = 1.f;` — recompute via `DbToGain` only when `input_trim_db` changes. (Local cache; do NOT modify the shared `DbToGain`.)
- [ ] **Step 2 (M6):** In `echo_engine.cpp` `SetTargets`, cache the slew coefficient: recompute `slew_coeff_` only when `(slew_s, sample_rate_)` differ from cached copies.
- [ ] **Step 3 (M7):** In `Retours.cpp`, hoist the two per-sample `isConnected()` calls (`OUT_R` `:338`, `IN_R` `:343`) into bools refreshed in the `BlockReady()` branch (members, e.g. `out_r_connected_`). ≤64-sample cable-detection latency; the consumers are already block-tolerant.
- [ ] **Step 4 (L1):** In `retours_processor.cpp` block-rate code: `x / 5.f` → `x * 0.2f` (5 sites `:197,214,227,246,259`) and `pitch_semi_eff / 12.f` → `* (1.f/12.f)` (`:241`).
- [ ] **Step 5 (L2):** `slow_random_lfo.h`: precompute the phase increment in `SetRate` (store `inc_per_block_ = rate_hz_ / sr_`; multiply by `n` in `Next`). In `retours_processor.cpp` `:191-193`, gate the three `SetRate` calls on actual change (they pass constants every block).
- [ ] **Step 6:** `tests/retours_delay_dsp/run.sh` → green. Task 10's pinning hashes still pass (these are block-rate math changes producing identical values — trim gain `powf(10,0)==1.f` exactly; slew coeff identical when inputs unchanged; reciprocal swaps are the only float-noise and they feed block-rate targets — if a hash moves, investigate before accepting). Commit: `git add -u src/retours_delay && git commit -m "Retours: cache block-rate conversions, hoist jack checks, reciprocal constants"`

---

### Task 12: Particules grain hot loop

**Files:**
- Modify: `src/particules/dsp/src/grain/grain.h`, `src/particules/dsp/src/buffer/recording_buffer.h`, `src/particules/dsp/src/grain/grain_engine.cpp` (call site)
- Test: `tests/particules_dsp/test_grain.cpp` (extend)

**Context:** Findings §4 H2, H3. `ReadHermiteStereoFrac` re-reads `format_`/`channels_`/LUT and switches per grain-sample; `ProcessBlock` runs 2 `isfinite` per grain-sample and keeps calling `Process` after the grain dies.

- [ ] **Step 1:** Read `recording_buffer.h:77-149` and `grain_engine.cpp:314-326` fully. Add to `RecordingBuffer` a resolved-read context:

```cpp
    struct ReadContext {          // block-lifetime only: pointers invalid after writes/config changes
        const RecordingBuffer* buf;
        // resolved once: format, channels, base data pointer(s), mu-law LUT
        // (copy the exact fields the per-sample switch currently re-derives)
    };
    ReadContext MakeReadContext() const;   // resolves format_/channels_/LUT once
    void ReadHermiteStereoFrac(const ReadContext& ctx, size_t i0, float frac,
                               float* l, float* r) const;  // same math, switch hoisted
```

Implement the context overload by restructuring the existing function: the format/channel `switch` moves OUTSIDE into `MakeReadContext` (or stays as a switch on a context enum but with the pointers/LUT pre-resolved so nothing is re-loaded per sample — measure by eye: the goal is that the inner call is straight-line loads+math for the common float32/int12/mulaw stereo cases). The EXISTING signature stays and delegates (VCV desktop + other callers unchanged).
- [ ] **Step 2:** `Grain::ProcessBlock` gains a `const RecordingBuffer::ReadContext&` parameter; `Grain::Process` likewise; `GrainEngine`'s render loop builds the context ONCE per block before iterating grains. Fold in H3 while editing the loop:

```cpp
        for (size_t i = 0; i < num_frames; ++i) {
            float gl = 0.0f, gr = 0.0f;
            if (!Process(ctx, buf_size_q, &gl, &gr)) {
                if (!active_) break;   // dead grains never reactivate mid-block
                // inactive-but-returning-false cases (pre-delay) continue
            }
            if (!std::isfinite(gl + gr)) {   // one check: NaN/Inf in either channel poisons the sum
                active_ = false;
                return;
            }
            output[i].l += gl;
            output[i].r += gr;
        }
```

**Read `Grain::Process`'s return-value semantics first** (pre-delay returns? kill returns?) and preserve them exactly — `break` ONLY when `active_` is false. Note the combined check: `finite+finite` can only be non-finite via overflow at \~3.4e38, unreachable for grain outputs (envelope·gain·pan ≤ a few units); document with a comment.
- [ ] **Step 3:** Extend `test_grain.cpp`: render a grain over a buffer through both the old single-call path and the context path, REQUIRE bit-identical output for each format (float32 / int12 / mulaw — see how existing tests construct buffers per format).
- [ ] **Step 4:** `tests/particules_dsp/run.sh` → all green (grain, NaN-robustness, quality suites all exercise this). `tests/run.sh` too (wrapper-level). Commit: `git add -u src/particules tests/particules_dsp && git commit -m "Particules: block-resolved grain reads, single finite check, dead-grain break"`

---

### Task 13: Particules spawn/block-rate items

**Files:**
- Modify: `src/particules/dsp/src/pitch/pitch_quantizer.cpp` (+ `.h`), `src/particules/dsp/src/grain/grain_engine.cpp`, `src/particules/dsp/src/grain/grain.cpp`, `src/particules/dsp/src/buffer/sample_codec.h`, `src/particules/dsp/include/particules_dsp/parameters.h`

**Context:** Findings §4 M3–M6, H4, L6.

- [ ] **Step 1 (H4, shared with Retours M2):** `sample_codec.h` `Int12Encode`: `std::lround(x * 2047.0f)` → `float s = x * 2047.0f; int v = (int)(s >= 0.f ? s + 0.5f : s - 0.5f);` (round-half-away-from-zero, matching `lround` exactly including ties — no libm). Keep the existing clamp/NaN guard order intact.
- [ ] **Step 2 (M3):** `pitch_quantizer.cpp`: eliminate the double-precision pow/log round trip. The input is already log2-domain: compute `k = std::floor(pitch / log2_period_)` (block... spawn-rate `floorf` is fine), `reduced = pitch - k * log2_period_` (float), scan the ratio table against `exp2f(reduced)` (or precompute the table in log2 domain at `loadRatios` time and scan `reduced` directly — pick whichever keeps the existing nearest-step semantics; the log2-domain scan is cleaner and monotone-equivalent since log2 is monotonic), and reconstruct the output from the chosen step + `k` octaves. Cache `log2_period_`-derived constants at `loadRatios`. **The suite has `test_pitch_quantizer.cpp` — it must stay green unmodified**; if any assertion fails by float-noise, inspect whether it pins exact double behavior and relax ONLY with justification in a comment.
- [ ] **Step 3 (M4):** `grain_engine.cpp:334-335`: cache the overlap coefficient keyed on `(slope_coeff, num_frames)` (two floats + result as members or function-local statics per the file's style — members on the engine).
- [ ] **Step 4 (M5):** same file: `/(1.0f - kSizeBoundary)` and `/(kSizeBoundary + 1.0f)` → precomputed `constexpr` reciprocals; cache the `log2f(max_duration/kMin...)` argument next to `cached_decimation_` per findings; `* df_f / sample_rate_` and `/ df_f` → cached `inv_sample_rate_` (set where sample rate is set) and `inv_df_` (set where `cached_decimation_` updates); `SemitonesToRatio(mod_pitch)` at `:158` → `SemitonesToRatioFast` from Task 1 (spawn-rate; \~1.5e-5 rel pitch error = micro-cents, consistent with the Ondes decision).
- [ ] **Step 5 (M6):** `grain.cpp:101-102`: pan gains via the existing table: `pan_l_ = CosLookup(p * 0.25f); pan_r_ = CosLookup(p * 0.25f - 0.25f);` — verify against the current `cos(p·π/2)`/`sin(p·π/2)` convention by checking `CosLookup`'s phase convention in `cosine_table.h` (cos(2π·x)); sin(θ) = cos(θ − π/2) → `CosLookup(p*0.25f - 0.25f)`. Confirm endpoints: p=0 → (1, 0); p=1 → (0, 1).
- [ ] **Step 6 (L6):** `parameters.h:64-79` `QuantizePitchLock`: `/12.f` → `* (1.f/12.f)`; keep `std::round`/`std::floor` (spawn-rate, sign-varying — not worth branch tricks).
- [ ] **Step 7:** `tests/particules_dsp/run.sh` AND `tests/retours_delay_dsp/run.sh` (codec is shared) → green. Task 10 pinning hashes: Int12 tie behavior is exact, so unchanged. Commit: `git add -u src/particules && git commit -m "Particules: spawn-rate reciprocals, float pitch quantizer, table pan, libm-free int12"`

---

### Task 14: Shared saturation divide folds

**Files:**
- Modify: `src/particules/dsp/src/fx/saturation.cpp` (+ `.h` if constants move), `src/particules/dsp/src/input/auto_gain.cpp`
- Test: `tests/particules_dsp/test_saturation_curves.cpp` (existing, must stay green)

**Context:** Findings §4 M1/M7, §5 M1. `NormalizedSoftClip(x, drive) = SoftClip(x*drive)/drive` = two divides; fold to one: for `|x*drive| <= 3`, `y=x*drive` → `y*(27+y²)/((27+9y²)*drive)`; for `|y| > 3` the clip is ±1 → `±1/drive` → multiply by a precomputed reciprocal. All call-site `drive` values are compile-time constants → add reciprocal-constant variants. Same for `AsymmetricSoftClip`'s `kTapeBiasAsymmetry` and auto_gain's `/kHeadroom` (0.2 → `* 5.0f`). `FastTanh`'s own data-dependent divide STAYS. Float-noise class; feedback paths make free-running A/B meaningless — the behavioral suites are the gate.

- [ ] **Step 1:** Read `saturation.cpp` fully and list every call site of `NormalizedSoftClip`/`AsymmetricSoftClip`/`SoftLimit` with its drive constant (grep both dsp trees). Implement the folds; precompute reciprocals as `constexpr`.
- [ ] **Step 2:** Run `tests/particules_dsp/run.sh` (saturation curves, tape voicing, feedback path, quality modes) AND `tests/retours_delay_dsp/run.sh` (quality degradation/modes) → all green. If a curve test fails by ≤1e-5, the test pins exact rounding — check the findings' algebra before touching either.
- [ ] **Step 3:** Task 10's Retours pinning hashes WILL change for lo-fi-mode scenarios (feedback + float-noise). Regenerate them in this commit with a comment citing this task. Tape/crossfade Bright-mode hashes must NOT change (Bright uses HardClip — no divides touched).
- [ ] **Step 4:** Commit: `git add -u src/particules src/retours_delay tests/retours_delay_dsp && git commit -m "Shared saturation: single-divide folds and reciprocal drive constants"`

---

### Task 15: Particules reverb/fade polish

**Files:**
- Modify: `src/particules/dsp/src/fx/reverb.cpp` (+ `.h`), `src/particules/dsp/src/particules_processor.cpp`

**Context:** Findings §4 L1, L2.

- [ ] **Step 1 (L2):** `reverb.cpp:132-142`: `fb`, `lp_coeff`, `ap_coeff` derive only from `decay_`/`lp_`/`diffusion_` — move the derivations into `SetDecay`/`SetLpCutoff`/`SetDiffusion` (members `fb_`, `lp_coeff_`, `ap_coeff_`), keeping formulas byte-identical. Check `Init`/`Reset` seed them.
- [ ] **Step 2 (L1):** `particules_processor.cpp:260-261, 292-293`: `/ (float)Impl::kQualityFadeSamples` → multiply by a `constexpr` reciprocal (`1.f/2048.f` is exact — power of two). Same for the `counter / kModeXfadeSamples` at `quality_processor.h:122` (1/64, exact).
- [ ] **Step 3:** `tests/particules_dsp/run.sh` (reverb + quality-transition tests) → green. Commit: `git add -u src/particules && git commit -m "Particules: hoist reverb coefficients, exact fade reciprocals"`

---

### Task 16: Full verification gate + doc update

**Files:**
- Modify: `cpu-optimization-other-modules-2026-07-24.md` (add "Implemented" section)

- [ ] **Step 1:** Run ALL lanes: `tests/run.sh`, `tests/particules_dsp/run.sh`, `tests/retours_delay_dsp/run.sh` → exit 0 each.
- [ ] **Step 2:** Build both targets per `build-robotboy-plugin` skill: VCV `make -C vcv -B` clean; MetaModule cmake build clean with `All symbols found!` and `.mmplugin` produced.
- [ ] **Step 3:** A7 static op counts (filter-pass §8 methodology: `arm-none-eabi-g++` with the SDK release flags from `metamodule-plugin-sdk/cmake/arch_mp15xa7.cmake`, `objdump -d`, count `vdiv`/`bl <libm>` per symbol) for before (main) vs after (branch): `LoopEngine::process` (whole engine), `WavetableOscillator::Process`, `EchoEngine::ReadWet`, `Grain::ProcessBlock`. Record the table in the findings doc's new §"Implemented" along with the per-task equivalence notes and any regenerated-hash rationale.
- [ ] **Step 4:** Commit: `git add cpu-optimization-other-modules-2026-07-24.md && git commit -m "Record implemented loopers/Beads-family optimizations and A7 measurements"`
- [ ] **Step 5:** Report: summary of measurements, the user-run hardware checklist (load Loooop/Ondes/Retours/Particules patches on MetaModule; listen for looper seam behavior, Ondes FM pitch, Retours lo-fi modes, Particules grain texture), and the merge decision (leave on `cpu-opt-2` for user).

## Self-Review Notes

- Findings coverage: §2 H1–H5, M1–M5, desktop Lows → Tasks 3–8; §3 all → Tasks 1–2; §4 H1–H4, M1, M3–M7, L1–L2, L6–L7 → Tasks 9, 12–15; §5 H1–H4, M1–M3, M5–M7, L1–L2, L4 → Tasks 9–11, 14. Dropped by user decision: §4 M2 / §5 M4 (SVF skip), §2 M4 (float lerp). Deferred as not-worth-it: §5 L3/L5 (block-rate trig in tape wobble, repeat-envelope edge — a few cycles/sample equivalent), §2 desktop `std::sin` LED (Lock mode only), Ondes display L1 (GUI thread).
- Type consistency: `windowBoundsCached(h, headIdx, flavor, jitterOff, ws, wl)` used in Tasks 5 and referenced in 7-adjacent code; `ReadContext`/`MakeReadContext` names used consistently in Task 12; `Exp2Fast`/`SemitonesToRatioFast` produced in Task 1, consumed in Tasks 2 and 13.
- Ordering: pinning tests (7, 10) are written against pre-change code within their own task; Task 14 deliberately follows 10 and regenerates only lo-fi hashes.
