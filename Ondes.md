# Ondes

**Ondes** is a morphing wavetable oscillator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). A two-dimensional wavetable set — 24 banks of 8 waveforms each, derived from Mutable Instruments Plaits — is navigated with two knobs: **Bank** sweeps across timbral families, **Position** sweeps through the waveforms inside the current family. Both axes are interpolated, so every knob position between the stored waveforms is a smooth blend rather than a hard switch.

<img src="screenshots/Ondes.png" alt="Ondes module" height="500">

---

## Controls

- **Pitch** — −24…+24 st. Coarse/fine pitch, notched at octaves, fifths, and unison for easy tuning (shared with Particules).
- **V/Oct** — 1 V/octave pitch input, summed with the Pitch knob.
- **Bank** — 0–1. Selects the timbral family across the 24 banks.
- **Bank CV** + amount — ±5 V CV control of Bank, scaled by the attenuverter below it.
- **Position** — 0–1. Selects the waveform within the current bank.
- **Position CV** + amount — ±5 V CV control of Position, scaled by the attenuverter below it.
- **Out** — ±5 V mono oscillator output.

---

## The wavetable set

- **Banks 0–7** — mild / additive: sines, combs, pairs, triangles, drawbars.
- **Banks 8–15** — formant-flavoured: trisaw, burst, formant, pulse, sine^n.
- **Banks 16–23** — Braids imports: Male, Choir, Digi, Drone, Metal, Fant.

---

## Patch ideas

- Slow LFO into **Position CV** for evolving single-cycle movement.
- **Bank CV** from an envelope to jump timbral families on each note.
- Audio-rate V/Oct plus a second oscillator into Bank/Position for FM-like sidebands.

---

*Wavetable data derived from Mutable Instruments Plaits (© 2016 Emilie Gillet, MIT/Apache 2.0).*
