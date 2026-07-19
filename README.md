# Robot Boy

**Robot Boy** is a plugin for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It contains: 

* A recreation of Mutable Instruments Beads in three modules: 
	* **[Particules](Particules.md):** A granular texture processor.
	* **[Retours](Retours.md):** An unusual delay and beat-slicer. 
	* **[Ondes](Ondes.md):** A morphing wavetable oscillator.

* **[Loooop](Loooop.md):** A stereo looper with four independent playheads, and its smaller sibling **[Löp](Loooop.md#löp)**. 

* Three characterful stereo filter emulations, documented together in the [**Filters manual**](Filters.md):
	* **[MF-20](Filters.md#mf-20--the-korg-ms-20-filter):** A Korg MS-20-style filter.
	* **[Onbetap](Filters.md#onbetap--the-formanta-polivoks-filter):** A Polivoks-style multimode filter.
	* **[Vespid](Filters.md#vespid--the-edp-wasp-filter):** An emulation of the EDP Wasp state-variable filter.
---

## The Beads modules

A recreation of Mutable Instruments Beads, split across three modules.

### Particules

<img src="screenshots/Particules.png" alt="Particules module" height="300">

A granular texture processor: it records your input and plays back a swarm of short overlapping grains, from lush self-generating clouds to tightly clocked rhythmic slicing. → [**Full documentation**](Particules.md)

### Retours

<img src="screenshots/Retours.png" alt="Retours module" height="300">

A delay and beat-slicer based on the hidden delay mode of Beads: manual, clocked, or tap-tempo delay times down to audio rates (Karplus-Strong), a beat-slicer, a pitch shifter in the feedback path, tempo-synced repeat shaping, and the four Beads recording-quality modes. → [**Full documentation**](Retours.md)

### Ondes

<img src="screenshots/Ondes.png" alt="Ondes module" height="300">

A morphing wavetable oscillator: two knobs sweep across a two-dimensional set of 24 banks of 8 waveforms each. → [**Full documentation**](Ondes.md)

## Loooop + Löp

<img src="screenshots/Loooop.png" alt="Loooop module" height="300">
<img src="screenshots/Lop.png" alt="Löp module" height="300">

A stereo RAM looper: capture a loop, then play it back with four independent playheads, each with its own speed, position, window size, level, jitter, and pan. Get granular glitches and backward drones simultaneously. Löp is the single-playhead version, for when you just need a single loop.

→ [**Full documentation**](Loooop.md)

## Filters

Three characterful stereo filter emulations, each modeled from the behavior of the original circuit. Full details for all three are in the [**Filters manual**](Filters.md).

- **[MF-20](Filters.md#mf-20--the-korg-ms-20-filter):** The Korg MS-20 filter — a high-pass and low-pass in series, switchable between the OTA and Korg35 revisions, with Drive.
- **[Onbetap](Filters.md#onbetap--the-formanta-polivoks-filter):** The Soviet Polivoks filter — five modes, with the quirk that driving it harder suppresses the resonance; calibrated **Tamed** and drifting **Vintage** characters.
- **[Vespid](Filters.md#vespid--the-edp-wasp-filter):** The EDP Wasp filter — a multimode filter with simultaneous LP/BP/HP outputs, riding the edge of self-oscillation (**Tame**) or crossing over it (**Screaming**).
