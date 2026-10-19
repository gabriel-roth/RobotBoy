# Changelog

What's new in Robot Boy, newest first. For how anything works, see the module manuals linked from the [README](README.md).

## [Unreleased]

### Lucarne color schemes

Lucarne now comes in seven color schemes: **Lucarne**, **Darkroom**, **Paper**, **Cyanotype**, **Thermal**, **Bakelite** and **Anaglyph**. Pick one from the right-click menu and it's saved with your patch.

Particules and Retours each get their own scheme, so a Lucarne you move from one to the other keeps a look for each. On VCV, **Make default** sets the look new Lucarnes start with, and the key under the display changes to match.

### Also

- Lucarne with Particules: grains now vanish the moment they stop sounding instead of fading out slowly, so dense clouds are much easier to read.
- Lucarne with Retours: assorted display improvements.
- Particules and Retours use less CPU while Reverb is turned all the way down. The sound doesn't change: the reverb only rests once its tail has completely died away.

### Small print

- With nothing attached, Lucarne's menu offers both schemes.
- MetaModule has no Make default and no key.
- The reverb rests only with Reverb at exactly 0, starting a second or so after its tail has gone silent. CV or an offset that keeps Reverb even slightly above 0 keeps it running.

## [2.3.0] — 2026-10-15 (MetaModule)

This release came out for MetaModule only. Mostly it's about speed: Particules, Retours and Ondes leave your MetaModule a lot more headroom.

### Faster

- Particules and Retours use much less CPU in the Cold digital and Scorched cassette Quality settings. On VCV they're also faster in every Quality setting when Rack isn't running at 48 kHz.
- Changing Quality, or plugging or unplugging an audio input, no longer causes a CPU spike that could make MetaModule drop audio.
- Ondes' anti-aliasing uses less CPU.

### Lucarne

- Lucarne works on either side of Particules or Retours now, not just the right. If it's between two of them, it shows the one on its left.
- With Retours, the view holds steady while you modulate Time or Interval instead of zooming in and out with every change.
- With a clocked Retours, each division keeps its color: the louder one's echoes are always white and the other's are always green. Before, they could swap colors partway through.
- The Retours key has been redrawn to explain what the colors and flashes mean.
- Bypassing Lucarne turns it all the way off.

### MetaModule tidying

- Particules and Retours list their controls in panel order (firmware 2.4 or later), so Filter and Seed are no longer stuck at the bottom of the list.
- Lucarne's "Add from the Expanders menu" message fits on its screen now.

## [2.2.1] — 2026-10-18

Corrected the tags in the VCV Library listing, so Loooop and Löp are filed under Recording and Ondes under Oscillator.

## [2.2.0] — 2026-10-14

### Beads source code

In October 2026, Émilie Gillet released the source code for Mutable Instruments Beads. Particules, Retours and Ondes now run on that code, replacing the reconstruction they used before. They should sound and respond the way Beads does in its granular, delay and wavetable-oscillator modes.

Your patches still load, but anything made with 2.1 or earlier may sound a bit different. If you need the old sound, [Robot Boy 2.2.0 Classic](https://github.com/gabriel-roth/RobotBoy/releases/tag/classic-2.2.0) has a **Classic mode** switch that brings back the old engines.

Along with the new engines come some new controls:

- **Particules** gets Beads' **Seed** button (press once to play grains only while it's held; hold it for four seconds to go back) and a **Gate** output that fires on every grain. The Pitch knob now has notches at the useful intervals, and grains can be up to four seconds long.
- **Retours** grows to 15 HP and gets Beads' **Filter** and **Reverb** controls, each with its own CV input, plus a **Gate** output that fires on every repeat. Tap tempo now works the way it does on Beads.
- **Ondes** now plays Beads' own 12 wavetable banks of 16 waveforms.
- The **Quality** settings now match Beads, buffer lengths included. Scorched cassette's hiss can be switched off, and Auto gain boosts quiet sources without squashing loud ones.

### New: Lucarne

**Lucarne** is a new display expander that shows what's going on inside Particules and Retours. Put it next to Particules to see the buffer and every grain as it plays: where it reads from, how long it is, its pitch and where it sits in the stereo field. Put it next to Retours to watch your audio scroll by, with a line for each echo that flashes as you hear it. On MetaModule, add it from the module's Expanders menu (firmware 2.4 or later).

### And also

- **Ondes** has a new **Anti-aliasing** option that removes the harsh aliasing Beads' oscillator makes on bright waveforms at high notes. It's off by default, so out of the box Ondes sounds just like Beads.
- Undo and redo now work for context-menu changes in every module.
- Particules has a Quality setting in its context menu, like Retours.
- The tooltips are clearer, and the small knobs under the main controls are now called attenurandomizers, as in the manuals.

### Small print

- Buffers: 4, 8, 10.7 and 16 seconds, doubled for mono input. Cold digital runs at 32 kHz.
- Auto gain: up to +32 dB, never cuts.
- Retours: feedback reaches unity at about 82%, where the panel arrow now points.
- Removed menus: Particules' "Seed CV mode" and "Grain trigger on R output", Retours' "Time change response", Ondes' "Waveform banks". Saved Seed settings carry over.
- Ondes: Position now starts fully counter-clockwise. Existing patches keep theirs.
- Presets that leave out a menu setting now reset it to the default.
- Patches saved in Classic mode open with the new engines.

## [2.1.0] — 2026-10-03

- On MetaModule, Loooop's long parameter list is now grouped by playhead (Red, Yellow, Blue, Purple), so finding a control no longer means scrolling through the whole list. Requires firmware 2.4 or later.

## [2.0.0] — 2026-09-01

Robot Boy's first public release.
