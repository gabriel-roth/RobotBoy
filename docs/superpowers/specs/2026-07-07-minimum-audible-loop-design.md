# Minimum Audible Loop Size Design

## Goal

Prevent Loooop and Löp from producing a stationary, effectively silent one-sample window when Size is fully counter-clockwise.

## Behavior

At Size 0, the shared loop engine will enforce a sample-rate-dependent minimum window length corresponding to a 20 kHz repeat frequency at 1× playback speed:

```text
minimum samples = ceil(sample rate / 20,000)
```

This yields three samples at 44.1/48 kHz and five samples at 96 kHz. The result remains an extremely short loop, but the playhead moves and its repeat frequency is potentially audible. If the recorded loop itself is shorter than the calculated minimum, the window is capped to the recorded loop length.

## Implementation

Change only `LoopEngine::windowBounds()`. Keep the Size knob and CV range at 0–1 and preserve proportional window sizing above the minimum. Because Loooop and Löp share `LoopEngine`, both modules receive identical behavior without wrapper-specific changes.

## Verification

- Add a regression test that fails under the current one-sample minimum.
- At 48 kHz, verify Size 0 produces a three-sample window and advancing output.
- At 96 kHz, verify Size 0 produces a five-sample window.
- Verify loops shorter than the calculated minimum still use their full available length safely.
- Run the full regression suite and build both VCV Rack and MetaModule targets.
