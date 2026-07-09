# Task 2 Report: MF-20 NaN/inf recovery

## What I implemented

Added a per-modulate-block NaN/inf recovery guard to the MF-20 filter, exactly
as specified in the brief:

- `src/mf20/MF20Filter.hpp` — added `bool stateFinite() const` next to
  `reset()`, checking `std::isfinite(s1) && std::isfinite(s2)` (the two TPT
  integrator states). Public `process(in, cutoffHz, res)` signature is
  untouched.
- `src/mf20/engine.hpp` — added `void VoiceEngine::sanitize()` after
  `reset()`. It checks `stateFinite()` on all four per-voice filters
  (`lpFilter`, `hpFilter`, `lpFilterR`, `hpFilterR`) and, if any is
  non-finite, resets all four filter states. It deliberately does **not**
  touch the cutoff/resonance slew smoothers (per the brief's note that Task 4
  will rename them — this task's `sanitize()` only resets the `MF20Filter`
  integrator states, not smoother state).
- `src/mf20/MF20Filter.cpp` — added `eng->sanitize();` in `modulate()`,
  immediately after `if (!eng) continue;` in the per-voice loop, so recovery
  runs once per modulate block (~2.5 ms) per active voice, not per sample.
- `tests/mf20/test_mf20.cpp` — added `#include "../../src/mf20/engine.hpp"`
  and the two new tests from the brief verbatim: `test_nan_recovery()` (both
  OTA and K35 modes: finite after normal use → non-finite after a NaN sample
  → finite again after `reset()`, with 200 finite samples following) and
  `test_voice_sanitize()` (poison `VoiceEngine::hpFilter` with a NaN sample,
  call `sanitize()`, confirm it's finite and stays finite for 200 samples).
  Both registered in `main()`.

## TDD Evidence

### RED

Command: `cd /Users/gabrielroth/Dev/RobotBoy/tests && ./run.sh`

Relevant output (tests added, implementation not yet added):

```
== building mf20/test_mf20.cpp ==
mf20/test_mf20.cpp:819:18: error: no member named 'stateFinite' in 'MF20Filter'
  819 |         report(f.stateFinite(), b1);
      |                ~ ^
mf20/test_mf20.cpp:822:19: error: no member named 'stateFinite' in 'MF20Filter'
mf20/test_mf20.cpp:824:21: error: no member named 'stateFinite' in 'MF20Filter'
mf20/test_mf20.cpp:839:7: error: no member named 'sanitize' in 'VoiceEngine'
mf20/test_mf20.cpp:840:26: error: no member named 'stateFinite' in 'MF20Filter'
5 errors generated.
```

Matches the brief's expected RED state exactly (compile error on
`stateFinite`/`sanitize`, not yet a member).

### GREEN

Command: `cd /Users/gabrielroth/Dev/RobotBoy/tests && ./run.sh` (exit 0)

```
NaN recovery (stateFinite + reset)
  PASS  OTA: finite after normal use
  PASS  OTA: non-finite after NaN input
  PASS  OTA: finite output after reset
  PASS  K35: finite after normal use
  PASS  K35: non-finite after NaN input
  PASS  K35: finite output after reset

VoiceEngine::sanitize recovers poisoned voice
  PASS  sanitize() resets non-finite filter state

=======================
39 passed, 0 failed
```

All other binaries in the run (loooop, particules, dsp_utils) also passed
with no FAIL lines anywhere in the full `run.sh` output; overall exit code 0.

### Builds

- `make -C vcv -j8` — exit 0. Only pre-existing, unrelated Rack-SDK
  deprecation warnings (`helpers.hpp`, implicit `this` capture), nothing new
  from this change.
- `cmake --build metamodule/build -j8` — exit 0. `RobotBoy.so` built,
  "All symbols found!", `.mmplugin` package created successfully.

## Files changed

- `src/mf20/MF20Filter.hpp` — added `stateFinite()`.
- `src/mf20/engine.hpp` — added `VoiceEngine::sanitize()`.
- `src/mf20/MF20Filter.cpp` — call `eng->sanitize()` in `modulate()`.
- `tests/mf20/test_mf20.cpp` — added `engine.hpp` include, two new tests,
  registered in `main()`.

Commit: `0c0c90f` — "fix: MF-20 recovers from NaN/inf filter state"
(4 files changed, 62 insertions, matches the brief's `git add src/mf20
tests/mf20` file scope exactly.)

## Self-review

- **Completeness**: All 6 brief steps done — failing tests written and
  verified RED, `stateFinite()`/`sanitize()`/`modulate()` call added
  verbatim, tests verified GREEN, both plugin builds verified, committed
  with the exact specified message.
- **Signature preserved**: `MF20Filter::process(in, cutoffHz, res)` public
  signature is unchanged — `stateFinite()` is a new, separate accessor.
- **Scope discipline**: `sanitize()` resets only the four `MF20Filter`
  instances' integrator states (`s1`/`s2` via `reset()`); it does not touch
  `lpCutoffSlew`/`hpCutoffSlew`/`lpResSlew`/`hpResSlew` or their targets, per
  the brief's explicit instruction (their inputs are clamped params and stay
  finite, and resetting them would cause a spurious parameter sweep). This
  also satisfies the constraint that Task 4 (which renames these smoothers)
  can land cleanly on top.
- **Placement**: `eng->sanitize()` runs once per voice per `modulate()` call
  (~2.5 ms cadence), not per audio sample — matches the brief's stated
  "per-modulate-block recovery guard" performance intent, not a per-sample
  check.
- **Testing**: both new tests exercise real observable behavior (actual
  NaN propagation through the TPT state-update `s = 2·mid - s`, confirmed by
  `stateFinite()` before/after, and by checking `std::isfinite` on filter
  outputs over 200 further samples) — not tautological. Verified they fail
  pre-implementation (RED, captured above) and pass post-implementation
  (GREEN, captured above).
- **Concerns**: None. The diff matches the brief's exact code verbatim
  (`stateFinite()`, `sanitize()`, and the `modulate()` call site), and all
  verification gates (tests, VCV build, MetaModule build) passed cleanly.
