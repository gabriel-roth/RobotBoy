# Changelog

## [Unreleased]

### Fixed

- **Loooop & Löp — param IDs now match between VCV and MetaModule.** The VCV
  `ParamId` order was reordered to be identical to the MetaModule `Elem` /
  `Elements` order, so a patch authored on one host maps its knob values onto
  the correct controls on the other. Previously the two orders diverged (VCV
  kept Crossfade and the per-head Trigger/Speed-V-Oct/Grid-exclude menu params
  interleaved per head; MetaModule grouped them into a trailing "Options"
  block), so a VCV patch loaded on MetaModule scrambled every knob from the
  Crossfade slot onward. **Patches saved with an earlier VCV build will load
  with shifted param values** (same class of break as the earlier globals-first
  reorder); MetaModule-native patches are unaffected.

## [2.0.1] — 2026-07-11

Initial release.

Robot Boy is a plugin for [VCV Rack](https://vcvrack.com) and the
[4ms MetaModule](https://4mscompany.com/metamodule), collecting four modules:

- **Loooop** — stereo RAM looper with four independent playheads
- **Löp** — single-playhead version of Loooop
- **MF-20** — Korg MS-20-style filter (OTA and Korg35 revisions)
- **Particules** — granular texture processor

[2.0.1]: https://github.com/gabriel-roth/RobotBoy/releases/tag/v2.0.1
