# Task 2 Report: F5 — Grain-count LED Density

## Status: DONE

## Commit

`2bf2cc8` — "feat: grain LED brightness tracks active grain count"
3 files changed, 51 insertions(+), 6 deletions(-)

## What Was Implemented

### Core Feature: `NoteGrainActivity` Method

Added to `src/particules/particules_block_runtime.h` (after line 82):
- Takes `active_count` (snapshot from `processor_.ActiveGrainCount()`) and `triggered` (from `processor_.GrainTriggeredThisBlock()`)
- Calculates target brightness: `0.25 + 0.75 * count / 10.0`, clamped at 1.0
- Special case: if count is 0 but triggered, still registers at floor 0.25 (transient grains)
- Critical invariant: **never dims an already-bright LED** (decay machinery owns dimming)

### Wrapper Integration

Modified `src/particules/Particules.cpp` (lines 407-415):
- Removed old boolean flash: `SetGrainLed(1.f)`
- Added new call: `NoteGrainActivity(processor_.ActiveGrainCount(), triggered)`
- Simplified trigger pulse condition: `if (triggered && grain_trigger_out_)` (decoupled from LED)

### Test Suite

Appended comprehensive test to `tests/beads/test_particules_block_runtime.cpp`:
- Single grain floor: 0.325 brightness
- 10+ grains: 1.0 brightness (clamped)
- Transient grain (count 0, triggered): 0.25 brightness
- No activity: LED untouched (0.1 stays 0.1)
- Never dims: 1.0 stays 1.0 even with low grain count

## TDD Evidence

### Step 2: RED — Verify failure before implementation

```
$ tests/beads/run.sh
...
error: no member named 'NoteGrainActivity' in 'ParticulesBlockRuntime<4>'
[repeated 6 times for each test call]
make[2]: *** [CMakeFiles/beads_tests.dir/test_particules_block_runtime.cpp.o] Error 1
```

### Step 4: GREEN — Verify implementation passes tests

```
$ tests/beads/run.sh
...
Test project /Users/gabrielroth/Dev/RobotBoy/.worktrees/particules-track/tests/beads/build
    Start 1: beads_tests
1/1 Test #1: beads_tests ......................   Passed    1.54 sec

100% tests passed, 0 tests failed out of 1
```

### Step 6: Wrapper compiles clean

```
$ cd vcv && make -j8
...
c++ ... -o plugin.dylib ... -shared
[Build succeeds with no errors about NoteGrainActivity]
```

## Files Changed

- `src/particules/particules_block_runtime.h` — Added `NoteGrainActivity(int, bool)` method
- `src/particules/Particules.cpp` — Wired wrapper to call `NoteGrainActivity` and simplified trigger condition
- `tests/beads/test_particules_block_runtime.cpp` — Added comprehensive test case

## Self-Review

### Implementation Correctness
- Math formula matches brief exactly: `0.25 + 0.75 * count / 10.0`
- `std::min(1.0f, ...)` clamps properly
- Static cast to float is appropriate
- Conditional logic handles all cases: positive count, zero count + triggered, zero + not triggered

### API Design
- Method signature clean: `void NoteGrainActivity(int active_count, bool triggered)`
- Integrates with existing LED machinery (SetGrainLed/GrainLed/DecayGrainLed unchanged)
- Never-dims invariant preserved (`if (target > grain_led_)`)
- Comments explain design intent clearly

### Test Coverage
- 5 distinct test scenarios covering all code paths
- Edge cases: zero grains, high grain counts, transient grains, no activity
- Uses Approx for floating-point comparisons
- Each scenario is independent (SetGrainLed resets state between tests)

### Integration
- Wrapper correctly passes `ActiveGrainCount()` and `GrainTriggeredThisBlock()`
- Trigger pulse generation decoupled from LED brightness (cleaner separation of concerns)
- No impact on decay machinery or other LED methods

### Pre-existing Warnings
- VCV build emits 4 deprecation warnings about implicit `this` capture in Rack SDK headers (unrelated to this change)
- No new warnings introduced

## Concerns

None. Implementation follows brief precisely, all tests pass, wrapper compiles cleanly.
