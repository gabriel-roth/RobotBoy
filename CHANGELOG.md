# Changelog

## [Unreleased]

- Lucarne now works with Retours too: it shows Retours' buffer, each delay tap as a line from where it reads to the write head, and the looping slices under Slice. The key under the VCV display changes to match the module it's attached to.

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
