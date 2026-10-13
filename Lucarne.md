# Lucarne

**Lucarne** is a display expander for [Particules](Particules.md), for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It shows what Particules is doing with its buffer: the recorded audio, the write head, and every grain as it plays. It has no controls and no jacks.

---

## What it shows

**The buffer.** A waveform band across the middle of the display shows Particules' buffer as a ring. The write head is a bright vertical line sweeping left to right, overwriting the picture as it goes, just as it overwrites the audio. When Freeze is on, the line dims and stops.

**The grains.** Each grain is a line above or below the band, covering the stretch of buffer that grain plays. A grain pitched up plays through the buffer faster, so its line is longer.

- *Height shows pitch.* Unison sits just above the band; higher pitches sit further up, and lower pitches go below the band. The display covers ±2 octaves; anything beyond that pins to the edge.
- *A brighter cursor* moves along the line as the grain plays.
- *Colour and fade.* Lines are tinted with the current Quality colour and brighten with the grain's envelope. After a grain ends, its line fades out over a moment. Very short grains still flash briefly.
- *Direction.* Reversed grains (Size below about 9 o'clock) run right to left.

Changing Quality, switching between mono and stereo input, or clearing the buffer clears the display, which then fills in again as Particules records.

---

## Setup

- **VCV Rack:** place Lucarne directly to the right of Particules. Until it is, the display reads "Place right of Particules".
- **MetaModule:** add Lucarne to the patch, then attach it from Particules' **Expanders…** menu (this needs firmware 2.4 or later). Tap the display to see it fullscreen. Until it is attached, the display reads "Add from Particules' Expanders menu".

---

## Notes

- Lucarne works with Particules in Beads mode only. In classic mode it shows "Beads mode only".
- It is display-only: it doesn't change Particules' sound in any way.
