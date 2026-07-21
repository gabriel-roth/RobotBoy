# Cold Digital Clouds-Emulation Voicing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Voice Cold digital as a Clouds emulation: cubic write-path soft-limit (Clouds' own curve/drive) so overload is a warm smudge instead of the codec hard clamp, plus a deliberately leaky anti-alias LP for the cold digital aliasing sheen — the AA change shared with Particules per user decision.

**Architecture:** Two constants and one switch case, all in shared `particules_dsp`. (1) `Saturation::SaturateWrite` Cold case becomes `NormalizedSoftClip(input, kColdWriteDrive=1.4f)` — only Retours calls `SaturateWrite`, so this lands in Retours only. (2) `QualityProcessor::kColdDigitalInputLpHz` 10000 → 11500 Hz — shared constant, affects both Retours and Particules Cold input filtering (user approved shared). Research basis: `docs/superpowers/plans/2026-07-21-cold-digital-clouds-voicing-notes.md` (Clouds `granular_processor.cc:197-203` uses `SoftLimit` — the same polynomial as our `FastTanh` — with 1.4× drive).

**Tech Stack:** C++17, Catch2, test lanes `tests/particules_dsp/run.sh` and `tests/retours_delay_dsp/run.sh`.

## Global Constraints

- Commit messages: short, one sentence, ≤15 words, no AI attribution / no Co-Authored-By lines.
- Both test lanes must pass at every commit.
- All work in worktree `/Users/gabrielroth/Dev/RobotBoy/.claude/worktrees/retours-quality-degradation` on branch `worktree-retours-quality-degradation`.
- In worklog Markdown, escape literal approximation tildes as `\~` (code spans exempt).
- Threshold rule: record RED-side and GREEN-side measured values; if a threshold sits closer than 1.5 dB (or the equivalent linear margin) to either measured side, move it to the midpoint and note it in the worklog. Never weaken a threshold merely to pass.

---

### Task 1: Clouds Cold voicing — curves, constant, tests (TDD)

**Files:**
- Modify: `src/particules/dsp/src/fx/saturation.h`
- Modify: `src/particules/dsp/src/fx/saturation.cpp`
- Modify: `src/particules/dsp/src/quality/quality_processor.h`
- Modify: `tests/particules_dsp/test_saturation_curves.cpp`
- Modify: `tests/particules_dsp/test_tape_voicing.cpp`
- Modify: `tests/retours_delay_dsp/test_quality_degradation.cpp`

**Interfaces:**
- Consumes: `Saturation::NormalizedSoftClip(float, float)` (private helper, exists), `SaturateWrite` already wired into Retours' write path (`retours_processor.cpp`, lands automatically), `RenderSteady(mode, buffer_seconds)` helper in `test_quality_degradation.cpp` (exists).
- Produces: `Saturation::kColdWriteDrive = 1.4f`; Cold's `SaturateWrite` ceiling 1/1.4 ≈ 0.714; `QualityProcessor::kColdDigitalInputLpHz = 11500.0f`.

- [ ] **Step 1: Update/extend the tests (this is the RED — Cold's pass-through assertion flips to the new contract)**

In `tests/particules_dsp/test_saturation_curves.cpp`, the test `"Saturation: SaturateWrite ceilings sit below the codec clamp"` currently asserts Cold passes through:

```cpp
    REQUIRE(sat.SaturateWrite(1.7f, QualityMode::kColdDigital) == 1.7f);
```

Replace that line and extend the test. The digital-pass-through block becomes Bright-only, and Cold gets its own assertions:

```cpp
    // Bright passes through untouched.
    REQUIRE(sat.SaturateWrite(1.7f, QualityMode::kBrightDigital) == 1.7f);
    // Cold is a Clouds emulation: the same cubic soft-limit curve (and 1.4x
    // drive) Clouds ran its feedback sum through — symmetric, unity
    // small-signal slope, ceiling 1/1.4. Overload smudges, never hard-clips.
    REQUIRE(sat.SaturateWrite(0.01f, QualityMode::kColdDigital) / 0.01f
            == Approx(1.0f).margin(0.05f));
    REQUIRE(sat.SaturateWrite(-0.01f, QualityMode::kColdDigital) / -0.01f
            == Approx(1.0f).margin(0.05f));
    REQUIRE(sat.SaturateWrite(2.0f, QualityMode::kColdDigital) <= 0.72f);
    REQUIRE(sat.SaturateWrite(2.0f, QualityMode::kColdDigital) >= 0.70f);
    // Symmetric (no tape bias in a digital mode).
    REQUIRE(sat.SaturateWrite(2.0f, QualityMode::kColdDigital)
            == Approx(-sat.SaturateWrite(-2.0f, QualityMode::kColdDigital)).margin(1e-6f));
```

Also update the loop that asserts unity small-signal slope for tape modes so it covers Cold as well (add `QualityMode::kColdDigital` to its mode list) — or rely on the explicit lines above; either way Cold must be asserted.

In `tests/particules_dsp/test_tape_voicing.cpp`, add a new TEST_CASE after the existing one (reuse the file's `SteadyAmp4k` pattern, but the probe needs a frequency parameter — generalize the helper to `SteadyAmp(qp, mode, freq_hz)` and keep the existing call sites working):

```cpp
TEST_CASE("QualityProcessor: Cold anti-alias LP is deliberately leaky near Nyquist") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    // 13 kHz probe through Cold's input LP. 2-pole Butterworth math:
    // fc=10 kHz (old) -> |H| ~= 0.509 (-5.9 dB); fc=11.5 kHz -> ~0.617
    // (-4.2 dB). Threshold at the midpoint per the plan's threshold rule.
    float in_amp = 1.0f;
    float out_amp = SteadyAmp(qp, QualityMode::kColdDigital, 13000.0f);
    REQUIRE(out_amp / in_amp > 0.563f);
    REQUIRE(out_amp / in_amp < 0.68f);   // still a filter, not a bypass
}
```

Note: `SteadyAmp` must probe `ProcessInput` for this test (the AA filter is on the input side), not `ProcessOutput`. If the existing helper is output-side only, add an input-side variant `SteadyInputAmp(qp, mode, freq_hz)` with the same settle-then-measure structure (mode-change crossfade is 64 samples; skip the first 4800 samples, measure peak over the next second).

In `tests/retours_delay_dsp/test_quality_degradation.cpp`, add:

```cpp
// -----------------------------------------------------------------------
// (D) Cold digital is a Clouds emulation: accumulated feedback lands on
// the cubic write-limiter ceiling (1/1.4 ~= 0.71), never the int12 codec
// clamp. Pre-fix Cold measured 37% of steady-state samples pinned above
// 0.985 (identical overload character to Bright); post-fix it must be
// clamp-free while Bright still brickwalls (asserted in test B above).
// -----------------------------------------------------------------------
TEST_CASE("degradation: Cold settles on the Clouds soft-limit, not the codec clamp") {
    auto cold = RenderSteady(QualityMode::kColdDigital, 8.f);
    REQUIRE(cold.clip_frac < 0.005);
    REQUIRE(cold.peak < 0.9);
}
```

- [ ] **Step 2: Run both suites to verify RED (record values)**

```bash
./tests/particules_dsp/run.sh 2>&1 | tee /tmp/cold_red.txt
./tests/retours_delay_dsp/run.sh 2>&1 | tee -a /tmp/cold_red.txt
```

Expected failures: Cold SaturateWrite assertions (currently returns input unchanged → 2.0 stays 2.0), the 13 kHz leaky-LP test (old cutoff gives ~0.509 < 0.563), and Retours test D (clip_frac ~0.37). Record the printed values.

- [ ] **Step 3: Implement**

`src/particules/dsp/src/fx/saturation.h` — add next to the other drive constants:

```cpp
    // Cold digital write drive — a Clouds emulation: Clouds ran its
    // feedback sum through stmlib SoftLimit (the same polynomial as our
    // FastTanh) with a 1.4x drive (granular_processor.cc:197-203), so its
    // overload was a warm cubic smudge, never a hard clip. Normalized here
    // (unity small-signal slope, ceiling 1/1.4) to keep the uniform
    // loop-gain law. See docs/superpowers/plans/
    // 2026-07-21-cold-digital-clouds-voicing-notes.md.
    static constexpr float kColdWriteDrive = 1.4f;
```

`src/particules/dsp/src/fx/saturation.cpp` — in `SaturateWrite(float, QualityMode)`, split Cold out of the pass-through:

```cpp
        case QualityMode::kBrightDigital:
            return input;
        case QualityMode::kColdDigital:
            // Clouds emulation: symmetric cubic soft-limit, Clouds' own
            // curve and drive (see kColdWriteDrive in the header).
            return NormalizedSoftClip(input, kColdWriteDrive);
```

Also update the header comment above `SaturateWrite`'s declaration ("Digital modes pass through") to say Bright passes through and Cold applies the Clouds cubic limiter.

`src/particules/dsp/src/quality/quality_processor.h` — change the constant and its comment block:

```cpp
    // Anti-alias input LPs for the 2x-decimated modes (Nyquist 12 kHz).
    // Tape modes protect the band conventionally at 10 kHz. Cold digital
    // is deliberately leakier — 11.5 kHz, 2-pole, right under Nyquist —
    // so bright material folds back audibly: the un-anti-aliased "cold
    // digital sheen" that was part of the Clouds character (Clouds had no
    // anti-aliasing on transposed reads at all). Shared by Particules and
    // Retours per 2026-07-21 decision.
    static constexpr float kColdDigitalInputLpHz = 11500.0f;
```

(Keep `kSunnyTapeInputLpHz` / `kScorchedInputLpHz` at 10000 and their existing comment intact for the tape modes.)

- [ ] **Step 4: Run both suites to verify GREEN (record values)**

```bash
./tests/particules_dsp/run.sh && ./tests/retours_delay_dsp/run.sh
```

Expected: all pass. If any pre-existing test fails, investigate whether it pinned old Cold behavior (e.g., attenuation figures at the old 10 kHz cutoff); recalibrate only with an inline comment justifying the intentional voicing change, and record it for the worklog — never touch DSP constants to satisfy an old test.

- [ ] **Step 5: Commit**

```bash
git add -A src/particules/dsp tests/particules_dsp tests/retours_delay_dsp
git commit -m "Cold digital: Clouds cubic write limiter and leaky anti-alias voicing"
```

---

### Task 2: Validation, worklog addendum, checklist fix, rebuild + reinstall

**Files:**
- Modify: `docs/superpowers/plans/2026-07-21-retours-quality-degradation-worklog.md`
- Modify: `docs/superpowers/plans/2026-07-21-cold-digital-clouds-voicing-notes.md`

**Interfaces:**
- Consumes: Task 1's RED/GREEN recorded values; the probe binaries pattern from the previous round (scratchpad `quality_probe.cpp` / `steady_probe.cpp`, rebuilt against `tests/retours_delay_dsp/build/librobotboy_retours_delay_dsp.a`).

- [ ] **Step 1: Re-run both probes against the rebuilt library; check goals**

Rebuild the probes exactly as in the previous round (same compiler line, fresh `run.sh` first). Goals:
1. Cold steady-state: clip% < 0.5 (was 37.0), peak ≤ 0.75 (ceiling 0.714 + interpolation overshoot allowance).
2. Bright steady-state unchanged (clip% > 30) — proves the change is Cold-scoped.
3. Cold burst decay slope (RMS dB/repeat over repeats 8-12) within 0.5 dB of Bright's — unity small-signal slope preserved.
4. Tape modes' rows unchanged from the previous round's tables (Sunny/Scorched untouched by this change).

Tuning latitude (only if a goal fails): `kColdWriteDrive` ∈ [1.2, 1.6]; `kColdDigitalInputLpHz` ∈ [11000, 11800]. Update dependent test thresholds per the derivations in the test comments; document in the worklog.

- [ ] **Step 2: Worklog addendum + checklist fix**

Append to `2026-07-21-retours-quality-degradation-worklog.md` a section "## Round 2: Cold digital Clouds voicing (2026-07-21)" containing: what changed and why (two constants + one case; AA change shared with Particules per user decision), RED/GREEN values from Task 1, probe tables for Cold before/after, a note that Particules Cold input filtering brightened slightly (10 kHz → 11.5 kHz, 2-pole) and gains fold-back sheen on bright sources.

**Fix the existing user listening checklist item for Cold digital** — it currently reads "unchanged character (12-bit, 10 kHz), still crunches digitally at high feedback (intended)". Replace with:

```markdown
- [ ] **Cold digital:** now a Clouds emulation — repeats keep most of their
      brightness with a faint aliased sheen on bright sources; high feedback
      overloads into a warm, congested "smudgy" wall (cubic soft-limit),
      clearly different from Bright's hard brickwall. Compare against
      Particules' Cold, which shares the same character.
```

Escape approximation tildes as `\~` throughout new text.

Update `2026-07-21-cold-digital-clouds-voicing-notes.md`: add a one-line status note at the top ("Implemented 2026-07-21 — see worklog Round 2; AA change applied shared per user decision.").

- [ ] **Step 3: Final sweep, rebuild, reinstall**

```bash
./tests/particules_dsp/run.sh && ./tests/retours_delay_dsp/run.sh
make -C vcv -B -j4 2>&1 | tail -3
DEST="$HOME/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy"
cp vcv/plugin.dylib "$DEST/plugin.dylib"
cp plugin.json "$DEST/plugin.json"
rm -rf "$DEST/res" && cp -R res "$DEST/res"
```

(Deprecation warnings from Rack SDK headers are expected. The install refresh matters — the user auditions from this Rack install.)

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-07-21-retours-quality-degradation-worklog.md docs/superpowers/plans/2026-07-21-cold-digital-clouds-voicing-notes.md
git commit -m "Worklog round 2: Cold Clouds voicing measurements and checklist"
```

---

## Self-review notes

- Spec coverage: Clouds cubic write limiter (Task 1 curves), shared leaky AA (Task 1 constant), tests at unit + filter + integration level, Particules impact documented (Task 2), rebuild/reinstall for listening (Task 2). ✓
- Type consistency: `kColdWriteDrive` defined Task 1, referenced only there; `RenderSteady` exists from the previous round. ✓
- Known soft spot: the 13 kHz filter-test margins are ±~0.05 linear on each side; the threshold rule handles drift, and Step 2/4 record actuals.
