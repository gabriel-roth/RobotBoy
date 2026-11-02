# Skylark

**Skylark** is a clocked shape modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). Each cycle it plays one of 18 shapes; **VARIATION** selects a form within the shape's family, and five transforms reshape the result. It is one of Robot Boy's three complex modulators, with [Weaver](Weaver.md) and [Lyrebird](Lyrebird.md).

<img src="screenshots/Skylark.png" alt="Skylark module" height="500">

---

## Overview

Skylark is a source of rhythmic, tempo-synced modulation built from recognizable gestures: a bouncing ball, a ratchet, a heartbeat, a spring, an arpeggio. Instead of a plain LFO wave, each bar it plays a shape with internal structure, which suits it to clocked envelopes, accents and movement that sound performed rather than mechanical. Each shape has a family of variations to choose from, the transforms bend it further, and DRIFT or the random trimpots make successive bars differ.

---

## Controls

### SHAPE and VARIATION

- **SHAPE**: selects the shape (default Bounce); **‹ ›** step through them. SHAPE CV adds to the knob, 10 V spanning all 18 shapes, wrapping.
- **VARIATION**: 0–100%, default 50%.

### Transforms

Each transform applies immediately.

- **TIMING**: moves the shape's midpoint earlier or later in the cycle, compressing one half and stretching the other. Default center.
- **LIFT**: bends levels up or down, with the top and bottom fixed. Default center.
- **SYMMETRY**: crossfades the second half of the cycle toward a mirror of the first. At 100% the cycle ends where it starts. Default 0%.
- **TILT**: adds a rising or falling ramp across the cycle, which can push the shape out of range (see **When out of range**). Default center.
- **DRIFT**: randomly offsets the shape's internal parameters each cycle, by up to 30% of their range. The sequence repeats after RESET. Default 0%.

### CV trimpots

VARIATION and each transform have a CV input and trimpot. With a cable patched, the trimpot attenuverts the CV; at full trim, 5 V spans the control's range. With no cable:

- **Clockwise:** feeds **OUT** back into the control.
- **Counter-clockwise:** adds a random offset each cycle. VARIATION and DRIFT change at the cycle start; the other transforms glide between values. The sequence repeats after RESET.

### CLOCK, RESET and SPEED

- **CLOCK**: tempo, 20–300 BPM, default 120. A clock at the input overrides the knob.
- **RESET**: the button or a trigger restarts the cycle and the random sequences.
- **SPEED**: the outer buttons, or triggers at their inputs, halve or double the cycle rate (1/4x to 4x); the center button returns to 1x. A change takes effect at the next point where old and new cycles align, or immediately on RESET.

### Outputs

- **OUT**: the shape, ±5 V by default.
- **EOC**: a trigger at the end of each cycle.

---

## Display

One cycle of the output, with a playhead. The header shows the shape name (below the trace on MetaModule), VARIATION after CV, the tempo, and SPEED when not x1.

---

## The shapes

Each image shows one cycle at VARIATION 0%, 50% and 100%, other controls at defaults. The random shapes repeat exactly each cycle; only DRIFT and the trimpots' random mode vary them.

### Gestures

**1. Bounce**

<img src="images/skylark-shapes/01-bounce.svg" alt="Bounce at Variation 0%, 50% and 100%" width="620">

**2. Spring**

<img src="images/skylark-shapes/02-spring.svg" alt="Spring at Variation 0%, 50% and 100%" width="620">

**3. Ratchet**

<img src="images/skylark-shapes/03-ratchet.svg" alt="Ratchet at Variation 0%, 50% and 100%" width="620">

**4. Flam**

<img src="images/skylark-shapes/04-flam.svg" alt="Flam at Variation 0%, 50% and 100%" width="620">

**5. Bowed**

<img src="images/skylark-shapes/05-bowed.svg" alt="Bowed at Variation 0%, 50% and 100%" width="620">

**6. Stutter**

<img src="images/skylark-shapes/06-stutter.svg" alt="Stutter at Variation 0%, 50% and 100%" width="620">

**7. Heartbeat**

<img src="images/skylark-shapes/07-heartbeat.svg" alt="Heartbeat at Variation 0%, 50% and 100%" width="620">

### Cycles

**8. Harmonics**

<img src="images/skylark-shapes/08-harmonics.svg" alt="Harmonics at Variation 0%, 50% and 100%" width="620">

**9. Folded sine**

<img src="images/skylark-shapes/09-folded-sine.svg" alt="Folded sine at Variation 0%, 50% and 100%" width="620">

**10. FM**

<img src="images/skylark-shapes/10-fm.svg" alt="FM at Variation 0%, 50% and 100%" width="620">

**11. Chirp**

<img src="images/skylark-shapes/11-chirp.svg" alt="Chirp at Variation 0%, 50% and 100%" width="620">

**12. Beating**

<img src="images/skylark-shapes/12-beating.svg" alt="Beating at Variation 0%, 50% and 100%" width="620">

### Steps

**13. Arpeggio**

<img src="images/skylark-shapes/13-arpeggio.svg" alt="Arpeggio at Variation 0%, 50% and 100%" width="620">

Variation picks the pattern by thirds of its travel: **Up**, **Up-down**, then **Down**.

**14. Ringing steps**

<img src="images/skylark-shapes/14-ringing-steps.svg" alt="Ringing steps at Variation 0%, 50% and 100%" width="620">

**15. Devil's staircase**

<img src="images/skylark-shapes/15-devils-staircase.svg" alt="Devil's staircase at Variation 0%, 50% and 100%" width="620">

### Chaos

**16. Random nodes**

<img src="images/skylark-shapes/16-random-nodes.svg" alt="Random nodes at Variation 0%, 50% and 100%" width="620">

**17. Fractal line**

<img src="images/skylark-shapes/17-fractal-line.svg" alt="Fractal line at Variation 0%, 50% and 100%" width="620">

**18. Glitch**

<img src="images/skylark-shapes/18-glitch.svg" alt="Glitch at Variation 0%, 50% and 100%" width="620">

---

## Context menu

- **Output range**: ±5 V (default), ±10 V, 0–5 V, 0–10 V.
- **When out of range**: **Fit** (default; scales the whole cycle down to fit), **Clip**, **Soft clip**, **Fold** or **Wrap**.
- **Slew**: Off (default), 2 ms, 5 ms, 20 ms.
- **Clock pulses per cycle**: pulses per bar at the CLOCK input, 1–96 (default 4).
- **CV and feedback**: **Continuous** (default) or **Sample once per cycle**.
- **Shape changes**: **Immediately** (default) or **At end of cycle**.
