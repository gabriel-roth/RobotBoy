# Changelog

## [Unreleased]

- Particules, Retours and Ondes now run on the real Beads code, which Émilie Gillet released as open source in October 2026, instead of a reconstruction. They should sound and respond like Beads in its granular mode, delay mode and wavetable-oscillator mode. Panels and patches carry over, but some behavior changes:
  - Quality settings now match Beads: Cold digital runs at 32 kHz, and buffers are 4, 8, 10.7 and 16 seconds (double for a mono input). Scorched cassette no longer adds tape hiss, and Sunny tape no longer has wow and flutter.
  - Auto gain works as on Beads: it boosts quiet sources by up to 32 dB but never turns hot ones down.
  - Particules: the Pitch knob has Beads' notches (unison, 1, 3, 4 and 7 semitones, octaves). Size reaches 4-second grains. Grain scheduling and voice stealing are Beads'. The grain-trigger output sends a gate per grain.
  - Retours: tap tempo works as on Beads (the first press switches to tempo mode, then taps set the tempo). The "Time change response" menu is gone. Feedback reaches unity at about 82% (the panel arrow has moved there). New input-gain menu items, as on Particules.
  - Ondes plays Beads' 12 wavetable banks of 16 waves (previously 24 banks of 8 derived from Plaits), switching banks with a short crossfade. The "Waveform banks" menu is gone.

## [2.1.0] — 2026-10-03

- Loooop (MetaModule): the parameter list is now grouped by playhead, with Red, Yellow, Blue, and Purple submenus, followed by the global controls. Requires MetaModule firmware 2.4 or later; on firmware 2.3 the list stays flat.

## [2.0.0] — 2026-09-01

- First public release.
