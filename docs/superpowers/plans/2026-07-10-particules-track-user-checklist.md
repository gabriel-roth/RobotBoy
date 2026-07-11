# Particules track — user-run checklist (2026-07-10)

Branch `particules-track` (10 commits, final review READY TO MERGE). These are the
manual GUI/simulator/listening checks; record results here, then merge.

- [ ] **1. Grain LED (F5)** — VCV + simulator: dim flashes at sparse density, steady
  glow near full brightness when dense. Constants (0.25 floor, full at \~10 grains)
  are taste — tune by eye if wanted.

- [ ] **2. Input readout (F6)** — open the context menu with audio playing: "Input"
  row shows a live dB level on VCV, a static-but-plausible level on MetaModule;
  unplugged reads "silent".

- [ ] **3. Scale lock (F3)** — Lock pitch → Major, Root → D, melodic source: grains
  land in D major; Off/Octaves/Octaves+5ths behave as before. Re-check after a
  sample-rate change (scale must survive engine re-init). Note: the Root submenu's
  enabled state refreshes on menu re-open.

- [ ] **4. Trigger separation (F7)** — enable "Grain trigger on R output", crank
  density, scope the R output: distinct pulses with visible gaps, no continuous
  high level; a downstream trigger module fires repeatedly.

- [ ] **5. Undo (F8, VCV only)** — change each menu option (SEED CV mode, Lock
  pitch, Root, auto gain toggle, grain trigger, dry-follows-gain) and Ctrl-Z each.
  The manual-gain slider and "Clear buffer" are deliberately not undoable.

- [ ] **6. Dry-follows-gain (Q9)** — quiet input, auto gain active, sweep DRY/WET:
  no level jump through the middle (option defaults ON). Toggle it off and confirm
  DRY/WET = 0 is bit-transparent again. Old patches load with the new default —
  expected.

- [ ] **7. Reverb sleep (P1, MetaModule)** — patch with reverb at 0 shows the CPU
  drop after \~1 s; turning reverb up from 0 produces no burp or stale tail; a tail
  rung out just before a quick 0-and-back knob dip is preserved.

- [ ] **8. Disabled menu items on MetaModule (from final review)** — eyeball how
  MM renders the two disabled items (the "Input" readout row; "Root" while in a
  legacy pitch-lock mode) — MM's menu shim may style disabled items differently
  from VCV.

## Results

(record outcomes here)
