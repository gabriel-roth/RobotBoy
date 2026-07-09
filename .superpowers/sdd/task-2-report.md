# Task 2 Report: MetaModule flush-to-zero

## Status: DONE

## What was done

1. **Created `src/particules/metamodule_fpu.h`** — ported verbatim from `cf74abc:src/particules/metamodule_fpu.h`
   (the `#if defined(METAMODULE) && defined(__arm__)` VMRS/VMSR FPSCR read-modify-write, setting bits 24/25 = FZ +
   DN). Added a doc comment (codex's version lacked one) noting that the FPSCR write intentionally mutates
   thread-wide FPU state for the lifetime of the audio thread it's called from, and is not scoped/restored.

2. **Wired the call into `src/particules/Particules.cpp`**:
   - `#include "metamodule_fpu.h"` added alongside the other `particules_*` includes.
   - `bool metamodule_fpu_configured_ = false;` member added next to `needs_calibration_` (our branch has drifted
     from codex's — this is the nearest analogous one-time-init flag in the current struct layout).
   - At the top of `process()`, before any other per-sample work:
     ```cpp
     if (!metamodule_fpu_configured_) {
         metamodule_fpu_configured_ = true;
         particules::EnableMetaModuleFlushToZero();
     }
     ```
     Order matches the brief's Step 2 snippet (flag set before the call), which differs slightly from cf74abc's
     own ordering (call then flag) — brief takes precedence.

## Verification (all four lanes)

- `cd tests/beads && ./run.sh` — pass (`beads_tests` 100%, 1.13s).
- `cd tests && ./run.sh` — pass (beads unit tests, particules CV-conditioning tests, pitch/notch map tests all green).
- `make -C vcv -j8` — pass. Clean build; only pre-existing unrelated `-Wdeprecated-this-capture` warnings from
  Rack SDK helpers.hpp (menu lambda captures), not from this change.
- `cmake --build metamodule/build -j8` — pass. This is the meaningful check since the ARM branch only compiles
  under MetaModule. Confirmed by disassembling the built object
  (`metamodule/build/CMakeFiles/RobotBoy.dir/.../Particules.cpp.obj`): `Particules::process` opens with a guard
  read of the `metamodule_fpu_configured_` byte, and on the first-call path executes
  `vmrs r3, fpscr` / `orr r3, r3, #50331648` (0x3000000 = bits 24+25) / `vmsr fpscr, r3` — i.e. the flush-to-zero
  write is actually emitted and reachable, not compiled out.

## Self-review

- Header content matches the source commit exactly aside from the added comment (no functional change).
- Non-ARM builds (macOS VCV lane) compile the function to an empty inline no-op; verified by successful
  `make -C vcv` build with no new warnings/errors attributable to this change.
- Member and call placement adapted to current file (Schmitt-trigger-based gates, no `stereo_input_`) rather than
  assuming codex's line numbers — placed next to the most similar existing one-shot-init flag
  (`needs_calibration_`) and at the very top of `process()`, ahead of all other per-sample logic, matching intent.
- Diff is minimal: 2 files changed, 26 insertions, 0 deletions — no unrelated changes.

## Commit

`8625f6c` — "perf: enable flush-to-zero on MetaModule audio thread"

## Concerns

None. Task complete, no blockers.
