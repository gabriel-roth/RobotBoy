# Skylark

**Skylark** is a shape modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). Once per bar it plays a structured gesture — a bouncing ball, a ratchet, a heartbeat, a chirp, a fractal line — and loops it in time with your tempo or clock. Each of its 18 shapes is a whole family of related forms that the **Variation** knob sweeps through, and five transform controls bend the timing, curve, symmetry and slope of whatever is playing. Use it as a clocked LFO, a repeating envelope, or a slowly evolving modulation source.

Skylark is one of Robot Boy's three complex modulators, with [Weaver](Weaver.md) and [Lyrebird](Lyrebird.md).

<img src="screenshots/Skylark.png" alt="Skylark module" height="500">

---

## How it works

Skylark runs a cycle that lasts one bar — four beats at the tempo set by the **Clock** knob, or one bar of an external clock. During each cycle it plays one shape from start to end, then starts it again, sending a trigger from **EOC** each time it wraps. **SPEED** can stretch the cycle over two or four bars, or squeeze it into a half or a quarter.

You pick **which** shape with **Shape**, and **which form** of it with **Variation**. Each shape is built from a handful of internal settings (the number of bounces, how fast a spring rings, how many steps an arpeggio has), and Variation moves all of them together along a path chosen for that shape, so one knob sweeps the shape through its whole family without dull or broken forms in between. Every shape is scaled so that its biggest swing reaches the edge of the output range, whatever the Variation setting.

The five transforms — **Timing**, **Lift**, **Symmetry**, **Tilt** and **Drift** — then reshape the result. Turned to their defaults (noon for Timing, Lift and Tilt; fully counter-clockwise for Symmetry and Drift), they do nothing.

__NOTE:__ The "random" shapes (Random nodes, Fractal line, Glitch) are not random from cycle to cycle. Each one draws the same shape every time at the same settings, so patches are repeatable. **Drift** is what makes cycles differ.

---

## Quick start

1. **Patch OUT** to whatever you want to modulate. By default Skylark plays **Bounce** at 120 BPM, one bounce sequence every 2 seconds, over ±5 V.
2. **Turn Shape** (or press its **‹ ›** buttons) to choose a shape. The display names it and draws one full cycle.
3. **Turn Variation** to sweep through that shape's forms: more or fewer bounces, a faster or slower spring, a different arpeggio pattern.
4. **Set the tempo** with the **Clock** knob, or patch a clock into the **Clock** jack (four pulses per bar by default) and Skylark follows it.

---

## Controls

### Shape and Variation (top plate)

*Shape:* chooses one of 18 shapes (listed below; default Bounce). The **‹** and **›** buttons under its jack step to the previous and next shape, wrapping around at the ends. The **Shape** CV input adds to the knob: 10 V spans all 18 shapes (about 0.56 V per shape), negative voltages step backward, and the selection wraps around. By default a new shape takes over at once; the **Shape changes** menu option can make it wait for the end of the cycle.

*Variation:* sweeps the current shape through its family of forms, 0–100%, default 50%. The **Variation** CV input is scaled by the trimpot below it (−1 to +1, default 0); at full trim, 5 V moves Variation across its whole range.

With nothing patched into the **Variation** jack, its trimpot feeds back **OUT** or randomizes Variation instead: see **CV trimpots** below.

### Transforms (blue plate)

Each transform has a knob, a CV trimpot (−1 to +1, default 0) and a CV input. At full trim, 5 V of CV moves the control across its whole range; the result is clamped to the knob's range. The small pictures beside each knob show what its two extremes do. Transforms take effect immediately, mid-cycle. With nothing patched into a transform's jack, its trimpot feeds back **OUT** or randomizes the transform: see **CV trimpots** below.

*Timing:* moves the middle of the shape earlier or later in the cycle, squeezing one half and stretching the other. Counter-clockwise pulls the midpoint early (down to 2% of the cycle); clockwise pushes it late (up to 98%). Noon is the shape as drawn.

*Lift:* bends the shape's levels up or down without moving its top and bottom. Clockwise lifts everything toward the top, so peaks broaden and dips get narrow; counter-clockwise sinks it toward the bottom, leaving sharp spikes over a low floor. Noon leaves it unchanged.

*Symmetry:* 0–100%, default 0%. Fades the second half of the cycle toward a mirror image of the first half. At 100% the shape goes out and comes back the same way, ending where it started — handy for removing the jump at the loop point.

*Tilt:* adds a slope across the cycle. Clockwise adds a rising ramp (the end of the cycle higher than the start); counter-clockwise a falling one. At either extreme the ramp alone spans the whole output range, so Tilt is the control that pushes the shape out of range — what happens then is set by the **When out of range** menu option. With the default, Fit, the whole shape gets quieter as Tilt grows so that it stays in range.

*Drift:* 0–100%, default 0%. Each new cycle nudges the shape's internal settings by a random amount — at 100%, up to 30% of each setting's range — so every cycle is a slightly different member of the family: a bounce with one more hop, a ratchet a little faster. The sequence of nudges is fixed by the cycle count, so after a **Reset** the same sequence of cycles plays again. (Drift doesn't change Arpeggio's pattern or Chirp's direction, which follow Variation alone.)

### CV trimpots: feedback and random

All six CV trimpots (**Variation** and the five transforms) work the same way. With a cable in the jack, the trimpot scales and inverts the CV as described above. With the jack empty, the two halves of its travel do different things, as the small pictures beside each trimpot show; at noon, nothing happens.

- **Clockwise: feedback.** Skylark's own **OUT** is fed into the control, so the shape modulates itself as it plays; the trimpot sets how much. For Variation, Timing, Lift and Tilt, OUT works as if it were patched into the jack. Symmetry and Drift, whose knobs start fully counter-clockwise, are only pushed upward: the higher OUT is in its range, the more is added.
- **Counter-clockwise: random.** Each cycle the control gets a new random offset; the trimpot sets how big it can be. At full counter-clockwise the offset can reach across the control's whole range from noon (from the bottom, for Symmetry and Drift, which again only go upward).
  - **Variation** and **Drift** jump to their new value at the start of each cycle, so each cycle is a different form throughout.
  - **Timing**, **Lift**, **Symmetry** and **Tilt** glide from one value to the next across the cycle, so the shape bends gradually and never jumps.
  - Like Drift's nudges, the random values follow the cycle count: after a **Reset**, the same sequence plays again.

The **CV and feedback** menu option can sample the CV, or the feedback, once at the start of each cycle instead of following it continuously.

### Clock, Reset and SPEED (green plate)

*Clock knob:* the tempo, 20–300 BPM, default 120. One cycle is a bar of four beats, so a cycle lasts from 12 seconds (20 BPM) down to 0.8 seconds (300 BPM). The knob moves in 0.01 BPM steps, and you can type an exact value such as 121.35. Randomize leaves it alone.

*Clock input:* patch a clock and Skylark takes its tempo from the clock instead of the knob. The **Clock pulses per cycle** menu option sets how many pulses make a bar (default 4, a quarter-note clock). The first pulse snaps the cycle to its start; from the second pulse on, Skylark measures the tempo and then follows the clock with a gentle phase lock, so a wobbly or swung clock doesn't jolt the shape (the tempo is averaged over a bar's worth of pulses). With a clock, a cycle can last anywhere from 0.05 to 60 seconds.

- If the clock stops, Skylark carries on at the last tempo. When the clock comes back after a long gap, its first pulse snaps the cycle back to its start.
- Unpatch the clock to return to the knob's tempo.
- Clock and reset inputs register a pulse when they rise above 1 V.

*Reset button and input:* restart the cycle from the beginning. Reset doesn't fire **EOC**, but it does count as the start of a cycle for the per-cycle menu options, and it restarts Drift's sequence. With a clock patched, the next clock pulse after a reset is treated as the downbeat.

*SPEED:* the outer buttons halve and double how fast the cycle runs, from 1/4x to 4x the tempo: the same shape over four bars, two, one, a half or a quarter; the larger middle button returns it to x1. A trigger at the jack under each outer button does the same as that button (rising edge above 1 V). Everything follows, **EOC** and Drift's per-cycle nudges included. A change waits for the next bar where the old and new cycles both start (up to four bars away when 1/4x is involved), so the shape stays in step with the bar and carries on without a jump; the display shows the new speed dimmed and blinking until then. A reset changes it at once. SPEED is saved with the patch.

### Outputs (gray plate)

- **OUT** — the shape, ±5 V by default (±10 V, 0–5 V or 0–10 V with the **Output range** menu option).
- **EOC** — a 10 V, 1 ms trigger each time the cycle wraps (at SPEED, so twice a bar at x2).

---

## The display

The display draws one full cycle of the output as it currently stands — after Variation, the transforms, the output range and the out-of-range treatment, but before slew — with a vertical playhead and a dot riding the trace. The center line is the middle of the output range: 0 V, 2.5 V (0–5 V) or 5 V (0–10 V). If the end of the cycle doesn't meet its start, the jump is drawn as a vertical line at the left edge, where you hear it.

Along the top: the shape's name on the left (below the trace on MetaModule); on the right, Variation after CV (**VAR 50%**) and the tempo in BPM, measured from the clock when one is patched, followed by the **SPEED** when it isn't x1 (dim and blinking while a change waits for its cycle start).

---

## The shapes

Each picture shows one cycle of the shape with **Variation** at 0%, 50% and 100%, and the other controls at their defaults.

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

Open the context menu on the panel in VCV Rack, or open Options on MetaModule:

- **Output range** — **±5 V** (default), **±10 V**, **0–5 V** or **0–10 V**.
- **When out of range** — what happens when Tilt pushes the shape past the output range:
  - **Fit** (default) — the whole cycle is scaled down just enough to fit, keeping its shape.
  - **Clip** — the excess is cut off flat.
  - **Soft clip** — a smooth saturation curve. It rounds the whole shape a little, even when nothing is out of range.
  - **Fold** — the shape reflects back from the edge of the range.
  - **Wrap** — the shape jumps to the other edge and carries on.
- **Slew** — **Off** (default), **2 ms**, **5 ms** or **20 ms**. Smooths jumps and steps in **OUT**, to take the click out of a hard edge.
- **Clock pulses per cycle** — **1**, **2**, **4** (default), **8**, **16**, **24**, **32**, **48** or **96** clock pulses per bar. Use 4 for a quarter-note clock, 16 for sixteenths, 96 for a 24 PPQN clock.
- **CV and feedback** — **Continuous** (default) follows each CV input, or the feedback into an empty jack, all the time; **Sample once per cycle** reads them at the start of each cycle (or on Reset) and holds them for the whole cycle. The knobs themselves always respond at once, and the random offsets change once per cycle either way.
- **Shape changes** — **Immediately** (default), or **At end of cycle**, which holds any change from the knob, the **‹ ›** buttons or **Shape** CV until the next cycle starts (a Reset counts). The display keeps naming the shape that's playing until then.

Initialize resets these menu settings and SPEED along with the knobs.

On MetaModule, Variation and Drift changes are picked up every 256 samples (about 5 ms) instead of every 64, to save CPU; at LFO rates you won't hear the difference. The display also refreshes less often than in VCV Rack.

---

## Patch ideas

- **Clocked envelopes:** patch **OUT** to a VCA's CV with **Bounce**, **Ratchet** or **Flam**, clock Skylark from your sequencer, and the gesture replays every bar. Set **Clock pulses per cycle** to 1 to replay it on every clock pulse instead.
- **A shape that rewrites itself:** leave the **Variation** jack empty, set **CV and feedback** to **Sample once per cycle**, and turn the Variation trimpot a little clockwise. Each cycle's form is chosen by where the previous cycle ended, so the shape steps through its family in a pattern of its own.
- **Shape per bar:** feed a stepped sequence (about 0.56 V per shape) into **Shape** CV with **Shape changes** set to **At end of cycle**, so each bar plays a different shape and never changes mid-gesture.
- **Living repetition:** a little **Drift** on **Heartbeat**, **Bowed** or **Spring** keeps a repeating gesture from sounding mechanical; send Reset at the start of the song to hear the same "random" cycles every time.
- **Ramps and sawtooths:** turn up **Tilt** and set **When out of range** to **Wrap** or **Fold** for jagged ramps with the shape riding on them.
- **Melodies:** **Arpeggio** or **Devil's staircase** through a quantizer into an oscillator's V/oct. Turn **Lift** to bend the intervals toward the top or bottom notes.
