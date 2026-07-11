# Löp Overdub/Grid polish — user checklist (GUI checks Claude can't run)

Build installed to Rack2 plugins dir on 2026-07-11. In VCV Rack:

- [ ] Löp panel: Overdub is now an LED button above Record, on the Size/D-W
      knob center line; Grid knob + grey value ring sit on the same line.
      (Note: the OVERDUB/GRID text labels sit 1.5/1.0mm lower than SIZE/D-W's
      — labels travel with the nudged controls. Confirm it reads OK.)
- [ ] Clicking Overdub cycles Layer (blue) → Decay (amber) → Add (green) →
      Replace (red) → Lock (purple) → wraps to Layer. Colors match Loooop's.
- [ ] With a loop recorded on Löp: each mode writes like Loooop's same mode;
      Lock makes the Record button/trigger a no-op on the loop contents.
- [ ] Löp menu shows only One-shot on trigger / Speed CV = V/Oct / Crossfade
      (Overdub, Write mode, and Grid entries are gone).
- [ ] Both Loooop and Löp show dark screws (no silver) in Rack.
- [ ] Loooop's Grid value ring is dimmer grey now too.
- [ ] PATCH COMPAT: pre-2026-07-11 patches/presets that used Löp load with
      Overdub/Grid scrambled (Write mode was absorbed; ids shifted, like
      Loooop's earlier absorb). Old "Overdub Off" loads as Layer; the old
      Write-mode value lands on Grid. Re-save any Löp patches you keep.
- [ ] screenshots/Lop.png in the repo is stale — retake when convenient.
- [ ] MetaModule: no behavior change (Overdub/Write mode/Grid stay menu
      params there); rebuild the .mmplugin whenever you next cut one so the
      updated faceplate PNG ships.

## Round 2 (same day): layout rework, screws, display strip

- [ ] Löp rows now read Speed/Pos/Jitter, then Trig/Jump/Overdub/Grid, then
      Size/D-W/Clear/Record — labels, control centers, and jacks each on
      shared lines per row.
- [ ] Both modules show dark screws in Rack (stock ScrewBlack widgets — the
      SVG's drawn dots were invisible against the background).
- [ ] Löp's loop display: lane strip twice Loooop's height, waveform
      correspondingly shorter, strip purple (#bf5af2) — on VCV and on the
      rebuilt .mmplugin (metamodule/metamodule-plugins/RobotBoy.mmplugin).
