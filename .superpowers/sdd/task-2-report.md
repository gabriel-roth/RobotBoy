# Task 2 Report: F2 — overdub start/stop write-gain ramps

## What I implemented

Followed the brief (`task-2-brief.md`) verbatim:

- `LoopEngine.hpp`: added `odGain_` (write gain, default 1.f), `odGainStep_`
  (per-sample ramp increment; 0 disables the ramp), `stopPending_` (stop-ramp
  armed flag).
- `LoopEngine.cpp` `toggleRecord()`: rewritten into four cases — start
  overdub (ramp 0→1, or instant at `xfadeSamples_==0`), stop the initial
  pass (freezes immediately, behavior unchanged from before this task),
  request an overdub stop (arms/re-arms `stopPending_`; `recording_` stays
  true until the ramp completes), and the legacy instant-stop path when
  `xfadeSamples_==0`.
- `process()` overdub branch: write expression extended to
  `buf = old + odGain·(fb·fbSrc − old) + odGain·in`, applied write-then-advance;
  after the write, if a stop is pending, ramp `odGain_` down and clear
  `recording_` when it reaches 0 (re-arming `stopPending_` restarts the
  down-ramp from wherever the gain currently sits); otherwise ramp `odGain_`
  up toward 1 if it hasn't settled yet.
- `clear()` and `reset()`: both now also reset `stopPending_ = false`,
  `odGain_ = 1.f`, `odGainStep_ = 0.f`, so neither can leave a pending stop
  ramp armed on a re-armed engine.

No public API changes; `LoopEngine` stays Rack-free (no new includes touch
Rack headers).

## Testing

Added the three tests from the brief verbatim, registered in `main()`:
`test_overdub_ramps_declick`, `test_stop_ramp_rearm`,
`test_ramp_layer_combined_expression`.

### TDD Evidence

**RED** — command:
```
cd tests && mkdir -p ../build/tests && g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules -I../src/vendor/beads_dsp/include -o ../build/tests/test_loop_engine loooop/test_loop_engine.cpp ../src/loooop/dsp/LoopEngine.cpp && ../build/tests/test_loop_engine
```
Failing output (9 failures, tests added but production code not yet changed):
```
FAIL: ramps: still recording right after stop toggle
FAIL: ramps: still recording 239 samples into stop ramp
FAIL: ramps: first overdub sample at gain 0
FAIL: ramps: up-ramp midpoint
FAIL: ramps: down-ramp midpoint
FAIL: rearm: dip bottoms out ~0.5, no snap to 0
FAIL: rearm: recovered to full gain
FAIL: combined: mid-up-ramp expression
FAIL: combined: mid-stop-ramp expression

9 failure(s)
```
Expected and confirmed: without the ramp, `toggleRecord()` stops recording
instantly and the buffer holds full-gain (unramped) values, so every
gain-envelope and isRecording-during-ramp assertion fails. (The steady-state
assertions like "full gain after up-ramp" and "zero at ramp end" passed
incidentally — they only check the settled values, which happen to match
even the instant-toggle behavior.)

**GREEN** — same command after implementation: `All tests passed` (all 9
previously-failing assertions now `ok:`; all pre-existing assertions in the
file, including every 10 Hz overdub-write-mode test, still `ok:`).

### Full lanes (all green)

- `bash tests/run.sh` — all C++ and Python guard tests pass (`All tests
  passed`, `all passed`, `OK` for the 4 Python guard tests).
- `make -C vcv -j8` — links `plugin.dylib` successfully (only pre-existing
  `-Wdeprecated-this-capture` warnings from Rack SDK `helpers.hpp` menu-lambda
  captures in `Lop.cpp`/`Loooop.cpp`, unrelated to this change; no new
  warnings from `LoopEngine.cpp`/`.hpp`).
- `cmake --build metamodule/build -j8` — builds, `All symbols found!`,
  `RobotBoy.mmplugin` produced.

## Files changed

- `src/loooop/dsp/LoopEngine.hpp`
- `src/loooop/dsp/LoopEngine.cpp`
- `tests/loooop/test_loop_engine.cpp`

(`.superpowers/sdd/progress.md` also shows modified in `git status` — that
diff records Task 1's completion and predates this task; I did not touch it.)

## Self-review

- Confirmed `clear()`/`reset()` both zero `stopPending_`/`odGain_`/
  `odGainStep_` alongside `recording_` — a pending stop ramp cannot survive
  either call.
- Confirmed write-then-advance ordering: the gain used for a given write is
  read before the post-write ramp step advances it, matching what the tests
  assume (the first overdub sample lands at gain 0, not at the first ramp
  step past 0).
- Confirmed the buffer-ceiling auto-end path (the `loopLen_ == 0` branch of
  the recording `if` in `process()`) is untouched — the diff only touches the
  overdub `else` branch. `test_buffer_ceiling_autoend` still passes.
- Confirmed the 10 Hz exact-value tests (`test_write_mode_replace`,
  `test_write_mode_layer`, `test_write_mode_decay_at_low_rate_matches_layer`,
  `test_overdub_sums`, `test_overdub_gate`, `test_write_mode_peaks_track_decay`,
  etc., all at `reset(10.f, ...)`) still pass unchanged: at 10 Hz
  `xfadeSamples_` rounds to 0, so `odGainStep_ == 0` and the legacy
  instant-toggle / gain-always-1 path is taken throughout — none of these
  were weakened.
- Confirmed `test_write_mode_decay_rolls_off_highs` (pre-existing, landed
  already in Task 1's commit in anticipation of this ramp) still passes: it
  only measures HF rolloff past sample 1000 of a 4800-sample loop (outside
  the 240-sample ramp zones) and waits 300 samples after the stop toggle for
  the down-ramp to finish before restarting the head.
- No test assertions were weakened or removed; only new assertions and new
  tests were added.
- No new public API surface; `LoopEngine` remains free of Rack dependencies.

## Issues or concerns

None. Implementation matches the brief exactly; all new and pre-existing
tests pass; all three build lanes are green.
