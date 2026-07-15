# beads-delay worklog

Running notes for the autonomous beads-delay build (branch
`worktree-beads-delay`). Newest entries at the bottom.

## 2026-07-15 — research & spec

- Sources gathered: Beads manual (tracked at
  `resources/Mutable-Beads-manual.pdf`), transcripts of both YouTube
  videos (scratchpad), full survey of `~/Dev/4ms-vcv` MetaModule DSP
  techniques → `docs/superpowers/specs/2026-07-15-beads-delay-mm-optimization-notes.md`.
- Found large reusable infrastructure in `src/particules/dsp/`
  (RecordingBuffer with Hermite reads + freeze declick + write decimation,
  QualityProcessor with the 4 Beads quality characters, Saturation with
  per-quality feedback limiting, Attenurandomizer, Random, Svf). The spec
  builds the new core on these.
- Old Particules delay engine was removed 2026-07-07 as unreachable dead
  code; per instructions it is ignored entirely (not consulted).
  `tests/test_no_delay_mode.py` guards against its return — will be
  rescoped to Particules files only.
- Spec written: `docs/superpowers/specs/2026-07-15-beads-delay-design.md`.

### Decisions Gabriel should review on return

1. **Module name**: slug `Echos`, display **Échos** (family consistency
   with Particules/Ondes; avoids MI's "Beads" name). Rename is trivial
   until patches exist — say the word.
2. Dedicated FEEDBACK/BLEND CV jacks (no assignable-CV button).
3. Always-stereo buffer; quality→duration = 4/8/32/16 s (Particules
   mapping) instead of hardware mono column.
4. Reverb, AGC, buffer persistence: out of scope for v1.
5. Context-menu selectables where behavior was ambiguous: time-change
   response (tape doppler vs crossfade), envelope feedback tap point,
   sliders for slew rate / random-LFO rate / input trim.

## 2026-07-15 — MM smoke test

- `metamodule/CMakeLists.txt`: added `src/beadsdelay/Echos.cpp` +
  the 4 beadsdelay DSP `.cpp` files (`echos_processor.cpp`,
  `time/base_time.cpp`, `engine/echo_engine.cpp`,
  `pitch/rotary_shifter.cpp`; `mod/`/`env/` are header-only). Needed two
  include dirs beyond the brief — `src/particules/dsp/src` and
  `src/beadsdelay/dsp/src` — to find Particules' `RecordingBuffer` header
  (lives under `dsp/src/buffer/`, not `dsp/include/`), matching
  `vcv/Makefile`'s existing flags for the same reason.
- `.mmplugin` build: clean, `RobotBoy.mmplugin` (695 KB) produced.
- Headless simulator built-in build: clean, no workarounds needed (the
  Yellowjacket task's `-DSIMULATOR` gap is already fixed in the simulator
  repo).
- Simulator smoke test: single cabled `Echos` instance, DENSITY=0.409091 +
  TIME=0 (manual mode, unclocked) → predicted exactly 1.000 s delay
  (`4.0s buffer * 2^(-11 * 2/11) * 2^(4*0) = 1.0s`). Fed a 20 ms burst;
  measured echoes at 1.010/2.010/3.010 s (1.000 s spacing) decaying at
  ratio 0.300/repeat, matching FEEDBACK=0.3 exactly. 0 NaN, 0 Inf, no
  crash across a 4 s render.
- CPU: **~0.19% of one host core** (100 s sim, single cabled instance,
  average of 3 runs: 191/187/187 ms). Comfortably under the ~15%
  threshold — no optimization pass applied. (A 16-instance uncabled
  load-test cross-check gave inconsistent/non-monotonic numbers on this
  simulator version — see `tests/echos/mm-sim-notes.md` §5 for the
  discrepancy; the consistent cabled single-instance number was trusted
  instead.)
- `python3 -m unittest tests.test_no_delay_mode` and `tests/run.sh`: both
  green (unaffected — the guard test is scoped to Particules-only files).
- Full detail, patch/WAV fixtures, and regeneration commands:
  `tests/echos/mm-sim-notes.md`, `tests/echos/mm_sim/`.

## 2026-07-15 — verification sweep (Task 13, final)

### Test sweep — all green

| Lane | Result |
|---|---|
| `tests/run.sh` (mf20/loooop/particules + python guards) | all passed (5 python tests OK) |
| `tests/beadsdelay_dsp/run.sh` | 47 test cases, 2 363 113 assertions, all passed |
| `tests/particules_dsp/run.sh` | all passed |
| `tests.test_no_delay_mode` + `tests.test_robotboy_identity` | 4 tests OK |
| `make -C vcv -j8` | clean |
| `cmake --build metamodule/build` | clean, `RobotBoy.mmplugin` produced |

(beadsdelay_dsp was 46 cases at Task 12; the 47th is the regression test
added below.)

### Demo renders — MM headless simulator, one real bug found and fixed

**Host choice:** the plan called for the VCV headless host, but Échos (and
Particules — verified, so not a regression) segfault there:
`~/Dev/vcv-headless` provides no engine Context and both modules call
`APP->engine->getSampleRate()` in their constructors — the host skill's
documented "live engine Context unsupported" case (MF-20 runs fine). Used
the MM headless simulator instead, as the task brief allowed. The sim has 2
input channels and no knob automation, so SEED-clock and FREEZE-gate ride
WAV channel 1, and the freeze-slicer TIME "sweep" is three renders at
static TIME values. Fixtures: `tests/echos/mm_sim/demo_*.yml` +
`gen_demo_inputs.py`; listening WAVs in the session scratchpad
(`echos_demo/`), verified programmatically (`analyze_demos.py`, scratchpad).

**(a) Karplus-Strong** (DENSITY=0 → 2 ms base = 500 Hz, FEEDBACK=0.95,
8 ms noise burst): pluck rings at exactly 500 Hz (autocorr period 96
samples, normalized autocorr 0.95) — **but the first render grew instead of
decaying**. Isolated host-side against the DSP core: feedback 0.9 decays,
0.95 grows ~0.12%/cycle; FFT showed the growing component at ~30 Hz, not
500. **Root cause:** the feedback DC-block HP (SVF at 10 Hz) was left at
the SVF class's default Q=1, which peaks at ~1.155× above cutoff
(|H|≈1.05 at 30 Hz) — loop gain 0.95 × 1.05 > 1 → low-frequency
self-oscillation for any feedback ≥ ~0.87. Particules sets `SetQ(0.707f)`
on the same filter for the same reason; Échos's Init() omitted it. Fixed
(`echos_processor.cpp` Init, +comment), regression test added
(`test_echo_engine.cpp` "high feedback decays instead of self-oscillating",
confirmed red before / green after), all builds redone. Post-fix render:
500 Hz pluck, RMS 0.0025 → 0.0000 → 0.0000 at 0.2/0.8/1.6 s. Not a silent
fix — flagged here and in the user checklist for an ear-check.

**(b) Clocked echo** (2 Hz clock into SEED, DENSITY noon = 1/1, TIME 0,
FEEDBACK 0.5, burst at 0.6 s): echo peaks at 1.110/1.610/2.110/2.610/
3.110 s — 0.500 s spacing on the clock grid; repeat ratios
0.504/0.503/0.503/0.502 (expect ~0.5); silence between echoes; 0 NaN/Inf.

**(c) Freeze slicer** (DENSITY=4/11 → 0.5 s base → 8 slices; 8 distinct
0.5 s tones 200–900 Hz recorded, freeze gate at 4.0 s; TIME = 0, 3/7, 1):
frozen output loops the correct slice — 900/600/200 Hz for slices 0/3/7
(slice k starts at (3.5 − 0.5k) s → 900 − 100k Hz), pitch constant across
three separate loop passes (clean seam looping), 0 NaN/Inf.

### State of the branch

- All 13 plan tasks complete; every test lane green; VCV plugin.dylib,
  `.mmplugin`, and Rack2-installed copy all rebuilt with the feedback-HP
  fix. CPU number from Task 12 (~0.19% host-relative) is unaffected (the
  fix changes one filter coefficient).
- Uncommitted-by-design: nothing; `tests/echos/mm_sim/audio_{in,out}.wav`
  remain untracked (reproducible, per mm-sim-notes).
- For Gabriel: `docs/superpowers/plans/2026-07-15-beads-delay-user-checklist.md`
  — GUI checks, audible checks of the three demo patches, and the design
  decisions awaiting sign-off (module name Échos, MM access to the menu
  sliders, SlowRandomLfo shared wander, and the spec decision log).
