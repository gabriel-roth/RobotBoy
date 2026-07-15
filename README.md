# Robot Boy

**Robot Boy** is a plugin for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It currently contains six modules: 

* **Échos:** A delay based on the hidden delay mode of Mutable Instruments Beads. 
* **[Loooop](Loooop.md):** A stereo looper with four independent playheads. 
* **[Löp](Loooop.md#löp):** Loooop's little sibling, with just one playhead. 
* **[MF-20](MF20.md):** A Korg MS-20-style filter.
* **[Ondes](Ondes.md):** A morphing wavetable oscillator.
* **[Particules](Particules.md):** A granular texture processor based on Mutable Instruments Beads.

---

## Échos

A delay based on the hidden delay mode of Mutable Instruments Beads: manual, clocked, or tap-tempo delay times down to audio rates (Karplus-Strong), a freeze beat-slicer, a pitch shifter in the feedback path, tempo-synced repeat shaping, and the four Beads recording-quality modes. (Screenshot and full documentation to come.)

## Loooop

<img src="screenshots/Loooop.png" alt="Loooop module" height="300">

A stereo RAM looper: capture a loop, then play it back with four independent playheads, each with its own speed, position, window size, level, jitter, and pan. Get granular glitches and backward drones simultaneously. → [**Full documentation**](Loooop.md)

## Löp

<img src="screenshots/Lop.png" alt="Löp module" height="300">

The single-playhead version of Loooop, for when you just need a single loop. → [**Full documentation**](Loooop.md#löp)

## MF-20

<img src="screenshots/MF-20.png" alt="MF-20 module" height="300">

An emulation of the Korg MS-20 filter (switchable between the OTA and Korg35 revisions). The cutoffs for the HP and LP stages can be controlled independently or from the shared Total bus. Optional added Drive. → [**Full documentation**](MF20.md)

## Ondes

<img src="screenshots/Ondes.png" alt="Ondes module" height="300">

A morphing wavetable oscillator: two knobs sweep across a two-dimensional set of 24 banks of 8 waveforms each. → [**Full documentation**](Ondes.md)

## Particules

<img src="screenshots/Particules.png" alt="Particules module" height="300">

A granular texture processor based on Mutable Instruments Beads. → [**Full documentation**](Particules.md)
