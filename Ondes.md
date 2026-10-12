# Ondes

**Ondes** is a morphing wavetable oscillator for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule): the hidden oscillator Mutable Instruments Beads plays when nothing is patched into its inputs, running on Beads' own code and wavetables. There are 12 banks of 16 band-limited waveforms. **Position** sweeps smoothly through the waveforms in the current bank, interpolating between neighbours; **Bank** switches between banks with a short crossfade.

<img src="screenshots/Ondes.png" alt="Ondes module" height="500">

---

## Controls

- **Pitch** — −24 to +24 st, plain linear response.
- **V/Oct** — pitch input, summed with the Pitch knob. With the knob at center, 0 V is middle C (C4).
- **Bank** — selects one of the 12 banks. Changing bank crossfades over 25 ms, and the knob has a little hysteresis between banks, so it doesn't chatter at a boundary.
- **Bank CV** + amount — ±5 V CV control of Bank, scaled by the attenuverter below it.
- **Position** — sweeps through the 16 waveforms in the current bank.
- **Position CV** + amount — ±5 V CV control of Position, scaled by the attenuverter below it.
- **Out** — ±5 V mono oscillator output.

---

## The wavetable set

Beads' twelve banks, counter-clockwise to clockwise:

1. Sine harmonics: a sine at each of the first 16 partials.
2. Stacked triangles.
3. Organ drawbar settings.
4. Triangle-saw blends.
5. Bursts and band-pass formants.
6. Vocal formants, clean then distorted.
7. Waveforms from Braids' *Digi* and *Metal* sets.
8. More Braids waveforms.
9. Two-operator FM, carrier at twice the modulator frequency, sweeping the modulation index.
10. Two-operator FM, modulator an octave below the carrier.
11. Two-operator FM, modulator at three times the carrier.
12. Two-operator FM, modulator at eight times the carrier.

---

## Patch ideas

- Slow LFO into **Position CV** for evolving single-cycle movement.
- **Bank CV** from an envelope to jump timbral families on each note.
- An audio-rate oscillator into **Position CV** for FM-like sidebands. (Bank changes crossfade over 25 ms, so audio-rate Bank CV just blurs between banks.)

---
_Ondes runs on the source code and wavetables of [Mutable Instruments Beads](https://github.com/pichenettes/eurorack/tree/master/beads), designed and written by Émilie Gillet and released under the MIT licence._
