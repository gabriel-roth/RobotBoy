# Vespid / Onbetap MetaModule CPU Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove \~12 of \~17 executed float divides + both `sinhf` calls per sample from Vespid's hot path and \~7 of \~14 from Onbetap's, per the verified findings in `cpu-optimization-2026-07-24.md` (§3, §4, §9).

**Architecture:** All changes are local algebraic rewrites of the two filter cores plus small module-level fixes — no behavioral redesign. Fidelity budget verified in advance (doc §9.2): per-step differences are at float-noise level (Vespid ≤ 4.8e-6 V at normal levels, Onbetap ≤ 1.9e-6 cu). Working prototypes exist in the session scratchpad (`opt3/vespid/WaspFilter.hpp`, `opt/onbetap/OnbetapFilter.hpp`, `fastsinh.hpp`) — tasks below transplant them.

**Tech Stack:** C++20 headers, tests/run.sh harness (auto-builds `tests/*/test_*.cpp`), arm-none-eabi-g++ 12.3 for A7 op counts, VCV `make -C vcv` + MetaModule cmake builds.

## Global Constraints

- Branch: `cpu-opt` off `main`. Commit only files this plan touches (working tree has unrelated dirty `.md` files — leave them).
- Commit messages: one short sentence (≤ 15 words), no AI attribution.
- The user-approved §7 decisions: §7.1 = `sinhFast` in the standard-accuracy path on **both** platforms, `std::sinh`/`cosh` stay in the desktop-only Newton block; §7.2 = skip Onbetap's HP tap **only at oversample == 1** when no crossfade endpoint needs it.
- Equivalence testing must use per-step resync or the behavioral suites — never free-running waveform diff (doc §9.2: chaos).
- Onbetap `processG` keeps a `needHp = true` default so existing tests compile unchanged.
- `DecimFir9`/`DecimFir13` rework must keep the exact MAC accumulation order (h[0]·newest → h[N−1]·oldest) so outputs stay bit-identical (test_engine.cpp pins the impulse response).
- MetaModule sim / hardware listening checks are user-run items, not agent tasks.

---

### Task 1: Branch, `fastsinh.hpp`, accuracy test

**Files:**
- Create: `src/vespid/fastsinh.hpp`
- Create: `tests/vespid/test_fastsinh.cpp` (auto-built by tests/run.sh)

**Interfaces:**
- Produces: `wasp::exp2Fast(float)`, `wasp::sinhFast(float)` — used by Task 3.

- [ ] **Step 1: Create branch**

```bash
git -C ~/Dev/RobotBoy checkout -b cpu-opt
```

- [ ] **Step 2: Write `src/vespid/fastsinh.hpp`** — the §9.4 code, in `namespace wasp`, with provenance comment pointing at the doc.

- [ ] **Step 3: Write `tests/vespid/test_fastsinh.cpp`** asserting max relative error vs `std::sinh` (double) < 2e-5 over [-30, 30] at 1e-4 steps, plus continuity across the |x| = 0.5 handoff (adjacent samples within 1e-5 rel) and exact-zero at x = 0.

- [ ] **Step 4: Run the test standalone**

```bash
cd ~/Dev/RobotBoy/tests && g++ -std=c++20 -O2 -I../src -o ../build/tests/test_fastsinh vespid/test_fastsinh.cpp && ../build/tests/test_fastsinh
```
Expected: PASS (measured max rel err 1.117e-5).

- [ ] **Step 5: Commit** — `git add src/vespid/fastsinh.hpp tests/vespid/test_fastsinh.cpp && git commit -m "Vespid: add divide-free fast sinh with accuracy test"`

### Task 2: `railClamp` reciprocal

**Files:**
- Modify: `src/vespid/wasp_dsp_utils.hpp:166-172`

- [ ] **Step 1: Edit railClamp** — add `constexpr float invW = 1.f / w;` and replace both `/w` with `*invW` (prototype: scratchpad `opt/vespid/wasp_dsp_utils.hpp`).

- [ ] **Step 2: Run vespid utils test** — `tests/run.sh` builds it; quick single build like Task 1 Step 4 for `vespid/test_wasp_utils.cpp`. Expected: PASS.

- [ ] **Step 3: Commit** — `Vespid: replace railClamp knee divides with reciprocal multiply`

### Task 3: `WaspFilter.hpp` core rework (§3.1–3.4, §3.5 Newton guard, §7.1)

**Files:**
- Modify: `src/vespid/WaspFilter.hpp`

**Interfaces:**
- `ModeConfig` gains trailing fields `invNInv`, `a0OverVHi`, `a0OverVLo` (constexpr-arithmetic in `kGerman`/`kBritish` initializers). Only aggregate initializers in this header construct one (verified §3.4).
- `process()` signature unchanged. New private members `th`, `tb`, `diodePrev`; new `kInvVD`.

Transplant from scratchpad prototype `opt3/vespid/WaspFilter.hpp`, with these deltas from the prototype:
- keep `namespace wasp` and the original file's full comment headers;
- `#include "fastsinh.hpp"`; commit block uses `wasp::sinhFast(dArg)` (§7.1 approved);
- Newton block wrapped in `#if !defined(METAMODULE)`, keeps `std::sinh`/`std::cosh` and un-fused `finv`/`finvSlope`;
- `setMode()` recomputes pivots on actual change (doc §9.3 recommendation):

```cpp
void setMode(const ModeConfig& m) {
    if (mode == &m) return;
    mode = &m;
    // Cached secant pivots were computed with the old mode's c; refresh
    // them so the first post-switch sample pivots consistently.
    th = tanhXdX(m.c * hpPrev);
    tb = tanhXdX(m.c * bpPrev);
}
```
- `reset()` seeds `th = tb = 1.f; diodePrev = 0.f;`.

- [ ] **Step 1: Apply the rework** (per prototype + deltas above).
- [ ] **Step 2: Per-step equivalence harness** — pristine HEAD headers via `git show HEAD:src/vespid/*.hpp` into scratchpad `orig/vespid/` with `namespace wasp` → `waspref` sed; adapt `test_step_equiv.cpp` to compare `waspref` (free-running) vs new `wasp` (resynced per step; recompute `th`/`tb`/`diodePrev` from pre-state using the new code's own functions). Expected: per-step output diff ≤ \~5e-6 V at 0.5 V amp, ≤ \~1e-4 V at 50 V, matching doc §9.2 (fast-sinh column).
- [ ] **Step 3: Run vespid suite** — build+run `tests/vespid/test_wasp_filter.cpp` and `test_wasp_utils.cpp`. Expected: all PASS (golden comparisons run highAcc, which is untouched on desktop; test 9 checks standard-vs-high < 0.5 dB, our per-step error is orders below).
- [ ] **Step 4: Commit** — `Vespid: cache secant pivots, fuse inverter eval, reciprocals, fast sinh`

### Task 4: `Vespid.cpp` module-level (§3.5)

**Files:**
- Modify: `src/Vespid.cpp:299-361`

- [ ] **Step 1: Hoist R-connected check** — `processChannel(int c, float m)` → `processChannel(int c, float m, bool rConnected)`; compute `bool rConnected = inputs[AUDIO_INPUT_R].isConnected();` once in `process()` before the voice loop (mirrors `Onbetap.cpp:318`).
- [ ] **Step 2: Guard `setChannels`** —

```cpp
if (outputs[LP_OUTPUT].getChannels() != channels)
    for (int out = 0; out < NUM_OUTPUTS; out++)
        outputs[out].setChannels(channels);
```
- [ ] **Step 3: Build VCV** — `make -C vcv` (or the plugin's usual build) to prove it compiles.
- [ ] **Step 4: Commit** — `Vespid: hoist stereo check out of voice loop, gate setChannels`

### Task 5: `OnbetapFilter.hpp` (§4.1, §4.3, §7.2 core side)

**Files:**
- Modify: `src/onbetap/OnbetapFilter.hpp`

**Interfaces:**
- `satGain` closed forms (transplant from scratchpad `opt/onbetap/OnbetapFilter.hpp`), new constants `kInvAsymNeg`, `kInvSoftSpan`.
- `processG(float in, float g, float kEff, bool needHp = true)` — when `needHp` is false, `hp` is returned as 0.f and its `sat()` call skipped (state update untouched; exact at 1x, §4.6).

- [ ] **Step 1: Apply the rework** (satGain regions, `sat` reciprocal, `softLimitOne` reciprocal, `needHp` param).
- [ ] **Step 2: Per-step equivalence** — same harness pattern as Task 3 (rename pristine class to `OnbetapFilterRef`). Expected: ≤ \~2e-6 cu (doc §9.2).
- [ ] **Step 3: Run onbetap suite** — all five `tests/onbetap/test_*.cpp`. Expected: PASS (behavioral thresholds).
- [ ] **Step 4: Commit** — `Onbetap: single-divide satGain closed forms, reciprocals, optional HP tap`

### Task 6: `engine.hpp` decimation FIRs (§4.4, §4.5)

**Files:**
- Modify: `src/onbetap/engine.hpp:50-94`

**Interfaces:**
- `DecimFir13`/`DecimFir9` become ring buffers: `float push(float x)` (advance + MAC, same signature), new `void pushHistory(float x)` (advance only, for §4.4's discarded odd substeps). `reset()` also resets the head index. MAC order identical to the shift-register version:

```cpp
struct DecimFir9 {
    static constexpr float h[9] = { /* unchanged taps */ };
    float z[9] = {0.f};
    int head = 0;                      // index of most recent sample
    void reset() { for (auto& v : z) v = 0.f; head = 0; }
    void pushHistory(float x) { head = (head == 0) ? 8 : head - 1; z[head] = x; }
    float push(float x) {
        pushHistory(x);
        float y = 0.f;
        int i = 0;
        for (int idx = head; idx < 9; ++idx, ++i) y += h[i] * z[idx];
        for (int idx = 0; i < 9; ++idx, ++i)      y += h[i] * z[idx];
        return y;
    }
};
```
(DecimFir13 identical with 13/12.)

- [ ] **Step 1: Apply both conversions.**
- [ ] **Step 2: Run `tests/onbetap/test_engine.cpp`** — pins DC gain, impulse = taps, reset-clears. Expected: PASS bit-exactly.
- [ ] **Step 3: Commit** — `Onbetap: ring-buffer decimation FIRs with history-only push`

### Task 7: `Onbetap.cpp` module level (§4.2, §4.3, §4.4, §7.2 gate)

**Files:**
- Modify: `src/Onbetap.cpp:236-305` (processSide), `:307-349` (process)

- [ ] **Step 1: Dedicated 1x branch** replacing the generic fall-through loop:

```cpp
} else {
    // 1x: no resampling; xPrev still updated so a later oversampling
    // switch interpolates from the right sample.
    auto o = flt.processG(x1, g, kEff, needHp);
    lp = o.lp; bp = o.bp; hp = o.hp;
}
```
- [ ] **Step 2: §4.4 in the 4x loop** — odd substeps call `fir4*.pushHistory(o.*)`; even substeps keep `push`.
- [ ] **Step 3: needHp gate** — in `process()`, once per sample:

```cpp
auto usesHp = [](int m) { return m >= 2; };   // HP, notch, peak
bool needHp = oversample != 1 || usesHp(modeTarget)
              || (modeXf < 1.f && !vintageDrift && usesHp(modeCurrent));
```
pass down through `processSide` to `processG` (2x/4x always compute hp — conservative §7.2).
- [ ] **Step 4: §4.3** — `9.f * OnbetapFilter::tanhish(push * v * (1.f / 9.f))`.
- [ ] **Step 5: Run full onbetap suite + build VCV.** Expected: PASS, clean build.
- [ ] **Step 6: Commit** — `Onbetap: direct 1x path, gated HP tap, FIR history-only substeps`

### Task 8: Full verification + measurement + doc

- [ ] **Step 1: Full suite** — `tests/run.sh`. Expected: all module suites + Python guards PASS.
- [ ] **Step 2: A7 static op recount** — rebuild scratchpad `armprobe` against the reworked `src/` with `-DMETAMODULE`; record insns/vdiv/libm. Expected: Wasp \~446/12/0, Onbetap \~358/21/0 (±compiler noise).
- [ ] **Step 3: Host micro-benchmark** — confirm no host regression (doc §9.1 table).
- [ ] **Step 4: Builds** — VCV `make -C vcv` and MetaModule cmake build both clean (see build-robotboy-plugin skill).
- [ ] **Step 5: Update `cpu-optimization-2026-07-24.md`** — add §10 "Implemented" with final measured numbers and the user-run listening checklist (per project convention).
- [ ] **Step 6: Commit** — `Record implemented CPU optimization measurements`

### User-run checklist (post-merge, per project convention)

- [ ] MetaModule: load a Vespid + Onbetap patch, confirm CPU relief at 1x, listen for zipper/character changes (esp. German self-osc onset, Onbetap self-osc pitch, mode crossfades into HP/notch/peak).
- [ ] Desktop VCV: A/B against v-previous build at normal and max drive.
