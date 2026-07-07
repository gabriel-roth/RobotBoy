# Remove Particules Delay Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete the unreachable Particules delay engine and simplify BeadsProcessor to its only active granular signal path.

**Architecture:** Remove the unused mode from the public parameter/API surface, then collapse processor initialization, input/feedback handling, wet rendering, and output selection to the existing grain-mode branches. Delete the standalone engine and tests after a source-contract test proves every delay-mode integration point is gone. Preserve the user-edited manuals outside all commits.

**Tech Stack:** C++17/20, Python 3 unittest, Catch2, CMake, GNU Make, VCV Rack SDK, 4ms MetaModule SDK.

---

### Task 1: Add a failing no-delay contract

**Files:**
- Create: `tests/test_no_delay_mode.py`
- Test: `tests/test_no_delay_mode.py`

- [ ] **Step 1: Write the failing source-contract test**

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NoDelayModeTest(unittest.TestCase):
    def test_delay_engine_files_are_removed(self):
        self.assertFalse((ROOT / "src/vendor/beads_dsp/src/delay/delay_engine.h").exists())
        self.assertFalse((ROOT / "src/vendor/beads_dsp/src/delay/delay_engine.cpp").exists())
        self.assertFalse((ROOT / "tests/beads/test_delay.cpp").exists())

    def test_delay_mode_symbols_are_removed(self):
        paths = [
            "src/vendor/beads_dsp/include/beads/parameters.h",
            "src/vendor/beads_dsp/include/beads/beads.h",
            "src/vendor/beads_dsp/src/beads_processor.h",
            "src/vendor/beads_dsp/src/beads_processor.cpp",
            "metamodule/CMakeLists.txt",
        ]
        forbidden = (
            "delay_mode",
            "DelayEngine",
            "delay_engine",
            "IsDelayMode",
            "DelayTriggeredThisBlock",
            "mode_xfade",
            "wet_alt",
        )
        for relative in paths:
            text = (ROOT / relative).read_text()
            for symbol in forbidden:
                self.assertNotIn(symbol, text, f"{symbol} remains in {relative}")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the contract and verify RED**

Run: `python3 -m unittest tests/test_no_delay_mode.py -v`

Expected: failures for existing delay files and symbols.

### Task 2: Collapse BeadsProcessor to the grain path

**Files:**
- Modify: `src/vendor/beads_dsp/include/beads/parameters.h`
- Modify: `src/vendor/beads_dsp/include/beads/beads.h`
- Modify: `src/vendor/beads_dsp/src/beads_processor.h`
- Modify: `src/vendor/beads_dsp/src/beads_processor.cpp`
- Test: `tests/test_no_delay_mode.py`

- [ ] **Step 1: Remove delay mode from public parameters and API**

Delete `BeadsParameters::delay_mode`, `BeadsProcessor::IsDelayMode()`, and `BeadsProcessor::DelayTriggeredThisBlock()` declarations and definitions. Keep grain-count, grain-trigger, auto-gain, buffer, and scale APIs unchanged.

- [ ] **Step 2: Remove delay engine and transition state from Impl**

Delete the delay-engine include and member. Delete `delay_mode`, `prev_delay_mode`, `kModeXfadeSamples`, `mode_xfade_counter`, and `wet_alt_buf`. Keep `wet_buf` and quality-transition state.

- [ ] **Step 3: Remove delay initialization and mode switching**

Delete `impl_->delay_mode = false`, `delay_engine.Init(...)`, and the `params.delay_mode` transition block in `SetParameters()`. Keep `feedback_sample` initialization plus recording buffer, grain engine, quality, saturation, reverb, auto-gain, and feedback-filter initialization.

- [ ] **Step 4: Collapse input and feedback processing**

Replace conditional auto-gain with:

```cpp
in = s.auto_gain.Process(in, s.params.manual_gain_db, s.params.auto_gain);
```

Keep only the current grain feedback branch:

```cpp
float source_scale = 1.0f - s.smoothed_feedback * 0.5f;
in.l *= source_scale;
in.r *= source_scale;
StereoFrame mixed = in + fb * (s.smoothed_feedback * s.smoothed_feedback);
in = s.saturation.LimitFeedback(mixed, s.params.quality_mode);
```

- [ ] **Step 5: Collapse wet rendering and selection**

Keep quality pitch modulation, call only:

```cpp
float pitch_mod = s.quality_processor.GetPitchModulation(s.params.quality_mode, block);
s.grain_engine.SetPitchModulation(pitch_mod);
s.grain_engine.Process(s.params, wet, block);
```

Inside output processing initialize `StereoFrame wet_frame = wet[i];` and remove all mode-crossfade selection logic.

- [ ] **Step 6: Run the no-delay symbol test**

Run: `python3 -m unittest tests/test_no_delay_mode.py -v`

Expected: symbol test passes; file-removal test still fails until Task 3.

### Task 3: Delete delay implementation and build/test references

**Files:**
- Delete: `src/vendor/beads_dsp/src/delay/delay_engine.h`
- Delete: `src/vendor/beads_dsp/src/delay/delay_engine.cpp`
- Delete: `tests/beads/test_delay.cpp`
- Modify: `metamodule/CMakeLists.txt`
- Modify: `tests/README.md`
- Test: `tests/test_no_delay_mode.py`

- [ ] **Step 1: Delete the engine and dedicated tests**

Delete the three files listed above. Do not remove the generic recording-buffer implementation used by GrainEngine.

- [ ] **Step 2: Remove build and test-document references**

Delete the explicit `delay_engine.cpp` source line from `metamodule/CMakeLists.txt`. Remove `test_delay.cpp` from the vendored-suite list in `tests/README.md`; do not edit any module manuals.

- [ ] **Step 3: Run the contract and verify GREEN**

Run: `python3 -m unittest tests/test_no_delay_mode.py -v`

Expected: 2 tests pass.

- [ ] **Step 4: Verify user manuals are excluded**

Run: `git status --short Loooop.md MF20.md Particules.md`

Expected: all three remain unstaged modifications.

- [ ] **Step 5: Commit the refactor only**

```bash
git add tests/test_no_delay_mode.py \
  src/vendor/beads_dsp/include/beads/parameters.h \
  src/vendor/beads_dsp/include/beads/beads.h \
  src/vendor/beads_dsp/src/beads_processor.h \
  src/vendor/beads_dsp/src/beads_processor.cpp \
  src/vendor/beads_dsp/src/delay/delay_engine.h \
  src/vendor/beads_dsp/src/delay/delay_engine.cpp \
  tests/beads/test_delay.cpp metamodule/CMakeLists.txt tests/README.md
git commit -m "refactor: remove unreachable delay mode"
```

### Task 4: Verify granular behavior and both host builds

**Files:**
- Verify: `tests/**`
- Verify: `vcv/**`
- Verify: `metamodule/**`

- [ ] **Step 1: Run complete regression suites**

Run:

```bash
./tests/run.sh
./tests/beads/run.sh
python3 -m unittest tests/test_robotboy_identity.py tests/test_no_delay_mode.py -v
```
