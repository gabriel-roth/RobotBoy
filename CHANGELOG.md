# Changelog

What's new in Robot Boy, newest first. For how anything works, see the module manuals linked from the [README](README.md).

## [Unreleased]

### Lucarne color schemes

Lucarne now comes in seven color schemes. Pick one from the options menu and it's saved with your patch.

Lucarne saves different color schemes for Particules and Retours. On VCV you can set the current scheme as the default. 

### Also

- Lucarne with Particules: grains now vanish the moment they stop sounding instead of fading out slowly, which is more accurate and easier to read.
- Lucarne with Retours: assorted display improvements.
- Lucarne on MetaModule: colors look brighter and more vivid.
- Particules and Retours use less CPU when Reverb is at 0. 

## [2.3.0] — 2026-10-15 (MetaModule)

This release is for MetaModule only while we wait for the VCV library release. VCV will get these features in 2.4.0.

- Particules and Retours use much less CPU in the Cold digital and Scorched cassette Quality settings on MetaModule. On VCV they're also faster in every Quality setting when Rack isn't running at 48 kHz.
- Changing Quality, or plugging or unplugging an audio input, no longer causes a CPU spike that could make MetaModule drop audio.
- Ondes's anti-aliasing uses less CPU.
- Particules and Retours now list their controls in panel order on MetaModule (firmware 2.4 or later).

### Lucarne

- Lucarne works on either side of Particules or Retours now.
- Less zooming in and out with Retours.
- Fixed an issue where echo markers would switch colors with Retours when clocked.
- Revised the Retours panel key.
- Bypassing Lucarne turns it all the way off.
- The "Add from the Expanders menu" message now fits on the display on MetaModule.

## [2.2.1] — 2026-10-18

Corrected the tags in the VCV Library listing, so Loooop and Löp are filed under `Recording` and Ondes under `Oscillator`.

## [2.2.0] — 2026-10-14

### Beads source code

*Particules, Retours, and Ondes now run the authentic Beads code!*

When these modules were designed, that code was closed-source, so the modules reconstructed Beads's functionality using original code. 

In October 2026, Beads creator Émilie Gillet released the [source code](https://github.com/pichenettes/eurorack/tree/master/beads). Our modules should now sound and respond exactly as Beads does.

Your patches still load, but anything made with 2.1 or earlier will sound and play differently. If you need the old sound, [Robot Boy 2.2.0 Classic](https://github.com/gabriel-roth/RobotBoy/releases/tag/classic-2.2.0) has a **Classic mode** switch that preserves the old engines.

### New features: 
- **Particules** gets Beads' **Seed** button: press once to play grains only while it's held; hold it for four seconds to go back.
- **Particules** and **Retours** each now have a designated **Gate** output — you no longer have to give up an audio channel if you want a trigger for each grain or repeat. 
- **Retours** gets Beads' **Filter** and **Reverb** controls, each with its own CV input.

### New module: Lucarne

**Lucarne** is a display expander that shows what's going on inside Particules and Retours. Put it next to Particules to see the buffer and every grain as it plays. Put it next to Retours to watch your audio scroll by, with a flashing marker for each echo. On MetaModule, add it from the module's Expanders menu (firmware 2.4 or later).

### Also

- **Ondes** has a new **Anti-aliasing** option. It's off by default, so out of the box Ondes sounds just like Beads.
- Undo and redo now work for context-menu changes in every module.
- Particules has a Quality setting in its context menu, like Retours.
- The tooltips are clearer, and the small knobs under the main controls are now called attenurandomizers, as in the manuals.

## [2.1.0] — 2026-10-03

- On MetaModule, Loooop's long parameter list is now grouped by playhead (firmware 2.4 or later).

## [2.0.0] — 2026-09-01

First public release.
