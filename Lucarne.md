# Lucarne

**Lucarne** is a display expander for [Particules](Particules.md) and [Retours](Retours.md), for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It shows what the module is doing with its buffer: the recorded audio, the write head, and either every grain as it plays (Particules) or every delay tap (Retours). It has no controls and no jacks.

---

## What it shows with Particules

**The buffer.** A waveform band across the middle of the display shows Particules' buffer as a ring. The write head is a bright vertical line sweeping left to right, overwriting the picture as it goes, just as it overwrites the audio. When Freeze is on, the line dims and stops.

**The grains.** Each grain is a line above or below the band, covering the stretch of buffer that grain plays. A grain pitched up plays through the buffer faster, so its line is longer.

- *Height shows pitch.* Unison sits just above the band; higher pitches sit further up, and lower pitches go below the band. The display covers ±2 octaves; anything beyond that pins to the edge.
- *A brighter cursor* moves along the line as the grain plays.
- *Color shows stereo position.* Each grain is colored by where Particules pans it: blue for hard left, warm white for center, orange for hard right. Beads picks a random pan for each grain, within a range that widens as more grains overlap and as you add randomness (the attenurandomizers, or Density clockwise of 12 o'clock). With a steady pulse of grains and no randomness, every grain is centered and the display is all white; as you add randomness, the colors fan out.
- *Brightness and fade.* Lines brighten with the grain's envelope. After a grain ends, its line fades out over a moment. Very short grains still flash briefly.
- *Direction.* Reversed grains (Size turned counter-clockwise of about 10 o'clock) run right to left.

In VCV, a key under the display sums this up. On MetaModule the display is smaller (16:9, to fill the screen when you tap it) and the faceplate has no key.

Changing Quality, switching between mono and stereo input, or clearing the buffer clears the display, which then fills in again as Particules records.

---

## What it shows with Retours

**The buffer.** The waveform band and the write head work as with Particules, and Freeze dims and stops the line in the same way.

**The delay taps.** Each audible delay tap is a line above or below the band, running from the point where that tap reads the buffer to the write head, so its length is the delay time. Turn Time and the lines stretch and shrink.

- *A bright cursor* sits at the read point, where the tap picks up the audio.
- *Brightness follows the tap's level,* and pulses with Shape.
- *Color shows the tap's ratio.* Taps of the main ratio are warm white; taps of the second ratio are green. Unclocked, the second ratio is the 0.77 taps that fade in as Interval passes noon; clocked, it is the neighboring division.
- *Height shows the row.* Each side of the band has two rows (lanes): main-ratio taps sit in the upper row and second-ratio taps in the lower. Each ratio has two taps (adjacent multipliers that crossfade as Time turns), and each tap keeps its own row, so a line stays put while you play.

**Slice.** With Slice on, the tape stops and each looping play head draws as a line over the slice it plays, with a cursor running along it. Turn Slice off and the display goes back to the running taps.

In VCV, the key under the display changes to match the module Lucarne is attached to: Length shows delay or slice, Height shows one row per tap, Color shows the tap ratio, and Brightness shows level. On MetaModule there is no key.

---

## Setup

- **VCV Rack:** place Lucarne directly to the right of Particules or Retours. Until it is, the display reads "Place right of Particules or Retours".
- **MetaModule:** add Lucarne to the patch, then attach it from the **Expanders…** menu of Particules or Retours (this needs firmware 2.4 or later). Tap the display to see it fullscreen. Until it is attached, the display reads "Add from the Expanders menu of Particules or Retours".

---

## Notes

- Lucarne works with Particules and Retours in Beads mode only. In classic mode it shows "Particules is set to Classic mode. Lucarne only works in Beads mode." or "Retours is set to Classic mode. Lucarne only works in Beads mode."
- It is display-only: it doesn't change the sound of Particules or Retours in any way.
