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

## 2026-07-15 — final whole-branch review: Ready to merge

- Independent final review (fresh reviewer, whole branch b67a0b6..HEAD)
  found two real bugs the per-task gates missed, both fixed in `01afee3`
  with RED-verified regression tests: (1) a NaN on any CV input
  permanently killed the DSP (slew/shifter state poisoned; now sanitized
  at adapter, SetParameters, SetTargets, and SetRatio); (2) unfreezing
  from a wrapped slice hard-snapped the read head instead of slewing
  (equiv_delay now wrapped).
- Also restored the plan's >1 runaway feedback via a piecewise knob map
  (unity at 0.9, 1.1 at max — reviewer verified continuity empirically:
  decay ratios 0.988/0.999/1.009 at knobs 0.89/0.90/0.91), applied input
  trim to the dry path per the spec diagram, removed dead code, and added
  kCrossfade + envelope_pre_feedback coverage. 54 test cases, all green;
  both builds clean.
- Re-review verdict: **Ready to merge.** Branch left unmerged for
  Gabriel's checklist pass; the Beads-manual-PDF-in-history question is
  cheapest to settle before merging.
- VCV plugin (with all fixes) installed to the Rack2 plugins dir.

## 2026-07-15 — final review fixes

Full-branch review of Échos (post Task-13) surfaced 7 findings. All fixed
in one pass, verified, docs/checklist updated.

1. **NaN CV inputs permanently kill the DSP (critical).** A NaN on any CV
   jack reached `EchoEngine::delay_frames_`/`target_frames_` (the tape-mode
   slew `state += coeff*(target-state)` never recovers from NaN),
   `RotaryShifter::phase_` (wrap checks `>=1`/`<0` are never true for NaN),
   and `smoothed_dry_wet`/`smoothed_feedback` — permanently, even after the
   glitch input returned to normal. The existing first-target snap guard
   (`delay_frames_ < 0.f`) didn't catch it (NaN fails every comparison).
   Fixed in three layers: (a) `Echos.cpp updateSlowParams` sanitizes all six
   CV reads to 0 V if non-finite; (b) `EchosProcessor::SetParameters` now
   guards every float field of `EchosParameters` (mirrors
   `particules_processor.cpp`'s pattern), `EchoEngine::SetTargets`'s
   first-target snap also fires on `!isfinite(delay_frames_)`, and
   `RotaryShifter::SetRatio` sanitizes its input. New regression test
   (`test_hardening.cpp`, "NaN CV input for one block does not permanently
   poison the DSP") glitches `density_cv` then `dry_wet_cv` for one block
   each, mid-stream, then asserts full recovery (finite output, correct
   delay time, impulse echoes on time). **Confirmed red** against the
   unfixed `SetParameters` (temporarily reverted the guard block): the tail
   of the run stayed non-finite; green after restoring.

2. **Unfreeze from a wrapped slice snapped instead of slewing (important).**
   `EchoEngine::NotifyFreeze`'s falling edge computed
   `equiv_delay = read_subsample_ - frozen_read_pos` unwrapped. A high slice
   index (TIME=1.0) anchors `slice_start_` just past the write head once
   wrapped (since `slice_count*base` sits just under the buffer duration),
   making `equiv_delay` negative — which then satisfied `SetTargets()`'s
   "first-ever target" sentinel (`delay_frames_ < 0.f`) on the very next
   block, snapping straight to the live TIME target instead of continuing
   the slew. Fixed by wrapping `equiv_delay` into `[0, size)` via the
   existing `WrapPosition` helper (both tape and crossfade branches). New
   regression test (`test_freeze_slicer.cpp`, "unfreeze from a wrapped
   high-slice-index position slews, doesn't snap"). **Confirmed red**: the
   unfixed code read `DelayTimeSeconds() == 0` right after unfreeze (the
   negative delay clamped by `CurrentDelaySamples()`'s `max(0.f, ...)`) then
   `== 0.5` (the final target) just 3 blocks later — an instant snap, not a
   slew; green after the fix (still >1 s three blocks in, converges to 0.5 s
   given enough time).

3. **Feedback knob range restored to 0→1.1 (plan restoration).** The plan
   called for feedback >1 ("runaway", signature Beads self-oscillation
   behavior); the code clamped to 1.0. Restored a piecewise mapping in
   `echos_processor.cpp`: knob k≤0.9 → gain k/0.9 (0..1, unity at k=0.9);
   k>0.9 → gain 1.0+(k-0.9) (up to 1.1). The per-quality
   `Saturation::LimitFeedback` still bounds the loop regardless. Test
   fallout: `test_echo_engine.cpp`'s Q-bug regression ("high feedback decays
   instead of self-oscillating") used knob 0.95, which now maps *above*
   unity (1.05 gain) by design, so it no longer isolates the Q bug from the
   new intentional-runaway behavior. Moved to knob 0.87 (gain ≈0.967, still
   sub-unity) after probing several values with a small harness: the brief's
   suggested 0.85 turned out NOT to discriminate reliably at this fixture's
   burst amplitude (both Q=1 and Q=0.707 decayed there; the growth/decay
   knife-edge sits nearer knob 0.853). At 0.87 the separation is stark
   (Q=1: late RMS 0.72 vs. early 0.002, grows to clip; Q=0.707: late RMS
   0.0000, fully decays) — **confirmed red** with `SetQ(0.707f)` temporarily
   removed, green restored. The two feedback=1.0 tests the brief flagged
   (`test_hardening.cpp` corner stress, `test_quality_modes.cpp` (c)) were
   re-run as-is (now exercising gain 1.1 instead of 1.0) and both still pass
   unmodified — the limiter's boundedness/finiteness guarantee holds at the
   higher gain, no threshold changes needed. New test added
   (`test_echo_engine.cpp`, "feedback knob at max (gain 1.1) sustains/grows
   and stays bounded"): a burst at knob=1.0 sustains/grows to the limiter
   ceiling over 5 s (tail RMS ≥ RMS at 1 s) and stays within the documented
   ±2.0 headroom bound. User checklist updated with an ear-check item for
   the 0.9–1.0 knob range.

4. **Input trim didn't reach the dry path (important).** `input_gain`
   multiplied only the path written to the delay buffer; the spec puts trim
   at the very front of the signal flow, before the dry/wet split. Fixed by
   using the already-computed `trimmed_l`/`trimmed_r` (not raw `input.l/r`)
   in the final dry/wet mix. Default trim (0 dB, gain 1.0) is a no-op, so no
   existing test's expectations changed. New test
   (`test_processor_basics.cpp`, "input trim also applies to the dry tap"):
   -12 dB trim, dry_wet=0 → output ≈ input × 0.2512.

5. **Dead code removed.** `EchosProcessor::Impl::wet_buf`
   (`echos_processor.h`) and `RotaryShifter::Bypassed()`
   (`pitch/rotary_shifter.h`/`.cpp`) had zero callers (grep-confirmed before
   removal; MetaModule's symbol map showing the compiled function was just
   the pre-removal binary, not a live caller).

6. **Coverage gaps closed.** (a) `TimeChangeMode::kCrossfade` had no
   dedicated test: added "retarget mid-stream declicks and reaches the new
   target" (no sample-to-sample jump ≥0.5 across the retarget boundary with
   a 0.3-amplitude sine, lands on the new target once the 1024-sample fade
   completes) and "retarget during an in-progress fade queues cleanly" (a
   second retarget mid-fade queues via `queued_target_` rather than
   corrupting state; stays finite, eventually reaches the queued target).
   (b) `envelope_pre_feedback` had no dedicated test (ledger T5): added
   "envelope_pre_feedback sustains repeats measurably longer" — tail RMS
   with the flag true vs. false differs by ≥2x (T5's own probe measured
   ~21x; 2x is a robust floor, not a pin to that exact number).

7. **Hygiene.** `tests/echos/mm_sim/*.wav` added to `.gitignore`
   (reproducible render fixtures, were showing as untracked). User
   checklist's decision-log section gained two more items: the
   in-repo-tracked Beads manual PDF's public-history exposure, and the
   pitch-notch-map (0/±7/±12/±19) vs. the spec's "±5" mention.

**Verification:** `tests/beadsdelay_dsp` 54 cases (was 47), `tests/particules_dsp`
and root `tests/` lanes unaffected and still green, `make -C vcv -j8` and
`cmake --build metamodule/build` both clean.
