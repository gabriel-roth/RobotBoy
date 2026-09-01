# Loooop / Löp: Speed knob notch at 1.0 and -1.0

**Date:** 2026-08-27. **Status:** approved design; implementation plan and code not yet started.

## Summary

VCV Rack lets you double-click a knob to reset it to its default (1.0 for Speed), but MetaModule has no equivalent gesture — a MM user who has dragged a Speed knob away from normal speed has no easy way back. This adds a software "detent": each Speed knob hard-locks to exactly 1.0 or -1.0 whenever its raw position falls within a small window around those two points, and behaves as a fully continuous knob everywhere else. Applies to all five Speed knobs across both modules (Loooop's four per-head knobs, Löp's single knob) and both hosts (VCV and MetaModule).

The lock is implemented once, as a stateless function in the header shared by VCV and MetaModule, and applied only to the manual knob term — CV modulation is summed in afterward and is never affected, so a CV sweep through 1.0 or -1.0 stays perfectly smooth.

## Behavior

`loooop::applySpeedNotch(float rawKnob) -> float`, added to `src/loooop/LooperModuleDSP.hpp` next to `speedFromControls`:

```cpp
constexpr float kSpeedNotchTargets[2] = {1.f, -1.f};
constexpr float kSpeedNotchWidth = 0.05f;  // raw units, each side (range is -2..2)

inline float applySpeedNotch(float rawKnob) {
    for (float target : kSpeedNotchTargets)
        if (std::fabs(rawKnob - target) <= kSpeedNotchWidth)
            return target;
    return rawKnob;
}
```

- Pure function of the current raw knob position — no history, no hysteresis state. Unlike the Retours delay-time subdivision snap (`src/retours_delay/dsp/src/time/base_time.cpp`), there is no ambiguous midpoint between two different snapped outputs to chatter between: each of the two windows is isolated, everything outside both windows is untouched identity, and a boundary crossing produces one deliberate jump (the "click"), not a value in dispute.
- Width is ±0.05 raw units (the -2..2 range spans 4.0 total, so this is ~2.5% of full travel on each side of each target) — chosen as a middle ground: easy to land on without a lot of fiddling, narrow enough that nearby intentional settings (0.9, 1.1, etc.) are not swallowed. `kSpeedNotchWidth` is a single named constant so it can be retuned after real-hardware/real-mouse testing without touching call sites.
- 0.0 (freeze) is not a notch target — only ±1.0, per the request.
- Applies identically regardless of the per-head "Speed CV = V/Oct" switch: the notch acts on `spKnob` before either the `speedFromControls` or `speedFromVOct` branch, so pinning to exactly 1.0 (or -1.0) means the same "normal speed" (or "reverse at normal speed") base in both interpretations.

## Call sites

All four are a one-line wrap of the existing `spKnob` read, before it's used for CV summation or the V/Oct branch:

- `src/loooop/Loooop.cpp:202` (VCV, 4-head loop) — `float spKnob = loooop::applySpeedNotch(params[SPEED1_PARAM + HEAD_PARAMS * h].getValue());`
- `src/loooop/Lop.cpp:155` (VCV, single head) — `float spKnob = loooop::applySpeedNotch(params[SPEED_PARAM].getValue());`
- `metamodule/loooop/LoooopCore.cc:225` (MM, `updateHead` template) — `float spKnob = loooop::applySpeedNotch((getState<S>() - 0.5f) * 4.f);`
- `metamodule/loooop/LopCore.cc:103` (MM, single head) — `float spKnob = loooop::applySpeedNotch((getState<SpeedKnob>() - 0.5f) * 4.f);`

No changes to `speedFromControls`, `speedFromVOct`, `VOctSpeedMemo`, the MetaModule Info/Elements declarations, or the panel SVGs. No new params, no patch-persistence changes.

## VCV tooltip parity

On VCV, `params[...].getValue()` (what the tooltip and right-click "Set value" read/write) is normally the same number fed to the engine — wrapping only the local `spKnob` variable in `process()` would leave the tooltip showing the raw un-snapped position (e.g. "0.98") while the engine plays exactly 1.0. To keep the display honest, add a small `ParamQuantity` subclass to `src/plugin.hpp`, following the existing `PitchParamQuantity` pattern used for Particules/Ondes/Retours pitch knobs:

```cpp
struct SpeedParamQuantity : ParamQuantity {
    float getDisplayValue() override { return loooop::applySpeedNotch(getValue()); }
    void setDisplayValue(float v) override { setValue(v); }
};
```

- `getDisplayValue()` is display-only: it does not change the stored param value or the knob's drag feel, only what the tooltip/readout shows.
- `setDisplayValue()` is a plain passthrough (unlike `PitchParamQuantity`, Speed's display units and stored units are identical — there's no coordinate transform to invert). Typing "1" via right-click "Set value" stores exactly 1.0, which is already inside the lock window, so the two paths agree.
- Used via `configParam<SpeedParamQuantity>(...)` in place of plain `configParam` for all 5 Speed params (`Loooop.cpp:72`, `Lop.cpp:42`). Double-click-to-default is unaffected (still resets the stored value to 1.0 via stock Rack `ParamWidget` behavior).
- No default `getDisplayValueString()` override needed — Speed has no special unit formatting the way pitch does (semitones); the default formatting of `getDisplayValue()`'s float is sufficient.
- MetaModule has no numeric per-knob readout at all, so this section is VCV-only.

## Edge cases / risks

- **Hardware pot noise at a window boundary.** The lock is a hard threshold with no hysteresis; if a real MM pot's raw voltage has enough noise to dither across a boundary while the knob is stationary, the engine speed could flicker between the locked value and a nearby continuous value. This is expected to be a non-issue in practice (a resting pot's ADC read is not typically jittery enough to cross a ±0.05 raw window edge), but it can only be confirmed on physical hardware — flagged for the user's hardware checklist. If it turns out to be audible, the fix is a small hysteresis band around each window (same idiom as the Retours precedent), not a redesign.
- **No visual indicator.** The panel does not show where the notches are — this is a feel-only change (matching the request), and MetaModule's `KnobSnapped` element (the only SDK primitive with any per-position visual behavior) was ruled out because it quantizes the entire knob travel into equal steps, incompatible with keeping the rest of the range continuous.
- **V/Oct mode** is unaffected in scope — the notch acts on the same `spKnob` term consumed by both branches, so behavior is consistent whether or not "Speed CV = V/Oct" is enabled.

## Tests

`tests/loooop/test_module_dsp.cpp` already covers `speedFromControls`/`speedFromVOct` with the `check(near(...), "description")` pattern (e.g. line 16-19); add cases for `applySpeedNotch` alongside them:

- Inside each window snaps exactly: `applySpeedNotch(1.03f) == 1.0f`, `applySpeedNotch(-0.97f) == -1.0f`.
- Just outside each window passes through unchanged: `applySpeedNotch(1.06f) == 1.06f`, `applySpeedNotch(-1.06f) == -1.06f`.
- Exactly at the boundary (±0.05 from a target) snaps (inclusive `<=`).
- A value between the two windows (e.g. 0.0, or 0.4) passes through unchanged.
- Values at the extremes (±2.0, well outside both windows) pass through unchanged.

No integration/engine-level test needed — `applySpeedNotch` is a pure function tested in isolation, and the call sites are one-line wraps of an already-tested value (`speedFromControls`/`speedFromVOct` themselves are unchanged).

GUI/feel verification (does the lock feel right on a real VCV mouse-drag and a real MM hardware pot, is the tooltip readable, does double-click still work) goes on the user's manual checklist — not something an agent can verify via the headless test harness or a simulator screenshot.

## Docs

`Loooop.md` / `Lop.md`: brief mention in the Speed knob description that turning near 1x or reverse-1x snaps to exactly that speed. Escape literal tildes as `\~` if any are added.

## Out of scope

Any notch/detent for other continuous knobs on Loooop/Löp or other modules (Position, Size, Level, Jitter, pitch knobs elsewhere already have their own notch mechanism); a visual indicator of the notch position; `param_ranges.json` (unaffected — no param range or default changes).
