# Löp Overdub/Grid polish — user checklist (GUI checks Claude can't run)

Build installed to Rack2 plugins dir on 2026-07-11. In VCV Rack:

- [ ] Löp panel: Overdub is now an LED button above Record, on the Size/D-W
      knob center line; Grid knob + grey value ring sit on the same line.
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
