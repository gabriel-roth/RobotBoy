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
