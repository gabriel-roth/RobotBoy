# Skylark

*The Eurasian skylark sings in flight, delivering a long song of ever-changing phrases.*

**Skylark** is a tempo-synced modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule) built from recognizable physical and musical gestures. Each cycle it plays one of 18 shapes, each with a family of variations. Five transforms bend the shape further; DRIFT makes each cycle change; the trimpots can feed the output back into a control or add random movement.

Skylark is one of Robot Boy’s three complex modulators, with [Weaver](Weaver.md) and [Lyrebird](Lyrebird.md).

<img src="screenshots/Skylark.png" alt="Skylark module" height="500">

---

## Controls

### Shape

- **SHAPE:** the **‹ ›** buttons step through the shapes (see [**Shapes**](#shapes)). SHAPE CV offsets the selection, with 10 V spanning all 18 shapes. By default, changes take effect immediately (see [**Settings**](#settings)).
- **VARIATION:** sweeps the shape through a series of changes, 0–100%. Default is 50%.

### Transforms

Each transform applies immediately.

- **TIMING:** moves the shape’s midpoint earlier or later in the cycle, compressing one half and stretching the other.
- **LIFT:** bends levels up or down, with the top and bottom fixed.
- **SYMMETRY:** crossfades the second half of the cycle with a mirror image of the first.
- **TILT:** adds a rising or falling ramp across the cycle.
- **DRIFT:** randomly offsets the shape’s internal settings each cycle.

### CV trimpots

VARIATION and each transform have a CV input and a trimpot. With a cable patched, the trimpot attenuverts the CV; with the trimpot fully clockwise, 5 V spans the knob’s range. With no cable:

- **Clockwise:** feeds OUT back into the control.
- **Counter-clockwise:** adds a random offset each cycle. VARIATION and DRIFT change at the start of the cycle; the other transforms glide to the new value.

By default, CV and feedback are followed continuously; they can instead be sampled once per cycle (see [**Settings**](#settings)).

### Timing

- **CLOCK:** sets the tempo in BPM; default is 120. A clock signal at the input overrides the knob.
- **RESET:** returns to the start of the cycle and restarts the DRIFT and random sequences, so the same cycles play again. With a clock patched, by default it waits for the next clock pulse (see [**Settings**](#settings)).
- **SPEED:** sets the cycle rate, from 1/4 to x4.
    - The left button and trigger input halve the cycle rate.
    - The right button and trigger input double the cycle rate.
    - The center button returns the cycle rate to x1.

A **SPEED** change takes effect at the next point where old and new cycles align, or immediately on RESET.

### Outputs

- **OUT:** the shape, ±5 V by default.
- **EOC:** a trigger at the end of each cycle.

---

## Display

The display shows one cycle of the waveform at OUT, with a playhead. The header shows the shape name (below the trace on MetaModule), the VARIATION amount after CV, the tempo, and SPEED when not x1. A new SPEED blinks dimly until it takes effect.

---
<a id="settings"> </a>
## Settings

- **Output range:** ±5 V (default), ±10 V, 0–5 V, or 0–10 V.
- **When out of range:**
    - **Fit** (default)**:** scales the whole cycle down to fit.
    - **Clip:** flattens the signal above or below the range.
    - **Soft clip:** rounds the signal close to the top or bottom of the range.
    - **Fold:** anything past the edge is reflected back.
    - **Wrap:** anything past the edge comes in at the opposite edge.
- **Slew:** Off (default), 2 ms, 5 ms, or 20 ms.
- **Clock pulses per cycle:** 1, 2, 4, 8, 16, 24, 32, 48, or 96 (default 4).
- **CV and feedback:** **Continuous** (default) or **Sample once per cycle**.
- **Shape changes:** **Immediately** (default) or **At end of cycle**.
- **Speed changes:** **At end of cycle** (default) or **Immediately**.
- **Reset:** with a clock patched, **On clock pulse** (default) waits for the next pulse; **Immediately** restarts at once, and the cycle stays offset from the clock.

---
<a id="shapes"> </a>
## Shapes

Each image shows one cycle at VARIATION 0%, 50%, and 100%, other controls at defaults.

### Gestures

**1. Bounce:** a run of arcs, each lower and shorter than the last.

<img src="images/skylark-shapes/01-bounce.svg" alt="Bounce at Variation 0%, 50% and 100%" width="620">

**2. Spring:** a decaying oscillation.

<img src="images/skylark-shapes/02-spring.svg" alt="Spring at Variation 0%, 50% and 100%" width="620">

**3. Ratchet:** a burst of sharp hits that speed up or slow down across the cycle, each quieter than the last.

<img src="images/skylark-shapes/03-ratchet.svg" alt="Ratchet at Variation 0%, 50% and 100%" width="620">

**4. Flam:** one to three quieter grace hits ahead of a main hit, which then decays.

<img src="images/skylark-shapes/04-flam.svg" alt="Flam at Variation 0%, 50% and 100%" width="620">

**5. Bowed:** a swell, a sustain with vibrato fading in, and a release.

<img src="images/skylark-shapes/05-bowed.svg" alt="Bowed at Variation 0%, 50% and 100%" width="620">

**6. Stutter:** gated pulses under a decaying envelope.

<img src="images/skylark-shapes/06-stutter.svg" alt="Stutter at Variation 0%, 50% and 100%" width="620">

**7. Heartbeat:** one to four beats of an ECG trace around a center baseline: a small bump, a spike swinging both ways, a broad hump.

<img src="images/skylark-shapes/07-heartbeat.svg" alt="Heartbeat at Variation 0%, 50% and 100%" width="620">

### Cycles

**8. Harmonics:** a fundamental with overtones whose brightness opens and closes across the cycle.

<img src="images/skylark-shapes/08-harmonics.svg" alt="Harmonics at Variation 0%, 50% and 100%" width="620">

**9. Folded sine:** two sines through a wavefolder, swelling two to four times a cycle.

<img src="images/skylark-shapes/09-folded-sine.svg" alt="Folded sine at Variation 0%, 50% and 100%" width="620">

**10. FM:** a cascaded FM wave whose brightness and level decay or grow across the cycle.

<img src="images/skylark-shapes/10-fm.svg" alt="FM at Variation 0%, 50% and 100%" width="620">

**11. Chirp:** a wave that changes rate across the cycle, under a swell.

<img src="images/skylark-shapes/11-chirp.svg" alt="Chirp at Variation 0%, 50% and 100%" width="620">

**12. Beating:** three closely tuned waves beating slowly, with a faster beat inside, rectified toward separate pulses.

<img src="images/skylark-shapes/12-beating.svg" alt="Beating at Variation 0%, 50% and 100%" width="620">

### Steps

**13. Arpeggio:** stepped notes, in an Up, Up-down, or Down pattern.

<img src="images/skylark-shapes/13-arpeggio.svg" alt="Arpeggio at Variation 0%, 50% and 100%" width="620">

**14. Ringing steps:** two to six levels in turn, each change ringing before it settles.

<img src="images/skylark-shapes/14-ringing-steps.svg" alt="Ringing steps at Variation 0%, 50% and 100%" width="620">

**15. Devil’s staircase:** a fractal staircase of flat steps of many sizes.

<img src="images/skylark-shapes/15-devils-staircase.svg" alt="Devil's staircase at Variation 0%, 50% and 100%" width="620">

### Chaos

**16. Random nodes:** random points joined into a looping line.

<img src="images/skylark-shapes/16-random-nodes.svg" alt="Random nodes at Variation 0%, 50% and 100%" width="620">

**17. Fractal line:** a jagged line made by midpoint displacement.

<img src="images/skylark-shapes/17-fractal-line.svg" alt="Fractal line at Variation 0%, 50% and 100%" width="620">

**18. Glitch:** segments of random length, some holding a level, others breaking into bursts of rapid random steps.

<img src="images/skylark-shapes/18-glitch.svg" alt="Glitch at Variation 0%, 50% and 100%" width="620">
