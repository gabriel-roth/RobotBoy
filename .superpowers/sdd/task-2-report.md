# Task 2 Report: beads grain-scheduler fix bundle

## Status: DONE

## Commit

`ebb5913` — "fix: grain kill fade reaches zero, oldest-grain victim, clocked-mode reset"
9 files changed, 425 insertions(+), 34 deletions(-).

## What changed, per fix

### (a) Kill-fallback fade never reaches zero — `grain.h`

`Grain::ApplyPendingKill()`'s fallback branch computed `fade = fallback_counter_ * kFallbackFadeInv`
*before* decrementing `fallback_counter_`. With `kFallbackFadeSamples == 4` this emitted 1.0, 0.75,
0.5, 0.25 and then hard-cut to silence on the next call — a 0.25-amplitude step (audible click).

Fix: decrement `fallback_counter_` first, then compute `fade` from the post-decrement value. This is
exactly the `(counter − 1)/kFallbackFadeSamples` semantics from the spec — the emitted sequence is now
0.75, 0.5, 0.25, 0.0, with the last *emitted* sample landing on exactly 0. The kill-check
(`if (--fallback_counter_ < 0)`) is unchanged in position relative to the rest of the function; only its
order relative to the fade computation moved.

The edit is confined entirely to the `if (fallback_fade_) { ... }` block inside `ApplyPendingKill()`. It
does not touch `ComputeEnvelope()` or the main per-sample envelope multiply in `Process()` — the normal
(non-fallback) envelope path is provably unaffected (see self-review below).

### (b) Kill-oldest kills an arbitrary grain — `grain_engine.h`/`.cpp`

`AllocateGrain()`'s full-pool fallback picked the first non-pending grain *by array index*. Array index
does not track spawn order: once a grain finishes, its slot frees up and gets reused by the very next
trigger, so a low array index can end up holding the *newest* grain while genuinely older grains sit at
higher indices.

Fix:
- `Grain` gained a `uint32_t spawn_serial_` field (+ `spawn_serial()`/`SetSpawnSerial()` accessors),
  reset to 0 in `Init()`.
- `GrainEngine` gained a private `uint32_t spawn_serial_ = 0` monotonic counter, reset in `Init()`.
- In `Process()`, immediately after a successful `g->Start(gp)`, the engine stamps
  `g->SetSpawnSerial(++spawn_serial_)`. This is the only call site that activates a `Grain` in
  production code (verified by grep — see below), so every activation path is stamped.
- `AllocateGrain()`'s full-pool branch now scans all `kMaxGrains` slots, skips grains already
  `pending_kill()`, and picks the one with the lowest `spawn_serial()` as the victim.
- Added a comment on the wraparound: `spawn_serial_` wraps at 2^32 (~4.29 billion grains); even at a
  sustained worst-case trigger rate (e.g. an audio-rate signal into the clock/gate input firing on every
  edge, tens of kHz) that's still on the order of a day of continuous play, and since grains live at most
  a few hundred ms, no two simultaneously-active grains can ever straddle a wrap — ordering self-heals
  immediately. (I did not commit to the spec's literal "~24h at 50 Hz" figure since that arithmetic
  doesn't check out — see Concerns.)

**Important finding — the fallback branch is unreachable via the normal signal path as currently wired.**
`GrainEngine::Process()` guards the trigger loop with `if (active_before >= max_active) break;` *before*
calling `AllocateGrain()`. Since `max_active` is always clamped to `<= kMaxGrains` and `active_before` is
a real grain count also bounded by `kMaxGrains`, the moment the pool is genuinely full (`active_before ==
kMaxGrains`), `max_active <= kMaxGrains == active_before`, so the outer check always breaks *before*
`AllocateGrain()` is called again — the "no free slot" branch inside `AllocateGrain()` can never execute
through `Process()`. This is a pre-existing structural issue, separate from the three assigned fixes, and
out of scope to change here (see Concerns). To make the victim-selection logic testable at all, I added a
test-only accessor `GrainEngine::ForceAllocateGrainForTest()` that calls the private `AllocateGrain()`
directly, bypassing the outer cap — this drives exactly the same branch a real overflow trigger would
reach if the cap didn't preempt it.

### (c) Scheduler mode-change off-by-one + dead state — `grain_scheduler.h`/`.cpp`, `parameters.h`

`gate_phase_` is reused across trigger modes for different purposes: a continuous 0..1 phasor in
`kGated` (which the CW-density random re-latch branch can set as low as -2.0), an integer clock-division
counter in `kClocked`, and a repeat-rate phasor in `kMidi`. Nothing reset it (or `prev_gate_`/
`prev_clock_`) on a mode change, so switching modes mid-performance carried stale state from one mode's
interpretation into another's.

Fix:
- Added `TriggerMode prev_trigger_mode_ = TriggerMode::kLatched;` (matches `BeadsParameters`' default and
  `Init()`'s all-zero state, so the very first `Process()` call never spuriously looks like a mode
  change).
- At the top of `Process()`, before the `switch`, unconditionally reset `latched_phase_`, `prev_gate_`,
  `gate_phase_`, and `prev_clock_` whenever `params.trigger_mode != prev_trigger_mode_` — for *any*
  transition, not just gated→clocked.
- Deleted the dead clock-period measurement: `clock_period_` was written (directly, or smoothed via
  `OnePole`) on every `kClocked` rising edge but never read anywhere outside that same
  measure-and-smooth block; `samples_since_clock_` existed solely to feed that computation. Deleted both
  fields and the associated `max_samples`/increment/measurement code from the `kClocked` case. Confirmed
  via grep that after deletion there are zero remaining references to either symbol anywhere in the repo.
- Deleted `BeadsParameters::clock_interval_samples` (grep-confirmed: the only occurrence anywhere in the
  repo was its own declaration — no reader, no writer, not even in the deleted scheduler code, which used
  its own private `clock_period_`/`samples_since_clock_` rather than this parameter).

## TDD evidence

Wrote the three tests first, confirmed RED (either compile failure for missing accessors, or a genuine
logic failure against a temporarily-reverted single-fix), then implemented and confirmed GREEN.

1. **Compile-time RED (pre-implementation):** with only the test files added and no production changes,
   the build failed with 8 errors — `no member named 'ActiveAt'/'SpawnSerialAt'/'PendingKillAt'/
   'ForceAllocateGrainForTest' in 'beads::GrainEngine'` — confirming the tests actually depend on the new
   surface.

2. **Fix (a) RED, isolated:** with the fade computation reverted to the pre-fix order (fade before
   decrement) but everything else (including test accessors) in place:
   ```
   Grain: kill fallback fade reaches exactly zero
   test_grain_kill.cpp:87: FAILED:
     REQUIRE( fade_ratios[0] == Approx(0.75f).margin(1e-4f) )
   with expansion:
     1.0f == Approx( 0.75 )
   ```
   (old code's first fallback sample is full-gain 1.0, not 0.75 — confirms the sequence never reaches 0).

3. **Fix (b) RED, isolated:** with `AllocateGrain()`'s victim loop reverted to "first non-pending array
   index":
   ```
   GrainEngine: kill-fallback victim is the true oldest grain, not array slot 0
   test_grain_kill.cpp:211: FAILED:
     REQUIRE( pending_kill_index == true_oldest_index )
   with expansion:
     0 == 1
   ```
   (old code picks slot 0 — the newest grain, reused after grain #0 finished — instead of slot 1, the
   true oldest).

4. **Fix (c) RED, isolated:** with the mode-change reset block removed from `Process()`:
   ```
   GrainScheduler: switching gated -> clocked resets the division counter
   test_scheduler_clocked.cpp:115: FAILED:
     REQUIRE( edges_fired_on == 4 )
   with expansion:
     5 == 4
   ```
   (stale `gate_phase_` left over from a CW-density kGated run delays the first clocked division by one
   edge — 5th clock instead of 4th).

5. **GREEN:** with all three fixes restored, `tests/beads/build/beads_tests` (full suite, includes the
   pre-existing test cases plus the 3 new files): **All tests passed (503188 assertions in 144 test
   cases)**, including a clean from-scratch `rm -rf build && cmake -B build && cmake --build build -j &&
   ctest`.

Each isolated-revert/rebuild/retest/restore cycle was done with the fix files backed up to `/tmp` first
and diffed back to confirm an exact, byte-identical restore before moving to the next fix.

## Grep evidence for the `clock_interval_samples` deletion

Before deleting, ran:
```
grep -rn "clock_interval_samples" (repo-wide)
```
Single hit: the field's own declaration in `parameters.h`. No reader, no writer, anywhere in the
repo (not in `beads_processor.cpp`, not in `Particules.cpp`, not in any test). Safe to delete. Re-ran
after deletion — zero hits.

Also grepped `clock_period_`/`samples_since_clock_` after deletion — zero hits anywhere in `src`,
`metamodule`, or `vcv`.

## Files changed

- `src/vendor/beads_dsp/src/grain/grain.h` — fade-order fix; `spawn_serial_` field + accessors.
- `src/vendor/beads_dsp/src/grain/grain.cpp` — reset `spawn_serial_` in `Init()`.
- `src/vendor/beads_dsp/src/grain/grain_engine.h` — `spawn_serial_` counter; test-only accessors
  (`ActiveAt`, `PendingKillAt`, `SpawnSerialAt`, `ForceAllocateGrainForTest`).
- `src/vendor/beads_dsp/src/grain/grain_engine.cpp` — reset `spawn_serial_` in `Init()`; lowest-serial
  victim selection in `AllocateGrain()`; stamp serial at activation in `Process()`.
- `src/vendor/beads_dsp/src/grain/grain_scheduler.h` — `prev_trigger_mode_` field; deleted
  `clock_period_`/`samples_since_clock_`.
- `src/vendor/beads_dsp/src/grain/grain_scheduler.cpp` — mode-change reset at top of `Process()`; deleted
  dead clock-period measurement in the `kClocked` case; `Init()` sets `prev_trigger_mode_`.
- `src/vendor/beads_dsp/include/beads/parameters.h` — deleted `clock_interval_samples`.
- `tests/beads/test_grain_kill.cpp` (new) — fade-to-zero test; kill-oldest test.
- `tests/beads/test_scheduler_clocked.cpp` (new) — basic kClocked behavior test; division-by-4 test;
  gated→clocked mode-switch off-by-one test.

## Verification (all four lanes, from a clean rebuild)

- `cd tests/beads && ./run.sh` — pass. `rm -rf build` clean rebuild also verified separately: 144 test
  cases, 503,188 assertions, exit 0.
- `cd tests && ./run.sh` — pass (Loooop/Lop, MF-20, Particules CV-conditioning/pitch-notch suites all
  green; unaffected by this change but re-verified for no cross-talk).
- `make -C vcv -j8` — pass. Clean build; only pre-existing unrelated `-Wdeprecated-this-capture` warnings
  from Rack SDK `helpers.hpp` menu-lambda captures (in `Particules.cpp`'s SEED CV mode submenu), not from
  this change.
- `cmake --build metamodule/build -j8` — pass. "All symbols found!"; `.mmplugin` built successfully.

## Self-review

**(a) Does the fade change alter the NORMAL (non-fallback) envelope path?** No. The edit is entirely
inside the `if (fallback_fade_) { ... }` block of `ApplyPendingKill()`, which itself is only reached when
`pending_kill_` is already true (i.e., `StartPendingKill()` was called and the zero-crossing kill is in
progress). `ComputeEnvelope()` and the main envelope multiply in `Process()`
(`sample_l * env * gain_ * pan_l_`) are untouched. Verified by diff inspection and by the full pre-existing
test suite (including grain envelope-shape tests in `test_grain.cpp`) staying green.

**(b) Is the serial stamped on every activation path?** Yes — grepped for all `.Start(`/`->Start(` calls
on a `Grain` across `src/vendor/beads_dsp` and `src/particules`: the only production call site is
`grain_engine.cpp`'s `Process()`, immediately followed by `g->SetSpawnSerial(++spawn_serial_)`. There is
no second code path that activates a grain without stamping it. (Test files call `Grain::Start()` directly
without stamping, but those tests don't exercise spawn-order and aren't affected.)

**(c) Does the mode-change reset fire on all transitions, not just gated→clocked? Did deleting clock
state break kLatched/kGated?** The reset condition is `params.trigger_mode != prev_trigger_mode_` with no
mode-pair restriction — it fires for latched↔gated, latched↔clocked, gated↔midi, or any other pair,
symmetrically. `clock_period_`/`samples_since_clock_` were referenced *only* inside the `kClocked` case
block (confirmed by reading the full pre-fix `Process()` body); `kLatched` uses `latched_phase_` and
`kGated` uses `gate_phase_`/`prev_gate_`, neither of which touch the deleted fields. No breakage.

## Concerns (for final-review triage)

1. **`AllocateGrain()`'s kill-fallback branch is unreachable via `GrainEngine::Process()` given the
   current `active_before >= max_active` gate**, as detailed above under fix (b). This means fix (b)'s
   correctness has no coverage through the live audio-processing entry point without the added
   `ForceAllocateGrainForTest()` test hook — the hook is necessary but is itself evidence that the
   "kill-oldest" code path may not currently fire in real use at all (the CPU-based dynamic grain cap
   always intercepts trigger overflow first and silently drops the trigger with no grain marked for
   kill, buggy-oldest or otherwise). This is a distinct, pre-existing issue outside this task's three
   named fixes; flagging for whoever scopes a future task, since it may mean the click this whole bundle
   targets is rarer in practice than the spec assumed — or that there's a code path I haven't found where
   `max_active` can legitimately exceed the array's current fill level. Worth a dedicated look.
2. I did not use the spec's literal "~24 hours at 50 Hz" wraparound figure in the comment — that
   arithmetic doesn't hold (2^32 grains / 50/sec ≈ 994 days, not 24h). I wrote a comment capturing the
   same intent (wraparound is harmless and self-heals, grain lifetimes are far shorter than the wrap
   period) without asserting a specific number I couldn't verify. Flagging in case the "24h" figure was
   meant to convey something I'm missing (e.g. a specific worst-case trigger rate not obvious from the
   code).
3. `ForceAllocateGrainForTest()` is a new test-only public method with a real (if narrow) side effect
   (can mark a grain `pending_kill`). It's clearly named and documented as test-only, consistent with the
   brief's explicit allowance to add "a minimal accessor consistent with the codebase's test style," but
   flagging since it's a slightly heavier hook than the existing read-only accessors like
   `ActiveGrainCount()`.
