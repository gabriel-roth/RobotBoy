# Weaver

*Weaverbirds build intricate nests by knotting and interlacing single strands of grass into one structure.*

**Weaver** is a tempo-synced modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule) that builds complex shapes from simple parts. Three LFO-style operators run in fixed rhythmic relationships to the bar, and one of eleven algorithms mixes, multiplies, folds, samples or gates them into a single shape. Any operator can also take an external CV. It is one of Robot Boy's three complex modulators, with [Skylark](Skylark.md) and [Lyrebird](Lyrebird.md).

<img src="screenshots/Weaver.png" alt="Weaver module" height="500">

---

## Controls

### Operators

- **RATIO**: repetitions per cycle, from 1/8 to x32. A change waits for a cycle start that keeps the operator aligned (see **Ratio changes**).
- **WAVE**: the button steps through the waves; its color shows the current one. In VCV Rack, right-click to choose from a list.
- **PARAM**: reshapes the wave (see **Waves**). Default 50%.
- **PHASE**: offsets the wave within its cycle, 0–360°.
- **LEVEL**: −100% to +100%; negative inverts. In some algorithms LEVEL acts as a modulation depth.
- PARAM, PHASE and LEVEL each have a CV input and an attenuverter, centered (off) by default. At full trim, 5 V spans the control's range.

### Waves

| Wave | Button color | PARAM |
|---|---|---|
| Sine | green | SKEW: peak position |
| Triangle | yellow | SYMMETRY: rise/fall ratio |
| Ramp up | blue | CURVE: exponential to logarithmic |
| Ramp down | violet | CURVE: exponential to logarithmic |
| Square | red | WIDTH: pulse width |
| Envelope | orange | ATTACK: attack time. Unipolar. |
| Stepped random | white | STEPS: 1–8 random levels per cycle |
| Smooth random | pale blue | DENSITY: 1–8 interpolated points per cycle |
| Stepped random loop | pink | STEPS: 4–16 steps, repeating each cycle |
| Smooth random loop | aqua | DENSITY: 6–16 points, repeating each cycle |
| Input | magenta | SLEW: see **Input operators** |

The random waves restart identically on RESET. Choosing a loop wave generates a new pattern, saved with the patch.

### Input operators

With WAVE set to **Input**, the operator plays the signal at its PARAM jack (±5 V = full scale). The PARAM trimpot sets the input gain (0–2x) and the PARAM knob a slew of up to 1 s. RATIO and PHASE have no effect.

### Algorithms

The **ALGORITHM** **‹ ›** buttons step through the algorithms; in VCV Rack, right-click either to choose from a list. ALGORITHM CV offsets the selection, 10 V spanning all eleven.

| Algorithm | Formula | Function |
|---|---|---|
| MIX | A+B+C | Sum. |
| SCALE | (A scaled by B) + C | B is a VCA on A, with B's LEVEL as depth. Negative LEVEL inverts the response. |
| SCALE 2 | A\*B\*C | A scaled by both B and C, each LEVEL a depth. |
| RING | (A\*B)+C | Product of A and B. |
| BEND | (A bent by B) + C | B modulates A's phase, up to half a cycle. |
| MAX | max(A,B)+C | Higher of A and B. |
| MIN | min(A,B)+C | Lower of A and B. |
| XFADE | (A,B) xfaded by C | C crossfades from A to B; C's LEVEL is the depth. |
| FOLD | fold(A+B)+C | A+B wavefolded. |
| S&H | (A sampled by B) + C | A sampled at the start of each B cycle, or at B's rising zero crossings if B is an Input. |
| GATE | (A gated by B) + C | A passes while B is positive. |

### Presets

The **PRESET** buttons step through 15 factory presets, which set every operator control and the algorithm. Tempo, SPEED and menu settings are unchanged.

| # | Preset | Algorithm |
|---|---|---|
| 1 | Envelope + triangle x16 (default) | MIX |
| 2 | Ramp switched by square | SCALE |
| 3 | Swell with gated ripple | SCALE |
| 4 | Sampled falling ramp | S&H |
| 5 | Struck ring | RING |
| 6 | Grouped swells, rising | SCALE 2 |
| 7 | Bitten sine | MIN |
| 8 | Square to sine and back | XFADE |
| 9 | Folded rising ramp | FOLD |
| 10 | Breathing triangle | BEND |
| 11 | Phase-bent sine | BEND |
| 12 | Max with random steps | MAX |
| 13 | Random accents, fading | SCALE 2 |
| 14 | Random stutter | GATE |
| 15 | Random swell | MIX |

### CLOCK, RESET and SPEED

- **CLOCK**: tempo, 20–300 BPM, default 120. A clock at the input overrides the knob.
- **RESET**: the button or a trigger returns to the cycle start and realigns the operators.
- **SPEED**: the outer buttons, or triggers at their inputs, halve or double the cycle rate (1/4x to 4x); the center button returns to 1x. A change takes effect at the next point where old and new cycles align, or immediately on RESET.

### Outputs

- **OUT**: the combined shape, ±5 V by default.
- **INV**: OUT inverted.
- **EOC**: a trigger at the end of each cycle.
- **A**, **B**, **C**: each operator's wave at full scale, before LEVEL and the algorithm.
- The buttons under A, B and C show or hide each operator's trace on the display.

---

## Display

- **Main display:** one cycle of OUT, with a playhead, and each operator × LEVEL as a dashed line (A amber, B teal, C pink). The header shows the preset name when the settings match one, the tempo, and SPEED when not x1. On MetaModule it also names the algorithm, and the preset name moves below the trace.
- **ALGORITHM display:** the algorithm's name and formula.
- **Operator displays:** one cycle of the wave, with PARAM's function; the RATIO.

---

## Context menu

- **Output range**: ±5 V (default), ±10 V, 0–5 V, 0–10 V.
- **When out of range**: **Fit** (default; scales the whole cycle down to fit), **Clip**, **Soft clip**, **Fold** or **Wrap**.
- **Slew**: Off (default), 2 ms, 5 ms, 20 ms.
- **Clock pulses per cycle**: pulses per bar at the CLOCK input, 1–96 (default 4).
- **Algorithm changes**: **Immediately** (default) or **At end of cycle**.
- **Ratio changes**: **At end of cycle** (default) or **Immediately**.
- **New random loops on reset**: on by default.
- **Hide traces when operator level at 0**: off by default.
- **Algorithm** (MetaModule only): choose the algorithm from a list.
