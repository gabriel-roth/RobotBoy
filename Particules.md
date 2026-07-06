# Particules

Particules is a granular audio processor for VCV Rack and 4ms MetaModule — a texture synthesizer that creates soundscapes by playing back layered, delayed, transposed, and enveloped fragments of sound ("grains") taken continuously from the incoming audio signal. It covers the range from autonomous texture generation to tightly sequenced or rhythmic granular playing.

It is based on [No Such Texture](https://github.com/thorinside/nosuch_texture?tab=readme-ov-file), which is itself a reimplementation of Mutable Instruments Beads. Thanks and credit to Émilie Gillet for the original Beads design, and to Neal Sanche for the No Such Texture code. Panel knob graphics copyright 2021 Matt Demanett, licensed under [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).

---

Grains are controlled via knobs and CV inputs for time, position, size, shape, pitch, and density, all fed through "attenurandomizers." Particules also offers four audio quality settings with different tonal characters, a nice reverb, and a FREEZE function.

For full details on the underlying granular engine, consult the [Beads manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/beads/manual/).

---

## Differences from Beads

Particules covers the granular functionality of hardware Beads, but differs from it in a few ways.

| Feature | Beads | Particules |
|---|---|---|
| SEED button modes | Three modes: Latched, Clocked, and Gated | Latched and Clocked are one mode (determined by whether a cable is in the SEED CV input); no manual triggers |
| CV control for Feedback, Dry/Wet, Reverb | Single shared "macro" CV input and attenurandomizer | Separate CV input and attenurandomizer for each parameter |
| PITCH quantization | Not available | Context menu: octaves, or octaves and fifths |
| Input gain adjustment | Hold button [A] and turn the Feedback knob [J] | Context menu |
| Grain trigger output on R | Hold [M] and press SEED [C] | Context menu |
| FREEZE buffer persistence | Buffer saved after 10 seconds and restored on power-on | Not supported |
| Grain indicator LED | None | LED under DENSITY CV input flashes on each grain |
