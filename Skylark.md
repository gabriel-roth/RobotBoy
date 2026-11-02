# Skylark

**Skylark** is a clocked shape modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). Each cycle it plays one of 18 shapes. **Variation** selects a form within the shape's family; five transforms reshape the result. It is one of Robot Boy's three complex modulators, with [Weaver](Weaver.md) and [Lyrebird](Lyrebird.md).

<img src="screenshots/Skylark.png" alt="Skylark module" height="500">

---

## Overview

- One cycle = one bar of four beats, from the **CLOCK** knob's tempo or an external clock. **SPEED** scales the cycle from 1/4x to 4x.
- The shape plays once per cycle and restarts; **EOC** fires at each wrap.
- **SHAPE** selects the shape; **VARIATION** moves all of the shape's internal parameters along a fixed path.
- Each shape is normalized so its largest excursion reaches the edge of the output range at every Variation setting.
- The transforms (**TIMING**, **LIFT**, **SYMMETRY**, **TILT**, **DRIFT**) are applied after the shape. At their defaults (TIMING, LIFT, TILT at center; SYMMETRY, DRIFT fully counter-clockwise) they have no effect.
- The random shapes (Random nodes, Fractal line, Glitch) are deterministic: the same settings give the same cycle every time. Only DRIFT and the random trimpot mode vary cycles.

---

## Controls

### SHAPE and VARIATION

- **SHAPE** knob: selects one of 18 shapes (default Bounce). The **‹ ›** buttons step to the previous/next shape, wrapping.
- **SHAPE** CV: added to the knob; 10 V spans all 18 shapes (0.56 V per shape). Negative voltages step backward. Wraps.
- **VARIATION**: 0–100%, default 50%.
- **VARIATION** CV: scaled by its trimpot (−1 to +1, default 0). At full trim, 5 V spans the whole range.

A shape change takes effect immediately, or at the next cycle start with **Shape changes → At end of cycle**.

### Transforms

Each has a knob, a CV trimpot (−1 to +1, default 0) and a CV input. At full trim, 5 V spans the knob's range; the sum is clamped to that range. Transforms apply immediately, mid-cycle.

- **TIMING** (−1 to +1, default 0): moves the shape's midpoint between 2% and 98% of the cycle, compressing one half and stretching the other.
- **LIFT** (−1 to +1, default 0): power curve on the level, with the top and bottom fixed. Positive raises values toward the top; negative lowers them toward the bottom.
- **SYMMETRY** (0–100%, default 0%): crossfades the second half of the cycle toward a mirror of the first half. At 100% the cycle ends at its start value.
- **TILT** (−1 to +1, default 0): adds a ramp across the cycle, rising for positive, falling for negative. At ±1 the ramp alone spans the output range. The excess is handled by **When out of range**.
- **DRIFT** (0–100%, default 0%): each cycle, offsets the shape's internal parameters by a random amount, up to 30% of each parameter's range at 100%. The offsets are derived from the cycle count, so the sequence repeats after **RESET**. Does not affect Arpeggio's pattern or Chirp's direction.

### Trimpots with no cable

With nothing patched into a CV jack, its trimpot (VARIATION and the five transforms) does one of two things. At center it does nothing.

- **Clockwise: feedback.** **OUT** is fed into the control, scaled by the trimpot. VARIATION, TIMING, LIFT and TILT receive OUT as if it were patched. SYMMETRY and DRIFT receive only positive offsets, proportional to OUT's position in the output range.
- **Counter-clockwise: random.** The control gets a random offset per cycle, scaled by the trimpot. At full turn the offset spans ±half the control's range (SYMMETRY and DRIFT: 0 to the full range, upward only).
  - VARIATION and DRIFT: the value changes at the cycle start.
  - TIMING, LIFT, SYMMETRY, TILT: the value glides from one cycle's value to the next across the cycle.
  - The values are derived from the cycle count and repeat after **RESET**.

**CV and feedback → Sample once per cycle** samples CV and feedback at each cycle start instead of continuously.

### CLOCK, RESET and SPEED

- **CLOCK** knob: 20–300 BPM, default 120, in 0.01 BPM steps. One cycle = 4 beats (12 s at 20 BPM, 0.8 s at 300 BPM). Not affected by Randomize.
- **CLOCK** input: overrides the knob. **Clock pulses per cycle** sets the pulses per bar (default 4). The first pulse resets the cycle to its start; from the second pulse, the tempo is measured, averaged over one cycle of pulses, and followed with a phase lock. Cycle length with a clock: 0.05–60 s.
  - If the clock stops, the last tempo continues. The first pulse after a long gap resets the cycle to its start.
  - CLOCK and RESET inputs trigger above 1 V.
- **RESET** button and input: restart the cycle. Does not fire EOC. Counts as a cycle start for per-cycle settings and restarts the DRIFT and random sequences. With a clock patched, the next pulse is treated as the downbeat.
- **SPEED**: the outer buttons halve or double the cycle rate (1/4x, 1/2x, 1x, 2x, 4x); the center button returns to 1x. The jacks under the outer buttons do the same on a rising edge above 1 V. A change waits for the next point where the old and new cycles both start (up to four bars), so the phase stays aligned to the bar; until then the display shows the new speed dimmed and blinking. RESET applies it immediately. Saved with the patch.

### Outputs

- **OUT**: the shape. ±5 V by default; ±10 V, 0–5 V or 0–10 V via **Output range**.
- **EOC**: 10 V, 1 ms trigger at each cycle wrap (at the SPEED rate).

---

## Display

- One full cycle of the output after Variation, transforms, output range and out-of-range handling, before slew. A vertical playhead and a dot mark the current position.
- Center line: middle of the output range (0 V, 2.5 V or 5 V).
- A discontinuity at the loop point is drawn as a vertical line at the left edge.
- Header: shape name on the left (below the trace on MetaModule). On the right: Variation after CV (`VAR 50%`), tempo in BPM (measured, when a clock is patched), and SPEED when not x1 (dimmed and blinking while a change is pending).

---

## The shapes

Each image shows one cycle at VARIATION 0%, 50% and 100%, other controls at defaults.

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

In VCV Rack, right-click the panel; on MetaModule, open Options.

- **Output range**: ±5 V (default), ±10 V, 0–5 V, 0–10 V.
- **When out of range**: how values beyond the output range (from TILT) are handled.
  - **Fit** (default): the whole cycle is scaled down to fit.
  - **Clip**: hard clip.
  - **Soft clip**: saturation curve; also rounds values within range.
  - **Fold**: reflect at the edge.
  - **Wrap**: wrap to the opposite edge.
- **Slew**: Off (default), 2 ms, 5 ms, 20 ms. Applied to OUT.
- **Clock pulses per cycle**: 1, 2, 4 (default), 8, 16, 24, 32, 48, 96. 4 = quarter notes, 16 = sixteenths, 96 = 24 PPQN.
- **CV and feedback**: **Continuous** (default), or **Sample once per cycle** (sampled at each cycle start or RESET, held for the cycle). Knobs always apply immediately; random offsets change once per cycle in both modes.
- **Shape changes**: **Immediately** (default), or **At end of cycle** (changes from the knob, buttons or CV wait for the next cycle start or RESET; the display shows the playing shape until then).

Initialize resets the menu settings and SPEED as well as the knobs.

---

## MetaModule

- Changes to VARIATION and DRIFT are applied every 256 samples (about 5 ms) instead of every 64.
- The display refreshes less often than in VCV Rack.
