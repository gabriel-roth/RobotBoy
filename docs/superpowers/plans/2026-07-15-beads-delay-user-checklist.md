# Échos (beads-delay) — user checklist (GUI checks Claude can't run)

Branch `worktree-beads-delay`; build installed to the Rack2 plugins dir on
2026-07-15 (plugin.dylib + plugin.json + res/). `.mmplugin` at
`metamodule/metamodule-plugins/RobotBoy.mmplugin`.

## VCV Rack — panel

- [ ] Échos panel renders correctly (12 HP, Beads-family look, dark screws).
- [ ] Title "ÉCHOS": the É glyph is substituted from DejaVu Sans because
      Shuttleblock has no É (Task 11 note). Confirm it doesn't look out of
      place next to the other letters.
- [ ] DENSITY CV jack row asymmetry (flagged in Task 11): the CV jack rows
      don't align perfectly under the knob columns. Acceptable, or should
      the layout get a pass before release?
- [ ] Panel layout in general is a "don't over-invest" first cut per the
      spec — revise freely.

## VCV Rack — tooltips and params

- [ ] PITCH knob tooltip shows semitones and lingers at the virtual notches
      (0, ±7, ±12, ±19 st get 3× the knob travel; same
      pitch_notch_map/PitchParamQuantity as Particules/Ondes). Noon shows
      0 st (shifter bypassed).
- [ ] QUALITY button tooltip names the mode (Bright digital / Cold digital /
      Sunny tape / Scorched cassette) and clicking cycles the LED color
      (same encoding as Particules). Cycling is blocked while frozen —
      intended (quality change clears the buffer, which would kill the
      frozen slice).
- [ ] Ctrl+R (randomize) never latches FREEZE (randomizeEnabled=false).
- [ ] SEED button light blinks on the base-time grid; tapping it sets the
      tempo; a clock cable into SEED takes over.

## VCV Rack — context menu

- [ ] Menu shows: Quality submenu, Time change response (Tape (doppler) /
      Crossfade), Envelope feedback tap (Post-envelope / Pre-envelope),
      three sliders (Input trim dB, Doppler slew s, Random LFO rate Hz),
      Clear buffer.
- [ ] Each menu change survives save → reload of the patch (all six fields
      are in dataToJson/dataFromJson).
- [ ] Undo (Cmd+Z) reverts a menu change (each is wrapped in a
      whole-module history snapshot).
- [ ] Clear buffer audibly empties the delay line (takes effect at the next
      64-sample block, drains incrementally — a short fade, not a click).

## MetaModule

- [ ] Load `RobotBoy.mmplugin` in the MM (or GUI simulator): screenshot the
      Échos panel for the repo (screenshots/Echos.png does not exist yet —
      the README section has no image until then).
- [ ] Knob mapping sanity: map DENSITY/TIME/PITCH/FEEDBACK to MM knobs and
      confirm ranges/directions feel right.
- [ ] QUALITY is a real 4-position switch on MM (not the desktop momentary
      button) — confirm the Adjust popup shows the four mode names.
- [ ] MM has **no path to Input trim / Doppler slew / Random LFO rate**
      (desktop-only menu sliders; MM menus can't host sliders). Fine for
      v1, or should these become hidden params/switches? (Ledger T10 —
      your call.)

## Audible check — the three demo patches

Fixtures in `tests/echos/mm_sim/` (`demo_*.yml` + `gen_demo_inputs.py`);
rendered WAVs from the verification sweep are in the session scratchpad
(`echos_demo/`). Settings to reproduce by hand:

- [ ] **Karplus-Strong**: DENSITY fully CCW (audio-rate base, clamps at
      2 ms → 500 Hz), TIME 0, FEEDBACK 0.95, BLEND 50%, feed a short noise
      burst → pitched pluck that rings and decays. (This patch caught a real
      bug in the sweep — see worklog: feedback-HP Q left at the SVF default
      made everything ≥ ~0.87 feedback self-oscillate at ~30 Hz. Fixed +
      regression-tested; worth an ear-check that high feedback now behaves.)
- [ ] **Clocked echo**: clock (0.5 s period) into SEED, DENSITY at noon
      (1/1), TIME 0, FEEDBACK 0.5, BLEND 50%, rhythmic burst in → echoes
      lock to the 0.5 s grid, each repeat ~half the last.
- [ ] **Freeze slicer**: record varied material for a few seconds, latch
      FREEZE, turn TIME → each position loops a different base-time-length
      slice of the buffer (verified: 8 slices at 0.5 s base time, correct
      slice content and clean looping).

## Design decisions to sign off (spec decision log + ledger)

1. [ ] **Module name**: slug `Echos`, display **Échos** — family consistency
       with Particules/Ondes, avoids MI's "Beads" name. Rename is trivial
       until patches exist; decide now.
2. [ ] Dedicated FEEDBACK/BLEND CV jacks instead of hardware's assignable
       CV button.
3. [ ] Always-stereo buffer (no mono time-doubling).
4. [ ] Quality→duration mapping 4/8/32/16 s (Particules mapping), not
       hardware's mono column (8/16/20/32 s).
5. [ ] Per-sample feedback topology (enables Karplus-Strong; done).
6. [ ] TIME multiplier range (1–16×) and envelope shape family are tunable
       constants; some exposed as menu sliders — tune by ear later.
7. [ ] Float RecordingBuffer reused from Particules (int16 documented as
       fallback if MM memory bandwidth ever matters — CPU came in at
       ~0.19% host-relative, so unlikely).
8. [ ] VCV-adapter route on MetaModule (like Particules), no native core.
9. [ ] **SlowRandomLfo wander is identical across module instances** (the
       per-instance salt is stored but unused, per plan). Two Échos in one
       patch randomize in lockstep. Keep (deterministic/cheap) or seed
       per-instance? (Ledger T6 — your call.)
10. [ ] Reverb, AGC, buffer persistence: out of scope for v1 (spec
        non-goals) — confirm.

## Housekeeping (non-blocking)

- [ ] Échos manual page (Echos.md) doesn't exist yet; README section says
      documentation to come.
- [ ] screenshots/Echos.png — take one when the panel design settles.
- [ ] param_ranges.json (yml-to-vcv project) has no Échos entry yet: run
      the check-metamodule-coverage / add-vcv-plugin-params skills before
      the next MM release. (metamodule/plugin-mm.json already validates in
      the .mmplugin build.)
