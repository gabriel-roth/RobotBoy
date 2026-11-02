# Weaver

**Weaver** is a shape modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It runs three *operators* — simple waves, or outside CV — at whole-number ratios of a shared cycle, and weaves them into one modulation shape with one of eleven *algorithms*: mixing, multiplying, bending, folding, sampling, gating. Use it as a tempo-synced LFO with far more character than a single wave, as a rhythmic envelope generator, or as a processor for CV you already have. Fifteen factory presets get you started.

Weaver is one of Robot Boy's three complex modulators, with [Skylark](Skylark.md) and [Lyrebird](Lyrebird.md).

<img src="screenshots/Weaver.png" alt="Weaver module" height="500">

---

## How it works

Everything runs on one master **cycle**, four beats long: at the default 120 BPM, a cycle lasts two seconds. Weaver runs from its own tempo or follows a patched clock.

Each of the three operators, **A**, **B** and **C**, plays a wave at a **ratio** of that cycle — **x1** once per cycle, **x4** once per beat, **1/2** once every two cycles — so the operators always line up with each other and with the bar. Each one has a **PARAM** control that reshapes its wave, a **PHASE** offset, and a **LEVEL**.

The **algorithm** decides how the three operators combine. In most algorithms A and B interact in some way and C is simply added on top. The ALGORITHM display names the one running and shows its formula.

Finally, the result is brought back into the output range (by default it's scaled down to fit, so nothing clips) and sent to **OUT**, with an inverted copy at **INV** and a trigger at **EOC** at the end of every cycle. Each operator's own wave is available too, at the **A**, **B** and **C** outputs.

---

## Quick start

1. **Patch OUT** to whatever you want to modulate. Weaver starts on preset 1 at 120 BPM, so it's moving straight away: an envelope once per bar, with a fast triangle ripple on top.
2. **Step through the presets** with the **PRESET** buttons to hear what the algorithms do. The display names the preset you're on.
3. **Patch a clock** into **CLOCK** to lock Weaver to your tempo. By default it expects four pulses per cycle — a quarter-note clock, one cycle per bar.
4. **Turn the operators' knobs.** **RATIO** sets how fast each one runs against the cycle, the **WAVE** button picks its wave, and **LEVEL** sets how much of it you get.

---

## Display

The main display shows one full cycle of the output shape as a solid white line, after range and out-of-range handling but before slew. A vertical playhead runs across it, with a dot on the shape at the current value.

- **Operator traces:** each operator, scaled by its LEVEL, is drawn as a dashed line in its color — **A** amber, **B** teal, **C** pink. An operator at LEVEL zero draws as a flat line, unless **Hide traces when operator level at 0** is on in the context menu. The trace buttons under the A, B and C outputs hide or show each one.
- **Header:** the **A B C** legend on the left (a letter dims when its trace is hidden; on MetaModule, followed by the algorithm's name), the preset number and name in the middle when the settings exactly match a preset (blank after any edit; below the trace on MetaModule), and the tempo on the right, followed by the **SPEED** when it isn't x1 (dim and blinking while a change waits for its cycle start).
- **Input operators:** an operator set to the **Input** wave has no formula to draw ahead, so its trace, and the result while any operator is Input, show the last cycle as it actually played.

The ALGORITHM display shows the running algorithm's name, with its formula underneath. Each operator row has two small displays of its own: one cycle of the operator's wave, with the name of what **PARAM** does for that wave above it (with an arrow pointing to the PARAM knob) and the wave's name below; and the **RATIO** readout above the RATIO knob, which shows **IN** for an Input operator.

---

## Controls

### Operators

The three rows on the right are identical, one per operator.

- **RATIO** — how many times the operator's wave repeats per cycle: **1/8**, **1/4**, **1/2**, **x1**, **x2**, **x3**, **x4**, **x5**, **x6**, **x7**, **x8**, **x12**, **x16**, **x24** or **x32**. By default a new ratio waits for the start of the next cycle (the readout shows it dimmed until then), so the wave changes without a jump and stays lined up with the cycle; 1/2, 1/4 and 1/8 can wait up to two, four or eight cycles for a start where they line up. The context menu can make changes immediate instead. The jack below adds 1 V per step (rounded, and held at the ends of the list).
- **WAVE** — click the lit button to step to the next wave; its color shows which wave is chosen. In VCV Rack, right-click it to pick from the full list. The waves, with what **PARAM** does for each:
  - **Sine** (green) — PARAM is **SKEW**: moves the peak earlier or later in the cycle. 50% is a pure sine.
  - **Triangle** (yellow) — **SYMMETRY**: from a fast rise and slow fall, through a triangle at 50%, to a slow rise and fast fall.
  - **Ramp up** (blue) and **Ramp down** (violet) — **CURVE**: bows the ramp. 50% is a straight line; either side bends it into an exponential or logarithmic curve.
  - **Square** (red) — **WIDTH**: pulse width, 2% to 98%.
  - **Envelope** (orange) — **ATTACK**: an attack-decay shape that rises from zero, peaks and decays back to zero over each operator cycle. PARAM sets how much of the cycle the attack takes. Envelope is unipolar: it only goes positive, where the other waves swing both ways.
  - **Stepped random** (white) — **STEPS**: 1 to 8 random levels per operator cycle. The sequence never repeats, but it's the same every time from a Reset.
  - **Smooth random** (pale blue) — **DENSITY**: a random line gliding through 1 to 8 points per operator cycle. Like Stepped random, it's the same every time from a Reset.
  - **Stepped random loop** (pink; **STEPPED LOOP** on the display) — **STEPS**: a random pattern of 4 to 16 steps that repeats every operator cycle.
  - **Smooth random loop** (aqua; **SMOOTH LOOP** on the display) — **DENSITY**: a smooth random pattern through 6 to 16 points that repeats every operator cycle, joining up at the loop point.
  - **Input** (magenta) — **SLEW**: the operator plays outside CV; see **Input operators** below.
  - Choosing a loop wave makes a new random pattern for that operator. The pattern is saved with the patch.
- **PARAM** — reshapes the wave as listed above, 0–100%, default 50%. The wave display above it names what it does.
- **PHASE** — shifts the operator's wave later in its cycle, 0–360°.
- **LEVEL** — the operator's amount, −100% to +100%. Negative levels flip the wave upside down. In some algorithms LEVEL acts as a modulation depth instead (see **Algorithms**).
- **CV inputs and trimpots** — **PARAM**, **PHASE** and **LEVEL** each have a trimpot and a jack below the knob. The trimpot is an attenuverter, centered by default, so **a patched CV does nothing until you turn its trimpot**. Fully clockwise, 5 V moves the control across its whole range; counter-clockwise inverts the CV. PHASE CV wraps around; PARAM and LEVEL stop at the ends of their ranges.

### Input operators

Set an operator's **WAVE** to **Input** and it plays whatever is patched into that row's **PARAM** jack instead of a wave. Things change in that row:

- The **PARAM** jack is the signal: ±5 V corresponds to a full-scale wave.
- The **PARAM** trimpot is the input gain, from 0 to 2×, unity at center.
- The **PARAM** knob is a slew on the input, from off (fully counter-clockwise) to about 1 second.
- **RATIO** and **PHASE** do nothing; the ratio readout shows **IN**.
- **LEVEL** and its CV work as usual.

This turns Weaver into a CV processor: use another module's LFO or sequence as an operator and scale, ring-modulate, bend, fold, sample, gate or crossfade it with Weaver's own waves.

### Algorithms

The **ALGORITHM** button steps through the eleven algorithms; in VCV Rack, right-click it to pick from the list. The jack beside it adds CV: 10 V spans all eleven (\~0.9 V per step), counting on from the button's choice and wrapping around. By default a new algorithm takes over immediately; the context menu can hold it until the end of the cycle.

In the descriptions below, A, B and C mean each operator's wave times its LEVEL unless stated otherwise.

- **MIX** (A+B+C) — the three added together.
- **SCALE** ((A scaled by B) + C) — B works as a VCA on A, with B's LEVEL as the depth: at zero A passes untouched; turned up, A is pulled toward zero at B's low points; turned negative, at B's high points. C is added.
- **SCALE 2** (A\*B\*C) — as SCALE, with both B and C scaling A, each with its LEVEL as depth. Nothing is added.
- **RING** ((A\*B)+C) — A multiplied by B, plus C.
- **BEND** ((A bent by B) + C) — B pushes A's phase back and forth, by up to half a cycle at full LEVEL, so A's wave speeds up and slows down in time with B. C is added. An Input operator on A can't be bent and passes through.
- **MAX** (max(A,B)+C) — whichever of A and B is higher, plus C.
- **MIN** (min(A,B)+C) — whichever of A and B is lower, plus C.
- **XFADE** ((A,B) xfaded by C) — crossfades between A and B, following C. C's LEVEL is the crossfade depth (its sign doesn't matter): at zero you get an even blend of A and B; at full, C sweeps all the way from A to B.
- **FOLD** (fold(A+B)+C) — A and B added, and anything beyond full scale folded back into range; turn LEVELs up for more folds. C is added after the fold.
- **S&H** ((A sampled by B) + C) — A is sampled at the start of each of B's cycles and held, so B's RATIO and PHASE set the sample rate. When B is an Input, A is sampled on each rising zero crossing of B. B's LEVEL has no effect. C is added.
- **GATE** ((A gated by B) + C) — A passes while B is positive and drops to zero while it's negative (the other way round with a negative LEVEL on B; always open at B LEVEL zero). C is added.

The unipolar Envelope wave makes an especially good B in SCALE and SCALE 2, and an especially good C in XFADE.

### Presets

The two **PRESET** buttons step backward and forward through the 15 factory presets, wrapping around; if the settings match no preset, forward loads preset 1 and back loads preset 15. A preset sets every operator's wave, ratio, PARAM, PHASE and LEVEL, and the algorithm, all at once, even when the menu holds algorithm or ratio changes. It lines the operators up with the cycle, so the preset plays its real shape from wherever the cycle is, and the outputs glide to it over 10 ms. The tempo, SPEED, context-menu settings and trace buttons are left alone. In VCV Rack a preset load can be undone.

The presets:

1. **Envelope + triangle x16** — a once-per-bar envelope with a fast triangle ripple, dipping below the centre as it dies away. The default.
2. **Ramp switched by square** — a rising ramp chopped by a fast square.
3. **Swell with gated ripple** — a fast triangle switched on and off by a square, riding on a slow sine swell.
4. **Sampled falling ramp** — a falling ramp sampled into steps.
5. **Struck ring** — a sharp envelope ringing against a fast sine.
6. **Grouped swells, rising** — a fast sine in groups set by a triangle, swelling with a rising ramp.
7. **Bitten sine** — a sine with bites taken out by a falling ramp.
8. **Square to sine and back** — a crossfade between a fast square and a sine.
9. **Folded rising ramp** — a rising ramp and sine, folded.
10. **Breathing triangle** — a triangle bent by a slow sine.
11. **Phase-bent sine** — a sine bent by a triangle, with a little smooth randomness.
12. **Max with random steps** — the higher of a sine and a triangle, plus random steps.
13. **Random accents, fading** — a fast sine at random levels, dying away over the cycle.
14. **Random stutter** — a very fast triangle gated on and off by random steps.
15. **Random swell** — smooth randomness with random steps and a fast sine ripple.

### Clock and reset

- **CLOCK** knob — the tempo, 20–300 BPM, default 120, in 0.01 BPM steps (type a value into its field in VCV Rack for an exact tempo). One cycle is four beats.
- **CLOCK** jack — an external clock. Weaver aligns to the cycle start on the first pulse, takes the tempo from the second, then follows the clock smoothly, averaging over a cycle's worth of pulses so swing doesn't wobble the shape. While a clock is in charge the knob does nothing and the display shows the clock's tempo. If the clock stops, Weaver carries on at the last tempo, and realigns when it restarts; unpatch it and the knob takes over again. Set how many pulses make a cycle in the context menu.
- **RESET** button and jack — returns to the start of the cycle and lines all the operators up again from there. A reset doesn't fire EOC. The jack responds to a rising edge above 1 V.
- **SPEED** — the outer buttons halve and double how fast the cycle runs, from 1/4x to 4x the tempo: the same shape over four bars, two, one, a half or a quarter (at the default four beats a cycle); the larger middle button returns it to x1. A trigger at the jack under each outer button does the same as that button (rising edge above 1 V). Everything follows, EOC included. A change waits for the next bar where the old and new cycles both start (up to four bars away when 1/4x is involved), so the shape stays in step with the bar and carries on without a jump; the display shows the new speed dimmed and blinking until then. A reset changes it at once. SPEED is saved with the patch.

### Outputs

- **OUT** — the woven shape. ±5 V by default, or ±10 V, 0–5 V or 0–10 V (see **Output range** in the context menu).
- **INV** — OUT turned upside down, mirrored around the centre of the range (0 V, or 2.5 V in 0–5 V and 5 V in 0–10 V).
- **EOC** — a 10 V, 1 ms trigger at the end of every cycle (at SPEED, so twice a bar at x2).
- **A**, **B**, **C** — each operator's own wave at full scale, ignoring LEVEL and the algorithm: three synced LFOs alongside the main output. In the ±5 V and ±10 V ranges the Envelope wave covers only the upper half (0 to +5 V or +10 V); in the 0–5 V and 0–10 V ranges it spans the full range like the other waves.
- **Trace buttons** — the lit buttons under the **A**, **B** and **C** outputs show or hide that operator's dashed trace on the main display. They only affect the display, and randomizing leaves them alone.

---

## Context menu

Open the context menu on the panel in VCV Rack, or open Options on MetaModule:

- **Output range** — **+-5 V** (the default), **+-10 V**, **0-5 V** or **0-10 V**, for OUT, INV and the operator outputs.
- **When out of range** — what happens when the woven shape goes past full scale, as stacked LEVELs easily make it do:
  - **Fit** (the default) — the whole cycle is scaled down just enough to fit, keeping its shape; a shape already in range is left alone. With an Input operator, Weaver can't see a cycle ahead, so it fits to the loudest of this cycle and the last, and the level can settle over a cycle. The operator outputs clip instead.
  - **Clip** — flattened at the edges.
  - **Soft clip** — rounded off as it nears the edges; this also softens a shape that stays in range.
  - **Fold** — folded back from the edges.
  - **Wrap** — jumps to the opposite edge and carries on.
- **Slew** — **Off**, **2 ms**, **5 ms** or **20 ms** of smoothing on all the outputs except EOC, to take the click off steps and jumps.
- **Clock pulses per cycle** — how many CLOCK pulses make one cycle: **1**, **2**, **4** (the default), **8**, **16**, **24**, **32**, **48** or **96**. A cycle is four beats, so use 4 for a quarter-note clock, 16 for sixteenths and 96 for a 24 PPQN clock.
- **Algorithm changes** — **Immediately** (the default) or **At end of cycle**. At end of cycle, a new algorithm from the button or its CV waits for the start of the next cycle (a reset counts), so the shape never changes mid-bar. Presets and patch loads still switch at once.
- **Ratio changes** — **At end of cycle** (the default) or **Immediately**. At end of cycle, a new ratio from the knob or its CV waits for a cycle start where the operator stays lined up with the cycle. Immediately, the wave carries on from where it was at the new speed, without a jump, but stays shifted against the cycle until the next reset or preset load.
- **New random loops on reset** — on by default. When on, every reset gives the Stepped random loop and Smooth random loop operators new patterns; when off, they keep theirs.
- **Hide traces when operator level at 0** — off by default. When on, the main display leaves out an operator whose LEVEL is zero.
- **Algorithm** (MetaModule only) — a list to pick the algorithm directly, since MetaModule has no right-click menu on the button.

In VCV Rack, **Initialize** returns Weaver to preset 1, SPEED to x1, and these settings to their defaults. **Randomize** sets every knob except the tempo, picks new waves for all three operators (never Input, which needs a cable to be heard), and makes new loop patterns; it keeps the current algorithm.

---

## Patch ideas

- **Bar-locked modulation:** clock Weaver from your sequencer and use **EOC** to restart a sequence or fire an envelope on every bar. Set a RATIO to **1/2** or **1/4** for a shape that spans two or four bars.
- **Three related LFOs:** use the **A**, **B** and **C** outputs as three synced LFOs at different ratios, with **OUT** as a fourth that combines them.
- **Rhythmic VCA:** set A to **Input**, patch an LFO or sequence into A's **PARAM** jack, choose **SCALE** or **GATE**, and give B a **Square** or **Stepped random loop** to chop it in time.
- **Clocked sample and hold:** set A to **Input**, patch any CV into its PARAM jack and choose **S&H**: B's RATIO picks how many samples per bar, and B's PHASE shifts them off the beat.
- **Changing every bar:** set **Algorithm changes** to **At end of cycle** and step the **ALGORITHM** CV from a sequencer, so each bar gets a different weave of the same three operators.
- **Repeating random melody:** give A a **Stepped random loop**, set **FOLD** or **MIX**, and send OUT through a quantizer. Choose another wave and go back to the loop for a new pattern, or reset whenever you want a change (with **New random loops on reset** on, the default).
