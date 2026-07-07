# Minimum Audible Loop Size Design

## Goal

Prevent Loooop and Löp from producing a stationary, effectively silent one-sample window when Size is fully counter-clockwise.

## Behavior

At Size 0, the shared loop engine will enforce a sample-rate-dependent minimum window length of 1 ms:

```text
minimum samples = ceil(sample rate × 1 / 1000)
```

This yields 45 samples at 44.1 kHz, 48 at 48 kHz, and 96 at 96 kHz. At 1× playback, the loop repeats at approximately 1 kHz, making ordinary recorded material substantially more audible than the previous near-Nyquist minimum. If the recorded loop itself is shorter than the calculated minimum, the window is capped to the recorded loop length.

## Implementation

Change only `LoopEngine::windowBounds()`. Keep the Size knob and CV range at 0–1 and preserve proportional window sizing above the minimum. Because Loooop and Löp share `LoopEngine`, both modules receive identical behavior without wrapper-specific changes.

## Verification

- Add a regression test that fails under the current one-sample minimum.
- At 48 kHz, verify Size 0 produces a three-sample window and advancing output.
- At 96 kHz, verify Size 0 produces a five-sample window.
- Verify loops shorter than the calculated minimum still use their full available length safely.
- Run the full regression suite and build both VCV Rack and MetaModule targets.
