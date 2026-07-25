# Particules: drop automatic triggers at the long-grain cap floor

**Date:** 2026-07-25. **Status:** shipped as the new default behavior
2026-07-25 after a user listening pass (developed prototype-first on branch
`particules-longgrain-drop`; no menu option — replaces the old steal
behavior outright).

## Problem

At maximum Size, grain duration approaches the full buffer (\~4 s in Bright)
and the CPU grain cap (`buf_dur / grain_dur * 1.5`, floor 2) collapses to 2.
The engine's saturation policy is steal-and-replace ("newest events always
sound", `grain_engine.cpp` spawn loop): every density tick kills the oldest
grain at a zero crossing and starts a new one at a fresh random position. At
the cap floor this means nominally buffer-length grains only ever play for
one trigger period — constant mid-grain truncation churn, heard as a noisy,
spiky texture. Verified pre-existing (identical on pre-optimization main to
±1 LSB); this is a voicing change, not a bug fix.

## Change

1. **Trigger tagging.** `GrainScheduler::Process` gains a parallel output
   `bool droppable[]`, same indexing as `trigger_samples[]`. Automatic
   (droppable = true): `kLatched` phasor ticks and `kGated` held-repeat
   ticks. Manual (droppable = false, unchanged steal behavior): `kGated`
   rising edges, all `kClocked` ticks (dropping them would skip beats), and
   `kMidi` (inherited dead code — untouched semantics).
2. **Drop condition.** In `GrainEngine::Process`'s spawn loop, when a
   trigger finds `active_before >= max_active` it is **dropped** (no steal,
   no spawn) iff the trigger is droppable AND `cached_max_active_ == 2` —
   the cap floor, equivalent to grain duration > half the buffer. The gate
   uses `cached_max_active_`, not the startup-ramped `max_active`, so the
   1-second startup ramp (which pins the effective cap to 2 with short
   grains) keeps today's steal behavior.
3. **Everything else unchanged:** below the cap floor, the existing
   steal-and-replace runs exactly as before, including the pending-kill
   zero-crossing fade and the pool-full hard-replace path.

## Resulting behavior at max Size

Two buffer-length grains play out undisturbed; a new grain starts only when
one genuinely ends, or when the user hits Seed / a clock tick arrives (those
still steal). Silence gaps between grain endings and the next density tick
are natural and accepted.

## Tests (tests/particules_dsp, Catch2)

- At `size = 1.0` (cap floor), latched mode, once 2 grains are active:
  further density ticks change nothing (no new spawn serials, no
  pending-kills) — drops happening.
- Same state, gate rising edge (`kGated`): oldest grain replaced — manual
  triggers still steal.
- At mid size (`size = 0.5`): spawn behavior identical to before the change
  (steal path intact when saturated).
- Existing suite stays green.

## Out of scope

Menu option / patch persistence (decide after listening); MetaModule build
(same DSP, but the prototype install is VCV only); any change to the cap
formula or grain envelopes.
