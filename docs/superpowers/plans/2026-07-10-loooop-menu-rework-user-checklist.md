# Loooop menu rework — user-run checklist (2026-07-10)

Branch `loooop-track` (menu rework: commands-first context menu, color-named
playheads, per-head Exclude from Grid). Engine change is test-covered; these
are the GUI/listening checks. Record results here, then merge.

- [ ] **1. Menu structure (VCV)** — right-click Loooop: Crossfade loop seams,
  then **One-shot**, **Speed CV is V/Oct**, and **Exclude from Grid**, each
  opening a submenu listing *Red / Green / Blue / Yellow playhead* with
  checkmarks (One-shot checked = one-shot; unchecked = restart-at-window-start,
  which is deliberately unnamed).

- [ ] **2. Tooltips** — hover per-head knobs and jacks: labels read
  "Red playhead size", "Green playhead pan CV", etc., and the colors match
  the panel column tints / display markers.

- [ ] **3. Exclusion behavior (VCV)** — record a loop, set Grid to 8,
  shrink two heads' Size. Exclude the Red playhead: its window slides
  smoothly with Position/Jitter while the other head keeps snapping;
  re-include and it snaps again. Grid bars stay drawn throughout.

- [ ] **4. MetaModule options** — options list shows "Grid 1 exclude" ..
  "Grid 4 exclude" (Off/On) grouped with each head's other settings, and
  excluding a head audibly frees it while Grid is on.

## Results

(record outcomes here)
