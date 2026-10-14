# Lucarne

**Lucarne** is a display expander for [Particules](Particules.md) and [Retours](Retours.md), for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It shows what the module is doing with its buffer: the recorded audio, and either every grain as it plays with the write head (Particules) or a scrolling scope of every delay tap (Retours). It has no controls and no jacks.

---

## What it shows with Particules

**The buffer.** A waveform band across the middle of the display shows Particules' buffer as a ring. The write head is a bright vertical line sweeping left to right, overwriting the picture as it goes, just as it overwrites the audio. When Freeze is on, the line dims and stops.

**The grains.** Each grain is a line above or below the band, covering the stretch of buffer that grain plays. A grain pitched up plays through the buffer faster, so its line is longer.

- *Height shows pitch.* Unison sits just above the band; higher pitches sit further up, and lower pitches go below the band. The display covers ±2 octaves; anything beyond that pins to the edge.
- *A brighter cursor* moves along the line as the grain plays.
- *Color shows stereo position.* Each grain is colored by where Particules pans it: blue for hard left, warm white for center, orange for hard right. Beads picks a random pan for each grain, within a range that widens as more grains overlap and as you add randomness (the attenurandomizers, or Density clockwise of 12 o'clock). With a steady pulse of grains and no randomness, every grain is centered and the display is all white; as you add randomness, the colors fan out.
- *Brightness and fade.* Lines brighten with the grain's envelope. After a grain ends, its line fades out over a moment. Very short grains still flash briefly.
- *Direction.* Reversed grains (Size turned counter-clockwise of about 10 o'clock) run right to left.

In VCV, a key under the display sums this up. Until Lucarne is attached to Particules or Retours, the key area shows a Lucarne emblem instead. On MetaModule the display is smaller (16:9, to fill the screen when you tap it) and the faceplate has no key.

Changing Quality, switching between mono and stereo input, or clearing the buffer clears the display, which then fills in again as Particules records.

---

## What it shows with Retours

**The scope.** While Retours is running, Lucarne shows Retours' recent audio as a waveform scrolling from right to left, with the newest sound at the right edge. The waveform fills about 70% of the display height.

**The delay taps.** Each audible tap is a full-height vertical line at its delay: the farther left the line, the longer the echo. Taps of the main ratio are warm white; taps of the second ratio are green. Unclocked, the second ratio is the 0.77 taps that fade in as Interval passes noon; clocked, it is the neighboring division.

- *Each line flashes as sound passes it.* The line brightens and a short bar appears on it as the audio reaches that tap, so you see each echo as you hear it. The bar's height follows that tap's output level.
- *The view zooms to fit.* The longest audible echo sits near the left edge, and the view glides smoothly to a new zoom when you change Time or Interval.
- *Changing Quality or switching between mono and stereo input clears the scope.* It refills as Retours records.

**Slice.** With Slice on, the tape stops and the display switches to the slice view: each looping play head draws as a line over the slice it plays, with a cursor running along it. Turn Slice off and the display goes back to the scrolling scope.

In VCV, the key under the display changes to match the module Lucarne is attached to: Scroll shows that the newest audio is at the right, Marker shows one line per tap, Color shows the tap ratio, and Brightness shows the tap's output. On MetaModule there is no key.

---

## Setup

- **VCV Rack:** place Lucarne directly to the right of Particules or Retours. Until it is, the display reads "Place right of Particules or Retours".
- **MetaModule:** add Lucarne to the patch, then attach it from the **Expanders…** menu of Particules or Retours (this needs firmware 2.4 or later). Tap the display to see it fullscreen. Until it is attached, the display reads "Add from the Expanders menu of Particules or Retours".

---

## Notes

- Lucarne works with Particules and Retours in Beads mode only. In classic mode it shows "Particules is set to Classic mode. Lucarne only works in Beads mode." or "Retours is set to Classic mode. Lucarne only works in Beads mode."
- It is display-only: it doesn't change the sound of Particules or Retours in any way.
