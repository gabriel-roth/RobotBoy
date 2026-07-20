# Particules — Clocked Density noon = silence (plan)

**Goal:** Make Particules' clocked-mode Density knob treat 12 o'clock (default) as silence, with CCW = clock division ÷16→÷1 and CW = probability 0→100%, matching `Particules.md:116`.

**Source spec:** `docs/superpowers/specs/2026-07-20-particules-clocked-density-noon-silence-design.md` (do not modify).

**Architecture:** Single DSP change in the `kClocked` branch of `src/particules/dsp/src/grain/grain_scheduler.cpp`, plus test updates in `tests/particules_dsp/test_scheduler_clocked.cpp`. TDD: update/add tests first (red), then change the mapping (green).

**Tech stack:** C++ (Particules granular DSP), Catch2 lane `tests/particules_dsp/run.sh` (CMake + CTest).

## Global constraints

- Worktree `/Users/gabrielroth/Dev/RobotBoy/.claude/worktrees/worktree-particules-clock`, branch `worktree-particules-clock`.
- No new params, no panel/tooltip/VCV/MetaModule changes.
- Divider counter logic and mode-switch reset stay untouched.
- Commit messages: short, one sentence, ≤15 words, no AI attribution.

## File structure

| File | Change | Responsibility |
|---|---|---|
| `src/particules/dsp/src/grain/grain_scheduler.cpp` | Modify | Reverse divider direction; noon → silence |
| `tests/particules_dsp/test_scheduler_clocked.cpp` | Modify | Flip the noon test to silence; add ÷16-near-noon and ÷1-at-full-CCW tests |

## Tasks

### 1. Tests first (red)
- [ ] Rewrite `test_scheduler_clocked.cpp:26` case: rename + assert eff_density = 0.5 produces **0 triggers** over 10 clocks.
- [ ] Add case: eff_density just below noon (`0.49`) → **÷16** (exactly 1 trigger over 16 clocks, landing on the 16th).
- [ ] Add case: eff_density = 0.0 (fully CCW) → **÷1** (a trigger on every clock; 10/10).
- [ ] Add spot-check: eff_density = 1.0 (fully CW) → probability 100% → every clock (10/10).
- [ ] Keep the two `eff_density = 0.25 → ÷4` cases as-is; update their inline comments to the new mapping formula.
- [ ] Run the lane, confirm the new noon/÷16/÷1 cases fail against current code (÷4 cases should still pass).

### 2. Implement mapping (green)
- [ ] In `grain_scheduler.cpp` `kClocked` case:
  - CCW branch (`eff_density < 0.5f`): replace the division computation with
    `distance = (0.5 - eff)*2; level = min(int(distance*5), 4); division = 1 << (4 - level);`
    Update the explanatory comment (÷16 near noon → ÷1 fully CCW).
  - Remove the `else` "trigger on every clock" branch; exactly-0.5 now falls through to no trigger (silence). Keep `prev_clock_` update.
  - Leave the CW probability branch and the divider counter untouched.
- [ ] Run `tests/particules_dsp/run.sh` — expect `100% tests passed`.

### 3. Sanity + finish
- [x] `make -C vcv -j8` compiles clean (no interface change; sanity only).
- [x] Confirm `Particules.md:116` matches the new behavior (no edit expected).
- [x] Commit: tests+impl together (or tests then impl), short message. (6e744b7)

### 4. Follow-up: small dead-zone band (2026-07-20)
- [x] Add symmetric silence band `eff_density ∈ [0.5 ± 0.02]` in the kClocked branch (`kNoonDeadZone` constant); mappings unchanged outside it.
- [x] Update tests: move the ÷16 case off 0.49 → 0.47; add a dead-zone case asserting 0.49 and 0.51 are silent.
- [x] Re-run `tests/particules_dsp/run.sh` (100% pass) and commit.
