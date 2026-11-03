# Weaver

*Weaverbirds build intricate nests by interlacing strands of grass.*

**Weaver** is a tempo-synced modulator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule) that builds complex shapes from simple parts. Three LFO-style operators run in fixed rhythmic relationships to the bar, and one of 11 algorithms combines them into a single shape. You can also use an external CV for any operator, and the trimpots can feed each operator back into itself or add random movement.

Weaver is one of Robot Boy’s three complex modulators, with [Skylark](Skylark.md) and [Lyrebird](Lyrebird.md).

<img src="screenshots/Weaver.png" alt="Weaver module" height="500">

---

## Controls

### Operators

- **RATIO:** repetitions per cycle, from 1/8 to x32. RATIO CV steps through the ratios at 1 V per step. By default, changes take effect at the start of a new cycle (see [**Settings**](#settings)).
- **WAVE:** sets the operator’s waveform (see [**Waves**](#waves)).
- **PARAM:** each waveform has a designated parameter modulated by this knob and CV input. See [**Waves**](#waves) for details.
- **PHASE:** offsets the wave within its cycle.
- **LEVEL:** attenuates or inverts the waveform.

### CV trimpots

PARAM, PHASE, and LEVEL each have a CV input and a trimpot. With a cable patched, the trimpot attenuverts the CV; with the trimpot fully clockwise, 5 V spans the control’s range. With no cable:

- **Clockwise:** feeds the operator’s output back into the control.
- **Counter-clockwise:** adds a random offset that glides to a new value each of the operator’s cycles.

### Waves

| Wave | Button color | PARAM |
|---|---|---|
| Sine | green | SKEW: peak position |
| Triangle | yellow | SYMMETRY: rise/fall ratio |
| Ramp up | blue | CURVE: exponential to logarithmic |
| Ramp down | violet | CURVE: exponential to logarithmic |
| Square | red | WIDTH: pulse width |
| Envelope | orange | ATTACK: attack time. Unipolar. |
| Stepped random | white | STEPS: 2–16 random levels per cycle |
| Smooth random | pale blue | DENSITY: 6–16 interpolated points per cycle |
| Stepped random loop | pink | STEPS: 4–16 steps, repeating each cycle |
| Smooth random loop | aqua | DENSITY: 6–16 points, repeating each cycle |
| Input | magenta | SLEW: see [**Input operators**](#input-operators) |

<a id="input-operators"></a>
### Input operators

With WAVE set to **Input**, the operator passes through whatever comes into its PARAM jack. Some things worth noting about this mode:

- The PARAM trimpot behaves as an amplifier: at center, the input voltage is passed through unmodified; turned clockwise, the voltage is amplified up to twice its level; turned counter-clockwise, it’s attenuated to a minimum of 0. (The trimpot’s usual functions of adding feedback or random signal are disabled.)
- At ±5 V, the input voltage reaches full scale.
- The PARAM knob adds a slew of up to 1 s.
- RATIO and PHASE have no effect.

### Algorithms

The **ALGORITHM** **‹ ›** buttons step through the algorithms that combine the operators. ALGORITHM CV offsets the selection, with 10 V spanning all 11.

| Algorithm | Formula | Function |
|---|---|---|
| MIX | A+B+C | Sum. |
| SCALE | (A scaled by B) + C | B sets A’s amplitude, offset by C. |
| SCALE 2 | A scaled by B and C | B and C set A’s amplitude. |
| RING | (A\*B)+C | A times B, offset by C. |
| BEND | (A bent by B) + C | B pushes A’s phase, up to half a cycle, offset by C. |
| MAX | max(A,B)+C | Higher of A and B, offset by C. |
| MIN | min(A,B)+C | Lower of A and B, offset by C. |
| XFADE | (A,B) xfaded by C | C crossfades between A and B. |
| FOLD | fold(A+B)+C | A+B wavefolded, offset by C. |
| S&H | (A sampled by B) + C | A held each time B starts a cycle, offset by C. (An Input’s cycle starts at each rising zero crossing.) |
| GATE | (A gated by B) + C | A passes while B is high, offset by C. (B’s LEVEL sets the threshold.) |

### Presets

A preset sets every operator control and the algorithm; tempo, SPEED, CV trimpots, and menu settings are unchanged. Presets are JSON files in the current folder, starting with 15 factory presets.

- **‹ ›:** load the previous or next preset in the folder.
- **SAVE:** saves the current settings to the folder.
- **DELETE** (red)**:** press twice to delete the current preset’s file.
- The current folder is that of the last preset saved with **Save preset as…** (see [**Settings**](#settings)). In VCV Rack it defaults to `RobotBoy/Weaver` in the Rack user folder, where the factory presets are written on first use.
- On MetaModule, the default folder is the first of `RobotBoy/Weaver`, `Weaver`, or `RobotBoy` found on the SD card, then on a USB drive, then the root of either.

### Timing

- **CLOCK:** sets the tempo in BPM; default is 120. A clock signal at the input overrides the knob.
- **RESET:** returns to the start of the cycle and realigns the operators. With a clock patched, it waits for the next clock pulse.
- **SPEED:** sets the cycle rate, from 1/4 to x4.
    - The left button and trigger input halve the cycle rate.
    - The right button and trigger input double the cycle rate.
    - The center button returns the cycle rate to x1.

By default, a **SPEED** change takes effect at the next point where old and new cycles align, or immediately on RESET (see [**Settings**](#settings)).

### Outputs

- **OUT:** the combined shape, ±5 V by default.
- **INV:** OUT inverted.
- **EOC:** a trigger at the end of each cycle.
- **A, B, C:** each operator’s wave at full scale, before LEVEL.
- The buttons under A, B, and C show or hide each operator’s trace on the display.

---

## Display

The main display shows one cycle of OUT, with a playhead, and each operator as a dashed line. The header shows the preset name (if any), the tempo, and SPEED when not x1. After SAVE or DELETE, a message replaces the preset name for a few seconds.

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
- **Algorithm changes:** **Immediately** (default) or **At end of cycle**.
- **Speed and Ratio change:** **At end of cycle** (default) or **Immediately**.
- **New random loops on reset:** on by default.
- **Hide traces when operator level at 0:** off by default.
- **Algorithm** (MetaModule only)**:** choose the algorithm from a list.
- **Save preset as…:** saves to a name and folder you choose, which becomes the current folder. If a preset in that folder already has these settings, it is renamed instead.
- **Restore factory presets:** writes the 15 factory presets back to the current folder, replacing any with the same names and leaving other presets untouched.
