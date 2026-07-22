# Code review — 2026-07-21

Full doc-vs-implementation audit: every documented feature in README.md, Loooop.md,
Retours.md, Particules.md, Ondes.md, Filters.md, CHANGELOG.md, docs/test-patches.md,
and docs/scorched-cassette-quality-analysis.md was checked against the source, with
git history consulted where doc and code disagreed. Five parallel review passes:
Loooop/Löp, Retours, Particules, Filters+Ondes, cross-cutting metadata.

**Overall verdict:** the DSP implementations are in excellent shape — nearly every
documented control, range, default, mode, and signature behavior is implemented as
described and covered by tests. The real problems are documentation drift (docs
written against older code) and two places where the manual documents behavior the
code has never had. The 2026-07-08 review's "fixed" items are all confirmed fixed;
its remaining-backlog list is still accurate.

---

## High — documented behavior that doesn't exist

### H1. Particules: "Pitch CV always tracks 1 V/octave" is not implemented
Particules.md:57 and :100 claim the Pitch CV input always tracks 1 V/oct with the
attenurandomizer only adding randomization on top. In the code, pitch CV enters
**only** through the attenurandomizer (`src/particules/dsp/src/grain/grain_engine.cpp:150`,
`src/particules/dsp/src/random/attenurandomizer.h:21-45`): at noon (the default)
pitch CV is ignored entirely; clockwise scales it (0.5 → half V/oct); counter-clockwise
replaces tracking with randomization. The doc contradicts itself — line 52 and the
"Playable grains" patch idea (line 140, "set the Pitch attenurandomizer fully
clockwise") describe the actual behavior. The claim was added in doc-only commit
062be33; no code ever implemented it. Tests assert the code's behavior
(`test_attenurandomizer.cpp:11`).
**Decide:** either the doc lines are wrong (delete the "always V/oct" claims), or the
intent is real and pitch CV needs an unconditional V/oct path plus AR on top.

### H2. Loooop: documented "separate processing" patch idea produces silence
Loooop.md:117 says to take each head's own Out L/R into different effects and turn
Level fully CCW to remove the head from the Mix. But Level is applied inside the
engine before per-head outputs exist (`src/loooop/dsp/LoopEngine.cpp:546-547`;
head outs fed directly from that in `src/loooop/Loooop.cpp:204-205` and
`metamodule/loooop/LoooopCore.cc:86-89`), so Level CCW silences the head outs too.
Loooop.md:48's "Level: this head's volume in the mix outputs" is wrong the same way —
only Pan is mix-only. `test_loop_engine.cpp` `test_per_head_outs` (line 420) asserts
head outs ARE level-scaled, i.e. tests agree with code.
**Decide:** either make Level mix-only like Pan (behavior change, arguably what the
doc intends), or fix Loooop.md:48/:117 (e.g. "use Dry/Wet or unplug Mix instead").

### H3. Loooop.md MetaModule note describes a UI that no longer exists
Loooop.md:69 says on MetaModule, Overdub and Grid live in the options list and
Overdub is split into On/Off + Write mode. Since 42fd0bd (2026-07-12) both are panel
controls: Overdub is a 5-position FlipSwitch (Layer/Decay/Add/Replace/Lock,
`metamodule/loooop/Loooop_info.hh:39-53`, `QlpElements.hh:41-51`) and Grid is a
panel snap knob. There is no On/Off + Write-mode split. The note was true when
added (2b46ade, 2026-07-10) and later doc refreshes never removed it.

### H4. docs/test-patches.md is badly stale (pre-rename names, removed sliders)
- Lines 169-206, 324, 342-345: "Yellowjacket" and Character modes "Tame/Screaming"
  — renamed Vespid (2c3c5d8) and British/German (21d09fe) before/at the commit that
  added this file.
- Lines 281-310: Retours patches use "Density", "Seed button/input", "Freeze" —
  shipping names are Interval, Tap tempo/Clock, Slice (`src/retours_delay/Retours.cpp:116,127,136`).
- Lines 137-144, 336-341: Onbetap "tuning sliders" (Drive span, Core headroom,
  Self-osc onset trim, Output trim) no longer exist — onset trim is baked
  (`src/Onbetap.cpp:32`); menu now has only Character / Resonance limiting /
  Oversampling. The checklist's "Resonance limiting (Hard)" default is also stale —
  Soft is shipped default (`src/Onbetap.cpp:74`).
- Lines 290, 348: Retours "Random LFO rate" slider removed (0ab139c baked 0.1 Hz;
  only Input trim + Doppler slew sliders remain).
- Lines 204, 344-345: Vespid "Inverter bandwidth" slider removed (baked 60/50 kHz
  per mode, `src/Vespid.cpp:88-89`); the "Accuracy default" open question is
  resolved (High is default, `src/Vespid.cpp:558`).

### H5. README.md:51 — broken screenshot link
`screenshots/MF-20.png` doesn't exist; the file is `screenshots/MF20Filter.png`.
All eight other README images resolve.

---

## Medium

### Uncommitted working-tree change (saturation.h/.cpp)
The pending edit's new header comment (`src/particules/dsp/src/fx/saturation.h:14,17-18`)
references a `SaturateWrite` method that exists nowhere in the repo, and says
Scorched uses "soft tanh limiting" when `LimitFeedback(kScorchedCassette)` is a
plain `HardClip(±1)` (`saturation.cpp:71-74`). Looks like mid-edit WIP describing
planned work. Related: `Saturation::Process()` (`saturation.cpp:23-42`) is dead
code — the only callers repo-wide are `LimitFeedback` — so the whole
"quality-mode saturation curves" comment block describes curves that never run.
Fix the comment (and consider deleting `Process()`) before committing.

### M1. Retours: clocked-tempo abandon only works with the Clock jack unpatched
Retours.md:108 says a tapped or clocked tempo is abandoned if the clock stops for a
few seconds or Interval is moved far enough. Both exits are gated on
`!clock_connected` (`src/retours_delay/dsp/src/time/base_time.cpp:105-114`): a
patched-but-silent clock keeps the last tempo forever and Interval can't break out.
A `Retours.cpp:313` comment suggests patched-means-clocked is deliberate — if so
the doc sentence is overbroad; if not, the gate is a bug. No test covers a
patched-but-stopped clock.

### M2. "Quality can't be changed while Slice/Freeze is engaged" — only the desktop button
Retours.md:67 and Particules.md:81. Only the desktop momentary button refuses to
cycle while frozen (`Retours.cpp:333`, `Particules.cpp:413-417`). The VCV context
menu and the MetaModule 4-position switch both change the selection while frozen;
the DSP defers the reformat until unfreeze (`retours_processor.cpp:164-173`,
`particules_processor.cpp:165-168` — the deferral is designed and tested). The
buffer is protected either way; the doc's blanket claim isn't accurate for
menu/MM paths.

### M3. Feedback limiter docs oversell Scorched (Retours.md:57, :43; Particules.md:61)
"From a clean brickwall to grungy tape saturation" — Bright and Scorched both use
`HardClip(±1)` (`saturation.cpp:57-77`); only Cold (soft clip) and Sunny
(asymmetric) differ. Scorched's grunge comes from the µ-law codec + coloring, not
the limiter. Deliberate (feedback-runaway fix, e06d2c0) but the doc sentence
misleads.

### M4. Particules auto-gain range: doc says "0 to +32 dB", code goes to −60 dB
Particules.md:125. Calibration computes `Clamp(-input_db - 8, -60, +32)`
(`src/particules/dsp/src/input/auto_gain.cpp:109`, `auto_gain.h:27`) — a hot
±5 V input locks ≈ −8 dB. The manual-gain UI is 0–32 dB and matches the doc.

### M5. Particules reverb is applied to the dry+wet mix, not "on the wet signal"
Particules.md:65 vs `particules_processor.cpp:314-324` — reverb input is the
post-crossfade mix, so fully-dry still reverberates. Matches hardware Beads
topology; Particules.md:13 describes it correctly ("at the end of the chain").

### M6. Loooop recording display doesn't "fill in left-to-right"
Loooop.md:24. The renderer stretches the recorded-so-far audio across the full
width and compresses as it grows (`src/loooop/display/LoopWaveformRenderer.cpp:38-41`,
with a code comment calling this the spec). The doc sentence has never matched the
vendored renderer.

### M7. Onbetap self-oscillation onset understated
Filters.md:90 says onset "around three-quarters of the way up." After the baked
onset trim (025910c, worklog lines 478-485: "onset res ≈0.72 → ≈0.84, abandoning the
hardware-matched ≈2/3-of-pot onset"), onset is ≈0.84 at default cutoff, ≈0.78 at
8 kHz, ≈0.63 at 20 kHz. Filters.md was consolidated after the bake and still says
three-quarters.

### M8. CHANGELOG.md gaps
- No entry for Ondes at all (added 818eae8, after the 2.0.1 release; registered in
  all manifests).
- Lines 12-13: Retours entry uses "DENSITY" and "FREEZE" — shipping names are
  Interval and Slice.

### M9. tests/README.md stale
run.sh actually builds 5 dirs (mf20, loooop, particules, onbetap, vespid —
`tests/run.sh:9`), there are three test lanes (retours_delay_dsp is missing), the
ported-tests table omits all post-port additions (now 16 test files), and the
python-guard list omits `tests/test_head_colors.py`.

---

## Low / nits

- **Retours** — stale `// 0.02..2` range comment on `random_lfo_hz`
  (`dsp/include/retours_delay_dsp/types.h:57`, slider-era leftover); envelope
  period and Clock light track the base Interval, not Interval × Time
  (deliberate — `retours_processor.cpp:207` — but Retours.md:41/:81 say "delay
  time/period", only true at Time = 1×); stale "SEED" naming in comments
  (`retours_block_runtime.h:57-58`, `types.h:46` — Retours has no Seed); the
  Feedback 90%-unity panel arrow (5e0858b) is implemented but never documented in
  Retours.md; buffer-length table implies absolute seconds but the frame budget is
  fixed (halves at 96 kHz engine rate); Sunny doc says "gentle wow" but code does
  half-depth wow **and flutter**; `tests/retours/mm-sim-notes.md:89` calls param 1
  `DENSITY_PARAM` (now `INTERVAL_PARAM`).
- **Particules** — analysis doc's Findings 1-2 file:line refs and LP figures are
  stale, and its Resolution section never states Finding 2 (darkness) was fixed
  (÷2 rate + 10 kHz input LP, e06d2c0/7ac4c99) — a reader can conclude Scorched
  still has a 2.5 kHz input LP; stale comment `Particules.cpp:264` ("4 s buffer,
  feedback path, reverb tail" — ClearBuffer clears only the recording buffer);
  grain trigger on R emits at most one pulse per 64-sample block, so grains born
  in the same ≈1.3 ms window share a pulse; Particules.md:52 calls the CW behavior
  an "attenuverter" — it's a unipolar attenuator (inversion impossible); doc never
  mentions that a Quality change fades out / clears / fades back in like an IN R
  change; stale test comment "8x decimation" (`test_processor.cpp:229`, Sunny is
  now ÷2).
- **Loooop/Löp** — Trig restart is direction-aware (reverse heads restart at the
  playback-order start, `LoopEngine.cpp:205-213`) but the doc says "beginning of
  its window" unconditionally; "Löp works exactly like one head of Loooop"
  oversells (no Pan/Level, no Exclude-from-Grid — an earlier doc revision spelled
  this out); panel silk says "Dub Mode", doc says "Overdub"; stale comment "Add
  must stay index 0" (`LoopEngine.hpp:40-43`) contradicted by
  `LooperModuleDSP.hpp:70-72` and 75d50fd (index 0 = Layer everywhere now); both
  panel yamls declare RECORD_PARAM widget `VCVButton` vs the .cpp's
  `VCVLightButton` (cosmetic placeholder).
- **Filters** — MF-20 "at 1× the filter is clean" isn't strictly true in Korg35
  mode (a full-scale ±5 V signal already enters the negative knee at drive 1,
  `MF20Filter.hpp:184-202`; OTA is genuinely clean); Filters.md:59 omits the K35
  resonance-loop clip, which the header calls essential; `OnbetapFilter.hpp:28-31`
  claims Soft limiting gives "sine-ish" self-osc — measurements (worklog test 2d)
  show both modes square-ish and nearly identical, and Filters.md:103 has it
  right; `WaspFilter.hpp:27-28` claims constants match fitted_constants.md
  "exactly" — British makeup is now 2.5 (a93a9a9) vs ref's 1.0 (golden tests
  unaffected, they measure pre-makeup states); fitted_constants.md says fPole
  default 80 kHz vs baked 50/60 kHz (historical, but the header calls the doc
  "authoritative"); Filters.md:161 "modes differ in loudness" is post-a93a9a9
  stale at drive 0 (level-matched; British ≈4.6 dB quieter only at full drive);
  `Onbetap.cpp:23` comment says gain tops at 16× — actually 15.77×.
- **Ondes** — Ondes.md:23-25 bank-family lists omit bank 2 (Quadra), bank 9
  (Sawtri), and banks 22-23 (Braids extra A/B).
- **Cross-cutting** — `src/plugin.cpp:31` comment says "six modules" (eight);
  `metamodule/register.cc:4` says "four modules" (eight); empty untracked
  `presets/` dir has reappeared after round 4 deleted it; stale untracked
  `vcv/src/Yellowjacket.cpp.o/.d`; no local/remote v2.0.1 git tag found (could be
  sandbox/network — unverified); README has no build instructions anywhere
  (workflow lives outside the repo).

---

## 2026-07-08 review status

Confirmed **fixed**: MF-20 cutoff floor, Particules queued-clear drain, grain
steal-and-replace, quality-crossfade dedup, RecordingBuffer read dedup,
control_conditioner.h relocation, density_cv_volts rename, MM set_samplerate loop
preservation, all in-flight branches merged, screenshots retaken. The "4096 peak
bins blocky" note was superseded rather than fixed (f19c7b4 retired peak bins for
raw-sample waveform).

Still **open** (matches that review's own backlog list): per-sample
`bumpWaveformRevision()` in the record path (`LoopEngine.cpp:473,486,500`), linear
(not constant-power) pan (`LooperModuleDSP.hpp:53-59`), no MF-20 HQ oversampling,
no Loooop anti-aliasing above 1× speed, dead `#include <cmath>` in
`recording_buffer.cpp:5`, NextGaussian non-unit variance nit.

---

## What checked out

- **Manifests/registration:** all 8 modules identical across plugin.json,
  vcv/plugin.json, metamodule/plugin-mm.json, src/plugin.cpp (both build paths),
  metamodule/CMakeLists.txt; enforced by `tests/test_robotboy_identity.py`. No
  load-bearing Yellowjacket/Échos leftovers. MM assets complete, Loooop/Löp PNGs
  correctly in `assets/Loooop/`.
- **Panel specs:** all seven module yamls match their .cpp param/input/output
  lists and mm coordinates exactly (spot-checked pairwise).
- **Retours:** every documented feature verified — buffer table incl. mono
  doubling and Cold's half-pool cap, codecs, fade-clear-fade, tap tempo,
  clocked subdivisions with hysteresis, pitch notches and 1 V/oct at full AR,
  Shape morph, Slice engine, attenurandomizers, tape/crossfade time modes,
  0.3 s slew + 0.1 Hz LFO constants.
- **Particules:** quality table exact, int12/µ-law codecs, Q32.32 grains,
  fade-clear-fade with freeze-abort re-arm, clocked Density (noon dead zone,
  1/16→1/1 divisions), C3 grain-rate cap with CV clamp, Seed gate modes, Freeze
  seam fades, Size/Shape/Pitch behavior, menus and undo scope, LEDs. Test suite
  passes with the uncommitted change in place.
- **Loooop/Löp:** all engine behaviors (speed/size/position/jitter, grid snap,
  five overdub modes with documented decay characters, one-shots, crossfade
  seams, V/oct speed mode, Jump/Trig) verified; 74-test engine suite covers
  nearly everything documented; gap is host-glue (Module::process wiring) and MM
  cores, consistent with the project's user-checklist convention.
- **Filters/Ondes:** all ranges, defaults, modes, normalling, polyphony,
  oversampling ladders, self-osc characters (German yes / British no), drive
  staging, Vintage drift model, wavetable interpolation, and pitch-notch sharing
  verified against code and tests.
