# Particules — Clocked Density noon = silence (design)

**Date:** 2026-07-20
**Module:** Particules
**Scope:** DSP only (`grain_scheduler.cpp` kClocked branch) + its Catch2 tests. No panel, no param, no VCV-layer, no MetaModule changes.

## Problem

With a clock patched into **Seed** (SEED CV mode = Triggers) and **Density at 12 o'clock (0.5, the knob default)**, Particules fires a grain on *every* clock pulse. The user did not expect this, and it contradicts the manual.

`Particules.md:116` documents the intended behavior:

> **A clock or trigger patched into Seed** — *clocked.* **Density** is repurposed as a divider/probability control. At 12 o'clock no grains fire. Turn clockwise to raise the *probability* (0–100%) that each incoming trigger spawns a grain; turn counter-clockwise to *divide* the clock (from 1/16 up to 1/1).

The code (`src/particules/dsp/src/grain/grain_scheduler.cpp`, `kClocked` case) instead:

- Maps CCW as division with **÷1 adjacent to noon** and **÷16 at the fully-CCW extreme** — the *reverse* of the manual's "from 1/16 up to 1/1."
- Treats **exactly 0.5** as "trigger on every clock" (`grain_scheduler.cpp:169-174`), not silence.

`tests/particules_dsp/test_scheduler_clocked.cpp:26` pins the every-clock-at-noon behavior, so this is deliberate code, not an accident — but it is wrong per the manual and surprising as a default.

## Decision

Fix the **code** to match the **manual** (the manual is the target spec; it stays unchanged). Rework the `kClocked` Density mapping so:

| Density (eff_density = clamp(density + density_cv, 0, 1)) | Behavior |
|---|---|
| **Exactly 0.5** (noon, default) | **Silence** — no grains on any clock |
| **0.5 → 1.0** (CW) | Probability the clock spawns a grain, `0% → 100%` (unchanged) |
| **just below 0.5** (CCW) | Clock division **÷16** (sparsest) |
| **→ 0.0** (fully CCW) | Clock division ramps up to **÷1** (every clock) |

This makes the knob **monotonic in grain density from the center outward** in both directions (noon = fewest grains = silence; both extremes = every clock), matching the free-running (kLatched) knob's "noon = silence, denser toward the edges" identity.

### Divider mapping detail (CCW, eff_density < 0.5)

```
distance = (0.5 - eff_density) * 2      // (0, 1]
level    = min(int(distance * 5), 4)    // 0..4
division = 1 << (4 - level)             // 16, 8, 4, 2, 1
```

Resulting zones (each ~20% of the CCW travel):

| distance | division |
|---|---|
| (0, 0.2)   | ÷16 |
| [0.2, 0.4) | ÷8  |
| [0.4, 0.6) | ÷4  |
| [0.6, 0.8) | ÷2  |
| [0.8, 1.0] | ÷1  |

- `eff_density = 0.25` (the value pinned by two existing tests) → distance 0.5 → level 2 → **÷4**, identical to today, so the division-by-4 tests keep passing unchanged.
- Divider counter logic (`gate_phase_` as an integer clock counter) is unchanged; only the `division` value and its direction change.

### Noon = silence

- `eff_density == 0.5` exactly → no trigger (replaces the old "trigger on every clock" branch).
- The default param (`density = 0.5`, `density_cv = 0.0`) yields exactly `0.5f`, so the out-of-the-box patch is silent under a clock.
- Silence at noon is a knife-edge, matching the manual: the CW side fades to ~0% probability approaching noon, and the CCW side starts at ÷16 (one grain per 16 clocks) immediately CCW of noon. This is intended — a divider always does *something* once engaged, and ÷16 is the closest-to-silent division.

## Non-goals

- No dead-zone band around noon (knife-edge silence is intended and matches the manual).
- No change to kLatched, kGated, or kMidi modes.
- No change to `Particules.md` (already correct), the panel, params, tooltips, or the VCV/MetaModule layers.
- No change to the divider *counter* semantics (first-output alignment, mode-switch reset).

## Acceptance

1. `tests/particules_dsp` Catch2 lane is green.
2. New/updated tests assert:
   - eff_density = 0.5 → **0 triggers** over N clocks.
   - eff_density just below 0.5 → **÷16** (one trigger per 16 clocks).
   - eff_density = 0.0 (fully CCW) → **÷1** (every clock).
   - eff_density = 0.25 → **÷4** (unchanged; existing tests still pass).
   - eff_density > 0.5 → probability branch unchanged (spot-check 1.0 → every clock).
3. `vcv/` compiles clean (`make -C vcv -j8`) — no interface change, sanity only.
4. Manual (`Particules.md:116`) now matches the code with no edits.
