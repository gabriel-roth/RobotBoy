> **SUPERSEDED 2026-07-26** by correlation-aligned splices. The bounded
> per-fade ratio chase described below was built (commit 238616d), measured,
> and removed. It never engaged at any sweep speed a hand can produce (the
> 0.75-octave cap needs a sub-0.2 s full-travel twist), so it left the sweep
> garble in the "Problem" section completely untouched — the metrics came out
> bit-identical to main on every 2 s and 0.4 s sweep — while adding ~12 fade
> cycles of lag to an instant retarget (CV jump / clock change), violating the
> one-fade-cycle responsiveness requirement. The problem statement below is
> still accurate and still the reason the work was done; only the mechanism
> changed. See `.superpowers/sdd/crossfade-variants-report.md` for the variant
> comparison and `AlignedFadeTarget` in
> `src/retours_delay/dsp/src/engine/echo_engine.cpp` for what shipped.

# Retours: bounded per-fade target chase in Crossfade mode

**Date:** 2026-07-26. **Status:** approved design, prototype-first (worktree
`RobotBoy-crossfade-chase`, branch `retours-crossfade-chase`; user listens
before any merge decision).

## Problem (measured 2026-07-26, harness in session scratchpad)

Free-running, the Interval knob maps 11 octaves of delay into each half of
its travel (`base_time.cpp:189-191`). Crossfade mode retargets on a fixed
cadence — one fade is `kJumpCrossfadeFrames = 1024` frames (\~21 ms at 48 kHz,
decimation 1) and knob motion mid-fade only replaces `queued_target_`. A
moderate 2 s knob sweep near noon therefore jumps the delay 66–104 ms per
21 ms fade cycle (a fast twist: 180–490 ms), so each crossfade blends two
wildly different moments of the buffer. Measured artifact rates (chirp
events/s, 220 Hz bursts): single-tap crossfade sweep 0.5, multi-tap 2.0,
tape reference 3.1, with fast sweeps ~20× worse on a chaos metric. This is
distinct from the known multi-tap glide and lives on the single-tap CCW
side too.

## Change

In `EchoEngine`'s Crossfade mode only: when a new fade starts (fresh target
or dequeued `queued_target_`), clamp the fade's destination to within a
maximum **ratio** step of the current effective delay, and keep chasing the
raw target through successive fades until converged.

- New constant in `types.h`: `kCrossfadeMaxStepOctaves = 0.75f` (prototype
  value; tune by ear). Per-fade multiplicative cap:
  `maxRatio = exp2f(kCrossfadeMaxStepOctaves)`.
- At fade-start in `SetTargets` (both the idle→fade path and the
  fade-complete→dequeue path in `ReadWet`): let `cur = delay_frames_`
  (current effective delay) and `want` = raw requested frames. The fade
  target becomes `clamp(want, cur / maxRatio, cur * maxRatio)`; if that
  differs from `want`, store `want` in `queued_target_` so the chase
  continues next fade. Guard `cur <= 0` (first-target sentinel, unfreeze
  `equiv_delay` snaps) — those paths snap as today, no chase.
- A full 11-octave traverse then takes ceil(11 / 0.75) ≈ 15 fades ≈ 320 ms —
  responsive, but each fade blends similar content.
- Tape mode, frozen paths, multi-tap flag, and the density mapping are
  untouched. Multi-tap's tap2 follows `delay_used` as before — the chase
  bounds its per-fade glide too, as a side effect (fine).

## Equivalence / tests

- Deliberate behavior change in crossfade mode: the pinning hash
  `pin_crossfade_retarget` in `tests/retours_delay_dsp/test_echo_engine.cpp`
  is expected to move IF its retarget jump exceeds the cap — verify the
  scenario, regenerate that one hash with a comment citing this spec, and
  confirm the OTHER four pins are bit-exact unchanged (tape/multi-tap/frozen
  paths must not move; the multi-tap pin runs on the tape... verify which
  mode each pin uses — any pin whose scenario never enters crossfade mode
  must stay bit-exact).
- New Catch2 tests: (1) per-fade step never exceeds the ratio cap during a
  large retarget (instrument via CurrentDelaySamples() across blocks);
  (2) chase converges: after a large retarget with no further knob motion,
  delay reaches the raw target within ceil(11/0.75)+2 fades; (3) small
  retargets (within the cap) behave exactly as before (single fade,
  bit-exact vs a captured pre-change hash if convenient, else behavioral).
- Suites: tests/retours_delay_dsp/run.sh, tests/run.sh, both builds.

## Out of scope

Density-curve reshaping; multi-tap changes; any tape-mode change; manual
updates until the user approves the sound (then one line in Retours.md's
Time change response bullet).
