# Lucarne

**Lucarne** is a display expander for [Particules](Particules.md) and [Retours](Retours.md), for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It shows what the module is doing with its buffer: the recorded audio, and either every grain as it plays with the write head (Particules) or a scrolling scope of every delay tap (Retours). It has no controls and no jacks.

---

## What it shows with Particules

**The buffer.** A waveform band across the middle of the display shows Particules' buffer as a ring. The write head is a bright vertical line sweeping left to right, overwriting the picture as it goes, just as it overwrites the audio. When Freeze is on, the line dims and stops.

**The grains.** Each grain is a line above or below the band, covering the stretch of buffer that grain plays. A grain pitched up plays through the buffer faster, so its line is longer.

- *Height shows pitch.* Unison sits just above the band; higher pitches sit further up, and lower pitches go below the band. The display covers ±2 octaves; anything beyond that pins to the edge.
- *A brighter cursor* moves along the line as the grain plays.
- *Color shows stereo position.* Each grain is colored by where Particules pans it. In the default color scheme that is blue for hard left, warm white for center, orange for hard right. Beads picks a random pan for each grain, within a range that widens as more grains overlap and as you add randomness (the attenurandomizers, or Density clockwise of 12 o'clock). With a steady pulse of grains and no randomness, every grain is centered and the display is all white; as you add randomness, the colors fan out.
- *Brightness and fade.* Lines brighten with the grain's envelope. After a grain ends, its line fades out over a moment. Very short grains still flash briefly.
- *Direction.* Reversed grains (Size turned counter-clockwise of about 10 o'clock) run right to left.

In VCV, a key under the display sums this up. Until Lucarne is attached to Particules or Retours, the key area shows where to place it instead, and the screen stays blank. On MetaModule the display is smaller (16:9, to fill the screen when you tap it) and the faceplate has no key.

Changing Quality, switching between mono and stereo input, or clearing the buffer clears the display, which then fills in again as Particules records.

---

## What it shows with Retours

**The scope.** While Retours is running, Lucarne shows Retours' recent audio as a waveform scrolling from right to left. The waveform fills about 70% of the display height.

**The dry playhead.** A near-white line just in from the right edge marks "now": the audio coming into Retours, which you hear as the dry sound. New audio appears there and scrolls away to the left. Like the tap lines, it brightens and shows a bar with the level of the incoming sound. It shows the input, so it still flashes with Mix turned fully wet.

**The delay taps.** Each audible tap is a full-height vertical line at its delay: the farther left the line, the longer the echo. Taps of the main ratio are amber; taps of the second ratio are cyan. Unclocked, the second ratio is the 0.77 taps that fade in as Interval passes noon. Clocked, Interval blends between two neighboring divisions, and the louder one is amber: as you turn Interval from one division to the next, the colors swap partway through the blend.

- *Each line flashes as sound passes it.* The line brightens and a short bar appears on it as the audio reaches that tap, so you see each echo as you hear it. The bar's height follows that tap's output level.
- *The view zooms to fit.* The longest echo sits near the left edge, and the view glides to a new zoom when you change Time or Interval. While the delay is being modulated (the Time attenurandomizer is turned away from noon, or a cable is in Interval CV) it shows the whole buffer instead, so the waveform holds still.
- *Changing Quality or switching between mono and stereo input clears the scope.* It refills as Retours records.

**Slice.** With Slice on, the tape stops and the waveform freezes. Each looping play head shows as a line (amber or cyan, by ratio) moving through a faint shaded band over the slice it plays, and it flashes with what it plays, just like a tap line. The view shows the whole buffer and holds it, unchanged, until Slice turns off. Turn Slice off and the waveform starts scrolling again.

In VCV, the key under the display changes to match the module Lucarne is attached to. With Retours, it labels the primary (amber) and secondary (cyan) tap markers and the play head, and shows how each marker flashes with the level of what it plays. On MetaModule there is no key.

---

## Color schemes

Right-click Lucarne and choose **Color scheme** to change how the display looks. Each Lucarne keeps its own scheme, saved with the patch. Changing it can be undone, and Initialize keeps it. The colors described above are the default scheme, Lucarne; the others recolor the same display (and, in VCV, the key under it):

- **Lucarne:** the default look.
- **Darkroom:** dim and warm only, for dark stages.
- **Paper:** a light background, for bright rooms.
- **Cyanotype:** white, pink and mint on deep blue.
- **Thermal:** amber and violet on near-black.
- **Bakelite:** warm cream with olive and orchid.
- **Anaglyph:** red on the left, cyan on the right, like 3D glasses.

In VCV, **Make default** in the same submenu makes this Lucarne's scheme the one every newly added Lucarne starts with, in any patch. Patches saved before color schemes existed, or without a scheme, open in the default Lucarne look regardless of your Make default choice. On MetaModule there is no Make default; new Lucarnes start as Lucarne.

---

## Setup

- **VCV Rack:** place Lucarne directly beside Particules or Retours, on either side. Until it is, the panel under the display reads "Place next to Particules or Retours" and the screen stays blank.
- **MetaModule:** add Lucarne to the patch, then attach it on either side from the **Expanders…** menu of Particules or Retours (this needs firmware 2.4 or later). Tap the display to see it fullscreen. Until it is attached, the display reads "Add from the Expanders menu of Particules or Retours".

If Lucarne sits between two of them, it shows the one on its left.

---

## Notes

- It is display-only: it doesn't change the sound of Particules or Retours in any way.
