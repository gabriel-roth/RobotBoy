# Onbetap Drive Grit (VCA Push) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the top of Onbetap's Drive knob getting dirtier (never smoother/softer) at high Q by pushing the signal harder into the existing output VCA as Drive rises.

**Architecture:** `onbetap::driveGains` gains a `gritDb` parameter and a third output `vcaPush = exp2(gritDb/6.0206·drive²)`; the module smooths it per voice (new `pushSlew`) and applies it inside the existing output VCA: `9·tanhish(push·v/9)`. A new "Drive grit" Tuning slider (0–12 dB, default `onbetap::kDefaultGritDb`) exposes it; 0 dB recovers today's behavior bit-exactly.

**Tech Stack:** Header-only C++20 DSP (`src/onbetap/`), host-free g++ test harness (`tests/run.sh`, no Rack SDK), VCV Rack SDK only for the final module build.

**Spec:** `docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md`

## Global Constraints

- Drive = 0 must stay **bit-identical** to the current build (`vcaPush(0) = 1` exactly).
- `gritDb = 0` must recover current behavior exactly (escape hatch).
- `vcaPush ≥ 1` always — a boost into the fixed 9 V ceiling, never a cut.
- Output bounded ≤ 9 V peak regardless of settings.
- Do not touch: core solver, resonance map, makeup formula, oversampling, mode taps, DC blocker.
- Commit messages: short (≤ \~15 words), **no AI attribution lines**.
- In Markdown docs, escape literal tildes as `\~`.
- Work on branch `onbetap-drive-grit` off `main`.

---

### Task 1: Grit law in `drive.hpp`

**Files:**
- Modify: `src/onbetap/drive.hpp`
- Modify: `tests/onbetap/test_drive_level.cpp:53` (call-site arity)
- Test: `tests/onbetap/test_drive_grit.cpp` (created here with law checks; behavioral checks added in Task 2)

**Interfaces:**
- Produces: `onbetap::driveGains(float drive, float driveDb, float headroom, float outDb, float gritDb) -> DriveGains{driveScale, makeup, vcaPush}` and `constexpr float onbetap::kDefaultGritDb` (initially `6.f`; Task 2 may recalibrate). All later tasks depend on these exact names.

- [ ] **Step 1: Create branch**

```bash
cd ~/Dev/RobotBoy && git checkout -b onbetap-drive-grit
```

- [ ] **Step 2: Write the failing law tests**

Create `tests/onbetap/test_drive_grit.cpp`:

```cpp
// tests/onbetap/test_drive_grit.cpp — Drive-grit VCA push (host-free).
//
// Guards the "top of the Drive knob keeps getting dirtier" promise: at high Q
// the (authentic) resonance choke used to take the resonance-derived grit and
// ~1 dB of level with it, so Drive got smoother/softer past ~90% of travel.
// The fix pushes the signal harder into the existing output VCA as Drive
// rises: out = 9*tanhish(push*v/9), push = exp2(gritDb/6.0206 * drive^2).
// See docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md.
//
// Run with any argument (e.g. "./test_drive_grit sweep") to print the
// calibration sweep table instead of asserting.
#include "onbetap/OnbetapFilter.hpp"
#include "onbetap/engine.hpp"
#include "onbetap/drive.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

int main(int argc, char**) {
    (void)argc;
    // --- Law unit checks (pure driveGains math) ---
    CHECK(onbetap::driveGains(0.f, 30.f, 1.f, 0.f, 12.f).vcaPush == 1.f,
          "push == 1 exactly at Drive 0 (bit-identity)");
    CHECK(onbetap::driveGains(0.5f, 30.f, 1.f, 0.f, 0.f).vcaPush == 1.f,
          "gritDb 0 -> push == 1 (escape hatch)");
    float p1 = onbetap::driveGains(1.f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(std::fabs(p1 - std::exp2(6.f / 6.0206f)) < 1e-4f,
          "push(drive 1, 6 dB) == exp2(6/6.0206)");
    float pa = onbetap::driveGains(0.25f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pb = onbetap::driveGains(0.50f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pc = onbetap::driveGains(0.75f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(1.f < pa && pa < pb && pb < pc && pc < p1,
          "push monotone increasing in drive");
    auto ga = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 0.f);
    auto gb = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 12.f);
    CHECK(ga.driveScale == gb.driveScale && ga.makeup == gb.makeup,
          "gritDb does not touch driveScale/makeup");

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
```

- [ ] **Step 3: Run to verify it fails**

```bash
cd ~/Dev/RobotBoy/tests && mkdir -p ../build/tests && \
g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
    -I../src/particules/dsp/include -o ../build/tests/test_drive_grit \
    onbetap/test_drive_grit.cpp && ../build/tests/test_drive_grit
```

Expected: **compile error** — `driveGains` takes 4 args and `DriveGains` has no member `vcaPush`.

- [ ] **Step 4: Implement the law in `drive.hpp`**

In `src/onbetap/drive.hpp`, replace the `DriveGains` struct and `driveGains` function with:

```cpp
constexpr float kDefaultGritDb = 6.f;  // Drive-grit VCA push at full Drive (dB);
                                       // calibrated, see 2026-07-18 grit spec

struct DriveGains {
    float driveScale;  // input gain into the core
    float makeup;      // output buffer gain (constant, Drive-independent)
    float vcaPush;     // Drive-grit push into the output VCA (always >= 1)
};

// drive:    knob [0,1] (+ CV, already applied/clamped by the caller)
// driveDb:  drive span in dB (menu "Drive span", default 30)
// headroom: input-scale trim (menu "Core headroom", default 1)
// outDb:    output trim in dB (menu "Output trim", default 0)
// gritDb:   VCA push at full Drive in dB (menu "Drive grit", default 6)
inline DriveGains driveGains(float drive, float driveDb,
                             float headroom, float outDb, float gritDb) {
    float spanOct    = driveDb / 6.0206f;                 // dB → octaves
    float driveGain  = std::exp2(-2.f + spanOct * drive); // 0.25 → …
    float driveScale = driveGain * kBaseTrim * kVoltsToCore * headroom;
    float makeup     = kOutScale * std::exp2(outDb / 6.0206f);
    float vcaPush    = std::exp2(gritDb / 6.0206f * drive * drive);
    return { driveScale, makeup, vcaPush };
}
```

Append to the file-header comment block (after the sentence ending "…dropping level AND stripping grit as Drive rises."):

```
 * The one deliberate Drive-dependent output element is vcaPush: a bounded
 * BOOST (never a cut) into the fixed 9 V output-VCA ceiling, quadratic in
 * drive, so the top of the knob keeps gaining grit while the (authentic)
 * resonance choke removes the resonance-derived grit. gritDb = 0 disables it.
 * See docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md.
```

- [ ] **Step 5: Update the existing call site**

In `tests/onbetap/test_drive_level.cpp:53`:

```cpp
    // gritDb 0: this test guards the constant-makeup baseline (Findings 1-2)
    auto gains = onbetap::driveGains(drive, 30.f, 1.f, 0.f, 0.f);
```

(`src/Onbetap.cpp:184` also calls `driveGains` — it is updated in Task 3; it will not compile between Tasks 1 and 3, which is fine because nothing builds the module until Task 3's build step.)

- [ ] **Step 6: Run both tests to verify green**

```bash
cd ~/Dev/RobotBoy/tests && \
g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
    -I../src/particules/dsp/include -o ../build/tests/test_drive_grit \
    onbetap/test_drive_grit.cpp && ../build/tests/test_drive_grit && \
g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
    -I../src/particules/dsp/include -o ../build/tests/test_drive_level \
    onbetap/test_drive_level.cpp && ../build/tests/test_drive_level
```

Expected: `5 passed, 0 failed` (grit) and `9 passed, 0 failed` (level).

- [ ] **Step 7: Commit**

```bash
git add src/onbetap/drive.hpp tests/onbetap/test_drive_grit.cpp tests/onbetap/test_drive_level.cpp
git commit -m "Onbetap: add Drive-grit vcaPush law to driveGains"
```

---

### Task 2: Behavioral guards + calibration

**Files:**
- Modify: `tests/onbetap/test_drive_grit.cpp` (add harness, sweep mode, criteria asserts)
- Possibly modify: `src/onbetap/drive.hpp` (recalibrate `kDefaultGritDb` / steepen law — only if the sweep says so)

**Interfaces:**
- Consumes: `onbetap::driveGains(drive, driveDb, headroom, outDb, gritDb)`, `onbetap::kDefaultGritDb` (Task 1).
- Produces: the final calibrated `kDefaultGritDb` value and the measured sweep table (recorded in the Task 4 worklog entry).

- [ ] **Step 1: Add the measurement harness and criteria asserts**

Rewrite `tests/onbetap/test_drive_grit.cpp` as follows (law checks kept, harness added — this is the complete final file):

```cpp
// tests/onbetap/test_drive_grit.cpp — Drive-grit VCA push (host-free).
//
// Guards the "top of the Drive knob keeps getting dirtier" promise: at high Q
// the (authentic) resonance choke used to take the resonance-derived grit and
// ~1 dB of level with it, so Drive got smoother/softer past ~90% of travel.
// The fix pushes the signal harder into the existing output VCA as Drive
// rises: out = 9*tanhish(push*v/9), push = exp2(gritDb/6.0206 * drive^2).
// See docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md.
//
// Mirrors Onbetap.cpp::processSide for the 2x/LP path (gains from the real
// onbetap::driveGains). Run with any argument to print the calibration sweep
// table instead of asserting.
#include "onbetap/OnbetapFilter.hpp"
#include "onbetap/engine.hpp"
#include "onbetap/drive.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

static constexpr float kTwoPi = 6.28318530717959f;

// One host sample through the 2x oversampled LP path (mirrors processSide,
// including the Drive-grit VCA push).
static float sideLP(OnbetapFilter& f, float& xPrev, DCBlock& dc, float inVolts,
                    float g, float kEff, float driveScale, float makeup,
                    float push, DecimFir13& firLp, DecimFir13& firBp,
                    DecimFir13& firHp, float dcCoef) {
    float lp = 0, bp = 0, hp = 0;
    float x1 = inVolts * driveScale;
    for (int i = 1; i <= 2; i++) {
        float t = (float)i / 2.f;
        float x = xPrev + (x1 - xPrev) * t;
        auto o = f.processG(x, g, kEff);
        float fl = firLp.push(o.lp), fb = firBp.push(o.bp), fh = firHp.push(o.hp);
        if (i == 2) { lp = fl; bp = fb; hp = fh; }
    }
    xPrev = x1;
    float v = -lp * makeup;                              // LP tap
    v = dc.process(v, dcCoef);
    return 9.f * OnbetapFilter::tanhish(push * v / 9.f); // output VCA + push
}

struct Meas { float outDb, thdPct, peak; };

// Steady-state output level (dB re 1 V), THD, and peak for one Drive point.
static Meas measure(float cutoff, float res, float drive, float amp, float fs,
                    float gritDb) {
    float fsOs = fs * 2.f, tone = cutoff;
    float g = OnbetapFilter::cutoffToG(cutoff, fsOs);
    float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f);   // res→k (tuneOnset 0)
    auto gains = onbetap::driveGains(drive, 30.f, 1.f, 0.f, gritDb);
    float kCLag = 0.25f;
    float kEff = std::max(k - kCLag * g * g / (1.f + g * g), -0.31f);

    OnbetapFilter f; f.setLimit(OnbetapFilter::Limit::Hard);
    f.setMismatch(0, 0); f.setOffset(0); f.reset();
    DCBlock dc; DecimFir13 fLp, fBp, fHp; float xPrev = 0.f;
    float dcCoef = 1.f - kTwoPi * 1.6f / fs;

    int warm = (int)(fs * 1.0f);
    int spc = (int)std::lround(fs / tone), W = spc * 256;   // coherent window
    std::vector<float> buf(W);
    float peak = 0.f;
    for (int n = 0; n < warm + W; n++) {
        float dither = (n & 1) ? 1e-9f : -1e-9f;
        float in = amp * std::sin(kTwoPi * tone * n / fs) + dither;
        float out = sideLP(f, xPrev, dc, in, g, kEff, gains.driveScale,
                           gains.makeup, gains.vcaPush, fLp, fBp, fHp, dcCoef);
        if (n >= warm) {
            buf[n - warm] = out;
            peak = std::max(peak, std::fabs(out));
        }
    }
    auto ampAt = [&](int h) {
        double re = 0, im = 0, w = kTwoPi * (double)h * tone / fs;
        for (int n = 0; n < W; n++) { re += buf[n] * std::cos(w * n); im += buf[n] * std::sin(w * n); }
        return 2.0 * std::sqrt(re * re + im * im) / W;
    };
    double fund = ampAt(1), hs = 0;
    for (int h = 2; h <= 12; h++) { double a = ampAt(h); hs += a * a; }
    double tot = 0; for (int n = 0; n < W; n++) tot += (double)buf[n] * buf[n];
    float rms = (float)std::sqrt(tot / W);
    return { 20.f * std::log10(rms + 1e-20f),
             (float)(std::sqrt(hs) / (fund + 1e-20)) * 100.f,
             peak };
}

int main(int argc, char**) {
    const float fs = 48000.f, cutoff = 750.f, amp = 5.f;
    const float dflt = onbetap::kDefaultGritDb;
    char nm[128];

    // --- Law unit checks (pure driveGains math) ---
    CHECK(onbetap::driveGains(0.f, 30.f, 1.f, 0.f, 12.f).vcaPush == 1.f,
          "push == 1 exactly at Drive 0 (bit-identity)");
    CHECK(onbetap::driveGains(0.5f, 30.f, 1.f, 0.f, 0.f).vcaPush == 1.f,
          "gritDb 0 -> push == 1 (escape hatch)");
    float p1 = onbetap::driveGains(1.f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(std::fabs(p1 - std::exp2(6.f / 6.0206f)) < 1e-4f,
          "push(drive 1, 6 dB) == exp2(6/6.0206)");
    float pa = onbetap::driveGains(0.25f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pb = onbetap::driveGains(0.50f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pc = onbetap::driveGains(0.75f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(1.f < pa && pa < pb && pb < pc && pc < p1,
          "push monotone increasing in drive");
    auto ga = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 0.f);
    auto gb = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 12.f);
    CHECK(ga.driveScale == gb.driveScale && ga.makeup == gb.makeup,
          "gritDb does not touch driveScale/makeup");

    // --- Calibration sweep mode (no asserts) ---
    if (argc > 1) {
        printf("gritDb | r.70 thd@.9 thd@1 lvl@.9 lvl@1 | r.30 thd@.5 | r.50 thd@.5\n");
        for (float gdb : { 0.f, 4.f, 6.f, 8.f, 10.f, 12.f }) {
            Meas a = measure(cutoff, 0.70f, 0.9f, amp, fs, gdb);
            Meas b = measure(cutoff, 0.70f, 1.0f, amp, fs, gdb);
            Meas c = measure(cutoff, 0.30f, 0.5f, amp, fs, gdb);
            Meas d = measure(cutoff, 0.50f, 0.5f, amp, fs, gdb);
            printf("%6.1f | %10.1f %6.1f %6.1f %5.1f | %11.1f | %11.1f\n",
                   gdb, a.thdPct, b.thdPct, a.outDb, b.outDb, c.thdPct, d.thdPct);
        }
        return 0;
    }

    // --- Criterion 1: top-of-knob recovery at the self-osc knee ---
    Meas k9  = measure(cutoff, 0.70f, 0.9f, amp, fs, dflt);
    Meas k10 = measure(cutoff, 0.70f, 1.0f, amp, fs, dflt);
    snprintf(nm, sizeof nm, "res 0.70: THD@1.0 (%.1f%%) >= 12%%", k10.thdPct);
    CHECK(k10.thdPct >= 12.f, nm);
    snprintf(nm, sizeof nm, "res 0.70: level@1.0 (%.1f) >= level@0.9 (%.1f) - 0.5 dB",
             k10.outDb, k9.outDb);
    CHECK(k10.outDb >= k9.outDb - 0.5f, nm);

    // --- Criterion 4: mid-knob voicing preserved vs gritDb 0 baseline ---
    for (float res : { 0.30f, 0.50f }) {
        Meas base = measure(cutoff, res, 0.5f, amp, fs, 0.f);
        Meas grit = measure(cutoff, res, 0.5f, amp, fs, dflt);
        snprintf(nm, sizeof nm, "res %.2f: THD@0.5 default (%.1f%%) <= gritDb0 (%.1f%%) + 8 pp",
                 res, grit.thdPct, base.thdPct);
        CHECK(grit.thdPct <= base.thdPct + 8.f, nm);
    }

    // --- Criterion 5: level never drops > 0.5 dB per 0.1 Drive (res <= 0.60) ---
    for (float res : { 0.30f, 0.50f, 0.60f }) {
        Meas prev = measure(cutoff, res, 0.f, amp, fs, dflt);
        bool mono = true;
        float worst = 0.f;
        for (int i = 1; i <= 10; i++) {
            Meas cur = measure(cutoff, res, 0.1f * i, amp, fs, dflt);
            worst = std::min(worst, cur.outDb - prev.outDb);
            if (cur.outDb < prev.outDb - 0.5f) mono = false;
            prev = cur;
        }
        snprintf(nm, sizeof nm, "res %.2f: level monotone within 0.5 dB (worst step %.2f dB)",
                 res, worst);
        CHECK(mono, nm);
    }

    // --- Bound: 9 V VCA ceiling holds at max settings ---
    Meas worst = measure(cutoff, 0.70f, 1.0f, amp, fs, 12.f);
    snprintf(nm, sizeof nm, "peak (%.2f V) <= 9 V at res 0.70 / drive 1 / grit 12", worst.peak);
    CHECK(worst.peak <= 9.001f, nm);

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
```

- [ ] **Step 2: Print the calibration sweep (also documents the red baseline)**

```bash
cd ~/Dev/RobotBoy/tests && \
g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
    -I../src/particules/dsp/include -o ../build/tests/test_drive_grit \
    onbetap/test_drive_grit.cpp && ../build/tests/test_drive_grit sweep
```

Expected: the `gritDb 0.0` row shows `r.70 thd@1 ≈ 4.4` — i.e. **the old behavior fails criterion 1** (4.4 % < 12 %); this is the red baseline the guard catches. Save the whole table for the worklog (Task 4).

- [ ] **Step 3: Choose the default from the sweep**

Pick the **smallest** `gritDb ∈ {4, 6, 8, 10, 12}` whose row satisfies both:
- criterion 1: `r.70 thd@1 ≥ 12` and `lvl@1 ≥ lvl@.9 − 0.5`;
- criterion 4: `r.30 thd@.5` and `r.50 thd@.5` each within +8 pp of the `gritDb 0.0` row.

If the chosen value ≠ 6, update `kDefaultGritDb` in `src/onbetap/drive.hpp` and the `p1`/comment references in the law checks if needed (the law checks pin 6 dB math explicitly and stay valid regardless of the default). If **no** value satisfies both, steepen the law in `drive.hpp` to `drive⁴`:

```cpp
    float d2 = drive * drive;
    float vcaPush = std::exp2(gritDb / 6.0206f * d2 * d2);
```

update the `p1` law check's expected value to match (`exp2(6/6.0206)` at drive 1 is unchanged; the monotone chain values shift but the assert is inequality-only), re-run the sweep, and re-pick. If it still fails both criteria at every value: **stop and report** — the spec names this a true blocker.

- [ ] **Step 4: Run the asserts**

```bash
../build/tests/test_drive_grit
```

Expected: `13 passed, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add tests/onbetap/test_drive_grit.cpp src/onbetap/drive.hpp
git commit -m "Onbetap: Drive-grit behavioral guards + calibrated default"
```

---

### Task 3: Module wiring

**Files:**
- Modify: `src/onbetap/engine.hpp` (OnbetapVoice: `pushSlew`/`pushTarget`)
- Modify: `src/Onbetap.cpp` (field, modulate, process, processSide, menu, JSON, header comment)

**Interfaces:**
- Consumes: `onbetap::driveGains(..., gritDb).vcaPush`, `onbetap::kDefaultGritDb` (Tasks 1–2).
- Produces: the shipped module behavior; JSON key `tuneGritDb` (clamped 0–12 on load; missing key → default — existing patches pick up the new voicing, intended).

- [ ] **Step 1: Add the per-voice smoother in `engine.hpp`**

In `struct OnbetapVoice`:

```cpp
    OnePoleSmoother makeupSlew { 1.f };
    OnePoleSmoother pushSlew   { 1.f };
```

```cpp
    float driveTarget = 0.25f, makeupTarget = 1.f;
    float pushTarget = 1.f;
```

In `setAlpha`:

```cpp
    void setAlpha(float a) {
        gSlew.setAlpha(a); kSlew.setAlpha(a);
        driveSlew.setAlpha(a); makeupSlew.setAlpha(a);
        pushSlew.setAlpha(a);
    }
```

In `reset()`, after `driveSlew.reset(0.25f); makeupSlew.reset(1.f);`:

```cpp
        pushSlew.reset(1.f);
```

and after `driveTarget = 0.25f; makeupTarget = 1.f;`:

```cpp
        pushTarget = 1.f;
```

- [ ] **Step 2: Wire the module in `Onbetap.cpp`**

Field (after the `tuneOutDb` declaration):

```cpp
	float tuneGritDb = onbetap::kDefaultGritDb;  // Drive-grit VCA push at full Drive (dB)
```

`modulate()` — replace the `driveGains` call block:

```cpp
			auto gains = onbetap::driveGains(drive, tuneDriveDb, tuneHeadroom,
			                                 tuneOutDb, tuneGritDb);
			v.driveTarget  = gains.driveScale;
			v.makeupTarget = gains.makeup;
			v.pushTarget   = gains.vcaPush;
```

`processSide()` — add `float push` to the signature after `float makeup`, and change the final line:

```cpp
	float processSide(OnbetapFilter& flt, float& xPrev, DCBlock& dc, float inVolts,
	                  float g, float kEff, float driveScale, float makeup, float push,
	                  DecimFir13& firLp, DecimFir13& firBp, DecimFir13& firHp) {
```

```cpp
		return 9.f * OnbetapFilter::tanhish(push * v / 9.f); // "overdriven VCA" stage
```

`process()` — slew and pass it (both L and R calls):

```cpp
			float makeup = v.makeupSlew.process(v.makeupTarget);
			float push   = v.pushSlew.process(v.pushTarget);
```

```cpp
			float outL = processSide(v.fL, v.xPrevL, v.dcL, inL, g, kEff, drive, makeup,
			                         push, v.firLpL, v.firBpL, v.firHpL);
```

```cpp
				float outR = processSide(v.fR, v.xPrevR, v.dcR, inR,
				                         g * v.fRgRatio, kEff, drive, makeup, push,
				                         v.firLpR, v.firBpR, v.firHpR);
```

`dataToJson()`:

```cpp
		json_object_set_new(root, "tuneGritDb", json_real(tuneGritDb));
```

`dataFromJson()`:

```cpp
		json_t* gritDb = json_object_get(root, "tuneGritDb");
		if (gritDb)
			tuneGritDb = clamp((float)json_real_value(gritDb), 0.f, 12.f);
```

Tuning menu (after the Output trim slider):

```cpp
			menu->addChild(new MenuSlider(new TuneQuantity(
				&m->tuneGritDb, 0.f, 12.f, "Drive grit", " dB", onbetap::kDefaultGritDb)));
```

File-header comment: after the sentence ending "…dropping level and stripping grit as Drive rises.", add:

```cpp
// A Drive-following push into the output VCA (drive.hpp vcaPush, quadratic in
// drive, bounded by the 9 V ceiling) keeps the top of the knob gaining grit
// while the authentic resonance choke removes the resonance-derived grit.
```

- [ ] **Step 3: Full host-free suite green**

```bash
cd ~/Dev/RobotBoy/tests && ./run.sh
```

Expected: all C++ and python tests pass (previously 41 + the new grit checks).

- [ ] **Step 4: VCV build compiles**

```bash
make -C ~/Dev/RobotBoy/vcv 2>&1 | tail -5
```

Expected: clean build, no errors (this is the compile check for `Onbetap.cpp`; install happens in Task 4).

- [ ] **Step 5: Commit**

```bash
git add src/onbetap/engine.hpp src/Onbetap.cpp
git commit -m "Onbetap: wire Drive-grit VCA push through module + menu"
```

---

### Task 4: Docs, verification, build + install

**Files:**
- Modify: `docs/research/onbetap-worklog.md` (dated entry)
- Modify: `Onbetap.md:15` (Drive bullet), `:26-28` (Tuning sliders)
- Modify: `docs/research/onbetap-drive-resonance-investigation.md` §8 (resolution note)
- Modify: `docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md` (Follow-up pointer)

**Interfaces:**
- Consumes: the calibrated `kDefaultGritDb` and the measured sweep table from Task 2.

- [ ] **Step 1: Worklog entry**

Append to `docs/research/onbetap-worklog.md` a dated entry (2026-07-18, "Drive grit — VCA push") recording: the Finding-3 recap (one sentence), the chosen law `push = exp2(gritDb/6.0206·drive²)` (or `drive⁴` if recalibrated), the chosen default, and the full measured sweep table from Task 2 Step 2 verbatim. Escape literal tildes as `\~`.

- [ ] **Step 2: Manual (`Onbetap.md`)**

Drive bullet (line 15) — append after "…the peak ducks.":

```
At the very top of the knob a drive-tracking push into the output stage keeps the tone thickening even as the resonant ring is fully choked, so more Drive never means a smoother, softer sound.
```

Tuning intro (line 26): change "four sliders" to "five sliders". After the Drive span slider bullet (line 27), add:

```
  - **Drive grit** (0–12 dB, default 6 dB) — how hard full Drive pushes into the output saturation stage. Keeps the top of the Drive knob dirty at high Q; set to 0 for the pre-2026-07 voicing.
```

(If Task 2 recalibrated the default, use that value in both the bullet and this line.)

- [ ] **Step 3: Investigation report resolution note**

In `docs/research/onbetap-drive-resonance-investigation.md` §8, after the "Recommendation:" paragraph, append:

```
**Resolution (2026-07-18, later the same day):** option (3) implemented — a
Drive-following push into the existing output VCA; see
`docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md`.
```

- [ ] **Step 4: Previous spec pointer**

In `docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md`, at the end of the "Follow-up (2026-07-18)" section, append:

```
**Superseded refinement:** the residual top-of-knob grit collapse this span
trim could not cure is fixed by the Drive-grit VCA push — see
`2026-07-18-onbetap-drive-grit-design.md`.
```

- [ ] **Step 5: Full verification**

```bash
cd ~/Dev/RobotBoy/tests && ./run.sh
```

Expected: all pass.

- [ ] **Step 6: Build + install for VCV Rack**

Invoke the `build-robotboy-plugin` skill (VCV path: `make -C vcv`, then copy dylib/plugin.json/res into the Rack2 plugins dir). Restart Rack to load.

- [ ] **Step 7: Commit**

```bash
git add docs/research/onbetap-worklog.md Onbetap.md \
    docs/research/onbetap-drive-resonance-investigation.md \
    docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md
git commit -m "Docs: record Onbetap Drive-grit voicing (worklog, manual, report pointers)"
```

---

## Self-Review Notes

- **Spec coverage:** change surface items 1–5 → Tasks 1–4; acceptance criteria 1, 3, 4, 5 → Task 2 asserts; criterion 2 (existing guards) → Task 1 Step 6 + Task 3 Step 3; criterion 6 (bound) → Task 2 peak assert + existing torture test in the suite; calibration fallback (steeper law / blocker) → Task 2 Step 3.
- **Type consistency:** `driveGains(drive, driveDb, headroom, outDb, gritDb)` and `DriveGains{driveScale, makeup, vcaPush}` used identically in Tasks 1, 2, 3; `kDefaultGritDb` referenced in Tasks 1, 2, 3, 4.
- **Known cross-task compile gap:** `src/Onbetap.cpp` doesn't compile between Task 1 and Task 3 (call-site arity); nothing builds it until Task 3 Step 4. Host-free tests never include it.
