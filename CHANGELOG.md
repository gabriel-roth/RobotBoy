# Changelog

## [Unreleased]

- Display improvements for Lucarne with Retours.
- Lucarne with Particules: grains disappear as soon as they stop sounding, instead of fading out, so dense clouds are easier to read.

## [2.3.0] — 2026-10-15 (MetaModule)

- Particules and Retours use much less CPU in Cold digital and Scorched cassette, and on VCV in every Quality setting when Rack runs at a sample rate other than 48 kHz.
- Changing Quality on Particules or Retours, or plugging or unplugging an audio input, no longer causes a CPU spike that could make MetaModule drop audio.
- Ondes' 2x and 4x anti-aliasing uses less CPU.
- MetaModule: Particules and Retours list their controls in panel order (firmware 2.4 or later). Before, Retours' Filter and Particules' Seed came after the CV amounts.
- MetaModule: Lucarne's "Add from the Expanders menu" message now fits on its screen.
- Retours' Lucarne view now holds still while Time or Interval is modulated, showing the whole buffer, instead of zooming with every change.
- Lucarne now works on either side of Particules or Retours, not just the right. Between two of them, it shows the one on its left.
- Bypassing Lucarne now turns it off: its screen goes blank, and Particules or Retours stops sending it data.
- Lucarne's key for Retours is redrawn: it labels the primary and secondary tap colors and shows how a tap's marker flashes with its level.
- Lucarne with a clocked Retours: the echoes of the louder division are always white and the other division's green. Before, a division could be white or green depending on which step of Interval it was on.

## [2.2.0] — 2026-10-14

This release collects the 2.2.0 betas, without their Classic mode.

- Particules, Retours and Ondes now run on the real Mutable Instruments Beads code, which Émilie Gillet released as open source in October 2026, instead of a reconstruction. They should sound and respond like Beads in its granular mode, delay mode and wavetable-oscillator mode. Panels and patches carry over, but patches made with 2.1 or earlier may sound different. For the old engines, install the 2.2.0 beta 4 pre-release from GitHub, which can switch back to them with a Classic mode menu option. Other changes that come with the new engines:
  - Quality settings now match Beads: Cold digital runs at 32 kHz, and buffers are 4, 8, 10.7 and 16 seconds (double for a mono input). Sunny tape no longer has wow and flutter. Scorched cassette's tape hiss can now be switched off from the context menu.
  - Auto gain works as on Beads: it boosts quiet sources by up to 32 dB but never turns hot ones down.
  - Particules: a new Seed button, as on Beads. One press unlocks it, so grains play only while it (or the Seed input) is held high; a 4-second hold locks it again. It replaces the "Seed CV mode" menu (Triggers = locked, Gates = unlocked); saved patches keep their setting.
  - Particules: the Pitch knob has Beads' notches (unison, 1, 3, 4 and 7 semitones, octaves). Size reaches 4-second grains. Grain scheduling and voice stealing are Beads'. A new **Gate** output sends a gate per grain, replacing the "Grain trigger on R output" menu option.
  - Retours: the panel is now 15 HP, laid out like Particules, with two new controls from Beads' delay mode: **Filter** (a low-pass/high-pass tone control on the repeats) and **Reverb**, each with a CV input and trimpot. A new **Gate** output sends a gate on every repeat. Existing patches load with Filter off and Reverb at zero.
  - Retours: tap tempo works as on Beads (the first press switches to tempo mode, then taps set the tempo). The "Time change response" menu is gone. Feedback reaches unity at about 82% (the panel arrow has moved there). New input-gain menu items, as on Particules.
  - Ondes plays Beads' 12 wavetable banks of 16 waves (previously 24 banks of 8 derived from Plaits), switching banks with a short crossfade. The "Waveform banks" menu is gone.
- New: **Lucarne**, a display expander for Particules and Retours. Place it to the right of Particules to watch the recording buffer and every grain as it plays: where it reads, how long it is, its pitch, its stereo position, and its playback. Place it to the right of Retours to watch Retours' recent audio scroll past a line for each echo, which flashes as you hear it; with Slice on, it shows the whole buffer with each looping slice shaded. Its panel key changes to match the module it's attached to. On MetaModule, attach it from the module's Expanders menu (firmware 2.4 or later).
- Ondes: a new **Anti-aliasing** context-menu option (Off, 2x or 4x oversampling) removes the harsh aliasing Beads' oscillator has on bright waveforms at higher notes. Off by default, so Ondes still sounds like Beads.
- Ondes: Position now starts fully counter-clockwise, on each bank's first waveform, which has a full fundamental. Existing patches keep their Position.
- Patches saved in Classic mode in the 2.2 betas open with the Beads engines.
- Particules: Quality can now also be set from the context menu, as on Retours.
- Particules and Retours: loading a preset that leaves out a context-menu setting (input gain, Quality, tape hiss and, on Particules, Lock pitch and its root) now resets that setting to its default, instead of keeping the module's current value.
- Particules, Retours and Ondes: clearer tooltips. The small knobs under Time, Size, Shape and Pitch are called attenurandomizers, as in the manuals, and the pitch inputs read "1V/octave pitch".
- All modules: context-menu changes can be undone and redone.

## [2.2.0-beta.4] — 2026-10-13 (pre-release)

- Lucarne now works with Retours too. Place it to the right of Retours (on MetaModule, attach it from Retours' Expanders menu) to watch Retours' recent audio scroll past a line for each echo, which flashes as you hear it. A line at the right edge flashes with the incoming dry sound. The view zooms to fit the longest echo. With Slice on, the waveform freezes and shows the whole buffer, with each looping slice shaded and a line moving through it as it plays.
- Lucarne's VCV panel changes its key to match the module it's attached to. With nothing attached, the panel says where to place it.

## [2.2.0-beta.3] — 2026-10-13 (pre-release)

- New: Lucarne, a display expander for Particules. Place it to the right of Particules (on MetaModule, attach it from Particules' Expanders menu) to watch the buffer and every grain: where it reads, how long it is, its pitch, its stereo position, and its playback. On MetaModule, attaching it needs firmware 2.4 or later.

## [2.2.0-beta.2] — 2026-10-12 (pre-release)

- Classic mode (context menu) for Particules, Retours and Ondes brings back the engines from 2.1 and earlier. Patches made before 2.2 open in classic mode, so they sound as they did. Switching isn't allowed while Freeze or Slice is on, and in Particules and Retours it clears the buffer and mutes the output briefly.
  - Particules' "Grain trigger on R Out" option is gone: patches that used it should patch the Gate output instead. In classic mode the Seed button fires grains even with nothing patched to Seed.
  - In classic mode, Retours' new Filter and Reverb knobs work too. Feedback reaches unity at about 90%, not at the panel arrow.
  - Ondes patches that turned off some waveform bank groups now spread Bank across all 24 banks in classic mode.
  - Classic mode uses more memory per instance on MetaModule.

## [2.2.0-beta.1] — 2026-10-12 (pre-release)

- Particules, Retours and Ondes now run on the real Mutable Instruments Beads code, which Émilie Gillet released as open source in October 2026, instead of a reconstruction. They should sound and respond like Beads in its granular mode, delay mode and wavetable-oscillator mode. Panels and patches carry over, but some behavior changes:
  - Quality settings now match Beads: Cold digital runs at 32 kHz, and buffers are 4, 8, 10.7 and 16 seconds (double for a mono input). Sunny tape no longer has wow and flutter. Scorched cassette's tape hiss can now be switched off from the context menu.
  - Auto gain works as on Beads: it boosts quiet sources by up to 32 dB but never turns hot ones down.
  - Particules: a new Seed button, as on Beads. One press unlocks it, so grains play only while it (or the Seed input) is held high; a 4-second hold locks it again. It replaces the "Seed CV mode" menu (Triggers = locked, Gates = unlocked); saved patches keep their setting.
  - Particules: the Pitch knob has Beads' notches (unison, 1, 3, 4 and 7 semitones, octaves). Size reaches 4-second grains. Grain scheduling and voice stealing are Beads'. A new **Gate** output sends a gate per grain, replacing the "Grain trigger on R output" menu option.
  - Retours: the panel is now 15 HP, laid out like Particules, with two new controls from Beads' delay mode: **Filter** (a low-pass/high-pass tone control on the repeats) and **Reverb**, each with a CV input and trimpot. A new **Gate** output sends a gate on every repeat. Existing patches load with Filter off and Reverb at zero.
  - Retours: tap tempo works as on Beads (the first press switches to tempo mode, then taps set the tempo). The "Time change response" menu is gone. Feedback reaches unity at about 82% (the panel arrow has moved there). New input-gain menu items, as on Particules.
  - Ondes plays Beads' 12 wavetable banks of 16 waves (previously 24 banks of 8 derived from Plaits), switching banks with a short crossfade. The "Waveform banks" menu is gone.

## [2.1.0] — 2026-10-03

- Loooop (MetaModule): the parameter list is now grouped by playhead, with Red, Yellow, Blue, and Purple submenus, followed by the global controls. Requires MetaModule firmware 2.4 or later; on firmware 2.3 the list stays flat.

## [2.0.0] — 2026-09-01

- First public release.
