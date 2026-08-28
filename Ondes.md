# Ondes

**Ondes** is a morphing wavetable oscillator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule), inspired by the hidden wavetable mode on Mutable Instruments Beads. A two-dimensional wavetable set — 24 banks of eight waveforms each, derived from Mutable Instruments Plaits — is navigated with two knobs: **Bank** sweeps across timbral families, **Position** sweeps through the waveforms inside the current family. Both axes are interpolated, so every position between stored waveforms is a blend, not a hard switch.

<img src="screenshots/Ondes.png" alt="Ondes module" height="500">

---

## Controls

- **Pitch** — −24 to +24 st, plain linear response.
- **V/Oct** — pitch input, summed with the Pitch knob.
- **Bank** — selects the timbral family across the 24 banks.
- **Bank CV** + amount — ±5V CV control of Bank, scaled by the attenuverter below it.
- **Position** — selects the waveform within the current bank.
- **Position CV** + amount — ±5V CV control of Position, scaled by the attenuverter below it.
- **Out** — ±5 V mono oscillator output.

---

## The wavetable set

- **Banks 0–7** — mild / additive: sines, quadra waves, combs, pairs, triangles, drawbars.
- **Banks 8–15** — formant-flavoured: trisaw, sawtri, burst, formant, pulse, sine^n.
- **Banks 16–23** — Braids imports: Male, Choir, Digi, Drone, Metal, Fant, plus two banks of Braids extras.

---

## Right-click menu

- **Waveform banks** — toggle **1 - Sines**, **2 - Formants**, **3 - Braids** independently. Disabling a group removes its banks from the Bank knob's range; the remaining enabled groups spread across the knob's full travel. At least one group must stay enabled — the last one can't be unchecked.

---

## Patch ideas

- Slow LFO into **Position CV** for evolving single-cycle movement.
- **Bank CV** from an envelope to jump timbral families on each note.
- Audio-rate V/Oct plus a second oscillator into Bank/Position for FM-like sidebands.

---
_Ondes is inspired by Mutable Instruments Beads, designed by Émilie Gillet. It uses wavetable data derived from Mutable Instruments Plaits by Émilie Gillet, and code from [No Such Texture](https://github.com/thorinside/nosuch_texture) by Neal Sanche._
