# Particules: drop automatic triggers at any saturated grain cap

**Date:** 2026-07-26. **Status:** approved for implementation (VCV listening
pass gates the MetaModule build and merge). Branch
`worktree-particules-midrange-drop`.

## Problem

The 2026-07-25 long-grain change (see
`2026-07-25-particules-longgrain-trigger-drop-design.md`) drops automatic
triggers instead of stealing, but only at the cap floor
(`cached_max_active_ == 2`, grain duration > half the buffer, \~4:10 on the
Size knob). Below that floor the July 11 steal-and-replace policy still
runs, and at high Density it produces exactly the churn the cap-floor change
eliminated at max Size — over a wide midrange of the Size knob:

- Once `density_rate x grain_duration` exceeds the dynamic cap
  (`clamp(1.5 x buf_dur / grain_dur, 2, 30)`), **every** automatic trigger
  steals the oldest grain. Effective grain lifetime collapses to
  `cap / trigger_rate` regardless of Size — e.g. \~23 ms of a nominal 1.8 s
  grain at max Density, 4 o'clock Size, Bright quality.
- Heard as: crunchy/spiky truncation churn from melodic input, and a large
  level drop (bell/reversed Shape envelopes are killed a few percent into
  their attack, so grains never get loud).
- The weird zone spans \~1:00–1:25 (steal onset, quality-dependent) to
  \~4:10 (cap floor, where yesterday's drop rule abruptly cleans everything
  up — a hard seam).

Reported on MetaModule 2026-07-26, but host-independent (shared DSP;
verified pre-existing in the 2026-07-25 spec).

## Decision

**Generalize the cap-floor rule to every cap value.** In
`GrainEngine::Process`'s spawn loop (`grain_engine.cpp`), when a trigger
finds the pool saturated, an automatic (droppable) trigger is dropped — no
steal, no spawn — at ANY cap, not just `cached_max_active_ == 2`. Manual
triggers (kGated rising edge, all kClocked ticks, kMidi) steal exactly as
today, through the existing pending-kill / pool-full-hard-replace paths.

Every grain therefore plays its full envelope; the effective birth rate
self-saturates at \~`cap / grain_duration`; both discontinuities (steal
onset and the 4:10 seam) disappear because one rule covers the whole Size
range.

### Startup ramp: fold into the single rule

The 2026-07-25 change deliberately gated its drop on the *unramped*
`cached_max_active_` so the 1-second startup ramp kept stealing. That
carve-out is removed: the drop condition is now simply "this droppable
trigger found the pool saturated" (i.e. the `!g` branch after
`AllocateGrain`), which includes saturation against the ramped cap during
the first second after Init. Rationale: with drops the norm at every cap,
the carve-out no longer preserves anything meaningful — short grains die
so fast the ramp barely saturates, and for long-grain patches dropping at
load is precisely the desired voicing. This deliberately supersedes the
ramp clause of the 2026-07-25 spec.

### Accepted trade-offs (decided 2026-07-26)

- Density's outer range goes inert at large Sizes: past the saturation
  point, turning Density further changes nothing. The headroom it appears
  to promise never existed — churn was fake density.
- In random (CW) Density mode, drops make surviving grains more regularly
  spaced than the exponential inter-grain distribution intends, within the
  saturated zone only.
- Old steal behavior is replaced outright — no menu option, no param, no
  patch-compat surface (same call as the 2026-07-25 change).

## Fallback if the listening pass finds the zone too static

**Option B — steal past the envelope peak:** an automatic trigger at
saturation may steal only if the oldest grain's envelope phase is past its
peak (`slope_`); otherwise drop. Keeps Density livelier (effective rate up
to \~2x) at the cost of audible tail truncation under pressure and
shape-dependent behavior (degenerates to plain drop for Reversed shape, to
today's churn for Rectangle). Reuses all of this change's plumbing. Not
built now; pre-agreed as the next iteration if needed.

Rejected alternatives (see conversation 2026-07-26): probabilistic
steal/drop blend (keeps both symptoms, nondeterministic tests); clamping
grain duration under saturation (silently breaks the Size knob); longer
steal fades (level collapse remains); raising the cap (CPU, and demand
exceeds any affordable cap \~10x).

## Tests (tests/particules_dsp, Catch2)

- **Invert the mid-size pin:** the existing `size = 0.5` test asserting
  steal-path behavior at saturation now asserts drops for automatic
  triggers (no new spawn serials, no pending-kills once saturated).
- **Cap-floor tests unchanged:** the 2026-07-25 drop tests must stay green
  as-is (they are a subset of the general rule).
- **Manual steal at mid size:** gated rising edge and clocked tick each
  still replace the oldest grain at a saturated mid-size cap.
- **Ramp window:** during the first-second startup ramp, a saturated
  automatic trigger drops (documents the superseded carve-out).
- Existing suite stays green.

## Addendum 2026-07-26: upward cap slew (Size-sweep CPU spike)

**Problem (user report, MetaModule listening pass):** moving Size quickly
spikes CPU. Benchmark (`GrainEngine` driven offline at max Density, Bright
4 s buffer): fast Size sweeps transiently carry 17–30 active grains into
knob regions whose steady state is 2–9 grains — grains spawned while the
knob passes high-cap territory (the \~27-grain steady-state peak near
Size ≈ 0.29) persist for their full multi-second durations after the cap
has collapsed. Pre-existing (the pre-change engine measured worse:
mean 21–23 active during sweeps vs 12–15 after the drop rule); newly
audible-on-the-meter because the zone is now musically usable.

**Change:** `max_active` becomes a slewed value. The per-block target is
`cached_max_active_` as before; the effective cap falls to the target
immediately (shrinking never kills grains — it only tightens the spawn
gate) but rises toward it at a bounded 28 grains/s. Spawning during and
after fast sweeps is therefore throttled to the slew line instead of
refilling instantly to the pool max. Parked-knob steady states are
unchanged.

The slew **replaces the 1-second startup ramp** (`startup_samples_
remaining_`): Init seeds the slew at the cap floor of 2, giving the same
patch-load protection (2 → 30 in \~1 s at the same 28 grains/s), one
mechanism instead of two. Small intentional difference: low targets are
now reached sooner (fixed rate, not proportional-over-1 s) — e.g. a
target of 9 in \~0.25 s instead of 1 s.

**Accepted trade-off:** after a fast Size move (or preset jump), texture
re-thickens over up to \~1 s instead of instantly — same spirit as the
startup ramp. Size **CV** modulation is unaffected: the cap has always
been computed from the raw knob (`params.size`), not the AR/CV-modulated
per-grain size.

## Out of scope

Cap formula (steady states), envelope shapes, kill fades, scheduler
changes, manual/docs wording, MetaModule build until after the VCV
listening pass.
