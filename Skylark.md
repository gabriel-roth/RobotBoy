# Skylark

**Skylark** is a shape modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). Once per bar it plays a structured gesture — a bouncing ball, a ratchet, a heartbeat, a chirp, a fractal line — and loops it in time with your tempo or clock. Each of its 18 shapes is a whole family of related forms that the **Variation** knob sweeps through, and five transform controls bend the timing, curve, symmetry and slope of whatever is playing. Use it as a clocked LFO, a repeating envelope, or a slowly evolving modulation source.

Skylark is one of Robot Boy's three modulators, with [Weaver](Weaver.md) and [Lyrebird](Lyrebird.md).

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

Along the top: the shape's name on the left; on the right, Variation after CV (**VAR 50%**) and the tempo in BPM, measured from the clock when one is patched, followed by the **SPEED** when it isn't x1 (dim and blinking while a change waits for its cycle start).

---

## The shapes

Variation sweeps every setting named here together; which way each one goes depends on the shape.

### Gestures

1. **Bounce** — a dropped ball: a run of arcs, each lower and shorter than the last, settling at the bottom. Variation changes the bounciness, the number of bounces (3–12) and how squashed (flat-topped) each arc is.
2. **Spring** — a struck spring ringing down: a decaying oscillation that starts at full swing, from the center, or anywhere between. Variation changes the ringing speed (from 1.5 to 14 swings per cycle), the damping and the starting kick.
3. **Ratchet** — a burst of 3–16 sharp hits that speed up or slow down across the cycle, each a little quieter than the last. Variation also changes how quickly each hit dies away.
4. **Flam** — one to three quiet grace hits just ahead of a main hit, which then decays. Variation changes the gap between hits, their number and level, and the decay.
5. **Bowed** — a bowed note: smooth swell, a sustain with vibrato that fades in, smooth release. Variation changes the attack, vibrato speed and depth, and where the release begins.
6. **Stutter** — 4–24 gated pulses under a decaying envelope. Variation changes the number of pulses, their width and the decay.
7. **Heartbeat** — an ECG trace around a center baseline: a small bump, a sharp spike that swings both ways, then a broad hump. Variation changes the size and width of each part and the timing of the last hump.

### Cycles

8. **Harmonics** — a fundamental with a ripple of overtones whose brightness opens and closes across the cycle (or the other way round). Variation changes the number of partials, where their peak sits, odd-against-even balance and the sweep.
9. **Folded sine** — two sines through a wavefolder, swelling two to four times a cycle: plain between swells, folded at each one. Variation changes the drive, the second sine's pitch and level, the swell depth and lean.
10. **FM** — a cascaded FM wave whose brightness and level decay or grow across the cycle, like a struck or bowed FM tone. Variation changes the ratios, the depths and the direction of the sweep.
11. **Chirp** — a wave that changes speed across the cycle, under a gentle swell. Variation sweeps it from speeding up, through fastest in the middle, to slowing down, and changes the range of speeds.
12. **Beating** — three closely tuned waves whose interference makes a slow beat with a faster one inside it, rectified at times into separate pulses. Variation changes the tunings, depths and how pulse-like it gets.

### Steps

13. **Arpeggio** — 3–12 stepped notes. Variation picks the pattern in thirds of its travel — **Up**, **Up-down**, **Down** — and within each third sweeps the number of notes, their span and an optional glide between them. Patch it through a quantizer for melodies.
14. **Ringing steps** — two to six levels in turn, each change ringing like a struck object before it settles. Variation changes the number of steps, the ringing speed and how fast it dies away.
15. **Devil's staircase** — a fractal staircase rising in flat steps of many sizes. Variation changes the step pattern, its depth (2–6 levels of detail) and the height of the middle step.

### Chaos

16. **Random nodes** — 3–24 random points joined into a looping line. Variation changes the number of points, how evenly they're spaced, and how they're joined: steps, straight lines or smooth curves.
17. **Fractal line** — a jagged, mountain-range line. Variation changes its roughness and its depth of detail.
18. **Glitch** — 6–24 segments of random length, some holding a level, others breaking into bursts of rapid random steps. Variation changes the number of segments, how many burst and how fast.

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
