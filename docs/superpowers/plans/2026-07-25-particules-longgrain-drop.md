# Particules Long-Grain Trigger Drop Implementation Plan

> **For agentic workers:** Single-task plan; spec is authoritative:
> `docs/superpowers/specs/2026-07-25-particules-longgrain-trigger-drop-design.md`

**Goal:** Implement the spec on branch `particules-longgrain-drop`.

## Task 1: Scheduler tagging + engine drop condition + tests

**Files:**
- Modify: `src/particules/dsp/src/grain/grain_scheduler.h` / `.cpp` (add `bool* droppable` output parameter to `Process`; write `false` by default at every trigger emission, `true` only for the `kLatched` phasor ticks and `kGated` held-repeat ticks — NOT the `kGated` rising edge, NOT `kClocked`, NOT `kMidi`)
- Modify: `src/particules/dsp/src/grain/grain_engine.cpp` (declare `bool trigger_droppable[kMaxTriggers]` beside `trigger_samples`, pass to scheduler; in the spawn loop's saturation branch, before `FindOldestActiveGrain`, add: `if (trigger_droppable[t] && cached_max_active_ == 2) continue;` with a comment citing the spec)
- Test: `tests/particules_dsp/test_grain_kill.cpp` or a new `test_longgrain_drop.cpp` (suite glob picks up `test_*.cpp`) implementing the spec's four test bullets. Drive `GrainEngine` directly (see how existing tests construct it) or through `ParticulesProcessor` — whichever existing tests make cheap.

**Steps:** write the failing tests first (drop case fails against current steal behavior); implement; suite green (`tests/particules_dsp/run.sh` AND `tests/run.sh` for the wrapper lane); `make -C vcv` clean. One commit: `Particules: drop automatic triggers at the long-grain cap floor`.

**Constraints:** no behavior change below the cap floor (mid-size test pins it); `kMidi` untouched; commit style short, no AI attribution; never stage user doc files.
