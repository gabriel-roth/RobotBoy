# Retours: sticky tap tempo + clock-light re-voicing

Date: 2026-07-23
Status: approved (tap-tempo escape-hatch decided by user; clock-light fix decided autonomously)

## Motivation

Two related complaints about Retours' clock/tempo behavior:

1. **Tap tempo doesn't stick.** With nothing patched into Clock, a tapped tempo is
   abandoned after ~5 s of no taps, or when the Interval knob moves > ~5% of travel
   (`base_time.cpp:105-114`). The user wants taps to hold indefinitely, like a normal
   tap-tempo pedal.

2. **Clock light flashes irregularly at fast tempos** (reported at 240 BPM), reading as
   "not tracking the clock."

## Investigation findings (clock light)

A headless reproduction (`scratchpad/repro_clocklight.cpp`) drove the real
`BaseTimeControl` with a clock and replicated `Retours.cpp`'s `clock_light_phase_` math:

- The tempo **measurement is correct**: with a steady clock the base delay time is exact
  and never flickers across the whole Interval sweep. Not a math/jitter bug.
- The light blinks **regularly** for a perfect clock, so the accumulator itself is sound.
- The real problems are two **indicator design** choices:
  - The light is driven by the **subdivided base Interval**, not the clock beat. The
    default Interval knob (0.35) selects the **1/4** subdivision, so at 240 BPM the light
    blinks at **~16 Hz** with a ~6 ms pulse (base = 62.5 ms) — a fast, short strobe that
    reads as erratic flicker, worsened by beating against the display refresh.
  - The phase accumulator **free-runs and is never anchored to clock edges** (unlike the
    Shape envelope, which resyncs at `retours_processor.cpp:219`), so it drifts against
    the user's clock and never visibly "locks."

Conclusion: not a DSP tracking bug. Fix is in the indicator.

## Design

### 1. Tap tempo holds indefinitely (chosen escape hatch: right-click "Clear tapped tempo")

- **Remove both auto-abandon triggers** in `BaseTimeControl::UpdateClockTiming` (the 5 s
  timeout and the Interval-moved-> exit). A measured tempo — from a tap or a cable —
  holds until explicitly cleared. Unpatching the Clock cable no longer drops it either;
  the model is uniformly "a tempo, however established, sticks until cleared."
- Because Interval no longer clears the tempo, it is free to select subdivisions while
  tapped, matching cabled behavior.
- Add `BaseTimeControl::ClearClock()` — resets `clocked_`, `clock_interval_`, `has_tick_`,
  `subdivision_zone_`, `samples_since_tick_` back to free-running.
- Expose it via `RetoursProcessor::ClearTappedTempo()`.
- Add a right-click menu item **"Clear tapped tempo"**, deferred to the audio thread via
  an atomic flag (same pattern as "Clear buffer"). Undoable on desktop via `withMenuUndo`
  is NOT applicable (it mutates DSP state, not JSON) — it runs directly like Clear buffer.
- Remove the now-unused `density_at_last_tick_` field.

### 2. Clock light: beat-rate, clock-anchored

- Add `BaseTimeControl::ClockIntervalSeconds()` (the measured tick-to-tick beat, 0 when not
  clocked) and expose via `RetoursProcessor::ClockBeatSeconds()`.
- In `Retours.cpp`, drive `clock_light_phase_`:
  - **Clocked:** period = clock beat (once per beat); reset phase to 0 on each incoming
    tick (`params_.clock_tick_offset >= 0`) so it anchors to the clock, matching the
    envelope's resync. Between ticks / when holding a tapped tempo, it free-runs at the
    held beat rate.
  - **Free-running:** period = base Interval (unchanged behavior).
- 240 BPM -> 4 Hz flash, locked to the clock: readable and visibly tracking.

## Testing

- DSP unit tests (`tests/retours_delay_dsp/test_base_time.cpp`):
  - tapped tempo holds past 5 s with no cable and no further ticks
  - tapped tempo survives a large Interval move
  - `ClearClock()` returns to free-running
  - `ClockIntervalSeconds()` reports the measured beat
- Build VCV + run full Catch2 suite.
- **GUI verification is a user-checklist item** (per project policy: no agent-driven
  GUI-simulator tests). The perceptual "reads as a steady, clock-locked blink" claim
  can only be confirmed on the real panel.

## Docs

- Update `Retours.md` clocking section: taps hold until cleared; new "Clear tapped tempo"
  menu item; Clock light now blinks once per clock beat when clocked.
- Update `CHANGELOG.md`.
