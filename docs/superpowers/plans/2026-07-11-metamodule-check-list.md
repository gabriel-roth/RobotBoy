# MetaModule check list — after the round-4 / MF-20 / loooop-track merges (2026-07-11)

Consolidated MetaModule-only verification for everything now on `main`. Covers
the round-4 changes plus the still-pending items pulled from the per-module
track checklists. The full GUI checklists (VCV + MM) live alongside this file:
`2026-07-10-loooop-menu-rework-user-checklist.md`,
`2026-07-10-particules-track-user-checklist.md`,
`2026-07-11-lop-overdub-grid-polish-user-checklist.md`.

**Note on MF-20:** MF-20 *is* a MetaModule module — the MM build compiles the
same `src/mf20/MF20Filter.cpp` (there is no separate MM core), so the
cutoff-floor fix reaches MetaModule automatically once the `.mmplugin` is
rebuilt. It needs an MM check (item 3 below).

---

## A. New behavior this session — verify on device / simulator

- [ ] **1. Loooop & Löp — sample-rate change preserves the loop.** Record a
      loop on Loooop, then on Löp; change the MetaModule sample rate
      (patch/global). The loop must keep playing. Previously the MM cores called
      `engine_.reset(sr)` and wiped the loop; they now call
      `engine_.setSampleRate(sr)` like VCV. *(round-4 Task 3)*

- [ ] **2. Particules — grain steal-and-replace at saturation.** Crank Density
      (and/or raise SIZE and lower CPU headroom) until the grain pool saturates.
      The newest grains should always sound — the oldest grain is stolen —
      instead of new triggers silently vanishing; listen for clicks/pops at the
      steal (there should be none). *(round-4 Task 5)*

- [ ] **3. MF-20 — CV sweeps cutoff below 20 Hz.** Patch a deep negative CV
      into an LP/HP/Total cutoff input with the attenuverter up: the filter
      should audibly close well below 20 Hz (near-silent), where before it
      floored at 20 Hz. Same shared `MF20Filter.cpp` as VCV, so the behavior
      should match VCV. *(MF-20 fix, commit 3a9647c)*

- [ ] **3b. Loooop & Löp — Overdub is now a per-mode colored switch (MM only).**
      *(2026-07-12, commit 471671c — plan `2026-07-12-overdub-mm-labeled-knob.md`)*
      On MetaModule, Overdub changed from a momentary RGB button to a five-position
      **FlipSwitch**: each position draws a dark button with the mode's colour LED
      baked into the frame image, and the firmware shows the mode name. (An earlier
      knob+LED attempt was abandoned — the firmware yanks a knob in front of any
      overlapping light, so the LED was always buried.) Verify:
      - Stepping Overdub cycles **Layer(blue) → Decay(amber) → Add(green) →
        Replace(red) → Lock(purple)**, the button changes colour per mode, and the
        **mode name** shows in the Adjust popup (instead of "Pressed/Released").
      - **Coverage** — the coloured button frame fully hides the faceplate's old
        button graphic underneath (no double-image / edges peeking). It draws on top
        of a *button* on the faceplate now, so it should look consistent.
      - **Art quality** — the frames are first-pass (dark bevel + glowing colour dot,
        `metamodule/assets/Loooop/overdub_*.png`). Judge whether the look/size suits
        the panel; easy to regenerate (`~/.claude/jobs/.../gen_overdub_frames.py`).
      - **Löp size** — the frame draws at a fixed ~16 px on both modules; on the
        smaller Löp panel confirm it isn't crowding the Grid knob.
      - **Interaction** — it's a stepped selector (advance with Adjust), not a
        momentary press. Confirm that feels OK.
      - **Patch round-trip** — set Overdub to e.g. Replace, save, reload → restores.
        (Patches saved with the *old* momentary build reopen at Layer — expected.)

- [ ] **3c. Particules — Quality is now a labeled 4-position colored switch (MM only).**
      *(2026-07-12, commit df82b00)* Desktop keeps its momentary RGB bezel; only the
      MetaModule build swaps in a 4-position `SvgSwitch` (via `#ifdef METAMODULE`),
      which the VCV→MM adapter renders as a labeled FlipSwitch. Verify:
      - Stepping Quality cycles **Bright digital (white) → Cold digital (cyan) →
        Sunny tape (orange) → Scorched cassette (magenta)**, the button shows the
        mode colour, and the **mode name** appears in the Adjust popup.
      - The coloured frames actually render (not blank/missing — confirms the
        `res/quality_*.svg` → bundled `quality_*.png` asset resolution works on MM).
      - **Desktop unchanged** — in VCV Rack, Quality is still the momentary
        click-to-cycle RGB bezel (this change is MM-only).
      - Audio: each mode still applies its quality colouring as before (the mode now
        comes from the switch position instead of an internal counter).

## B. Build / packaging — when you next cut a `.mmplugin`

- [ ] **4. Rebuild `RobotBoy.mmplugin`** (`cmake --build metamodule/build -j8`).
      It should build, and the derived project version should read **2.0.1**
      (now sourced from `plugin.json`, not hardcoded). *(round-4 Task 12 —
      already verified with the ARM toolchain here; confirm on your setup.)*

- [ ] **5. Faceplates ship in the rebuilt plugin** — the updated Löp faceplate
      (Overdub LED button + Grid knob, purple double-height loop-display strip)
      and Loooop's dimmer grey Grid value ring are present on the MM build, not
      just VCV. *(Löp Overdub/Grid polish)*

## C. Loooop / Löp — pending from the track checklists

- [ ] **6. Grid-exclude options** — the options list shows "Grid 1 exclude" ..
      "Grid 4 exclude" (Off/On) grouped with each head's other settings, and
      excluding a head audibly frees it (its window slides while the others keep
      snapping) with Grid on. *(menu-rework checklist #4)*

- [ ] **7. Löp menu params unchanged on MM** — Overdub, Write mode, and Grid
      remain menu params on MetaModule (the VCV panel button is VCV-only);
      confirm they still select correctly as menu params. *(Löp polish)*

- [ ] **8. CPU headroom** — with the new Loooop write modes, Grid quantization,
      and the level/pan/dry-wet smoothers active, confirm MetaModule CPU stays
      within headroom (no dropouts) on a representative patch. *(loooop-track
      pending MM CPU check)*

## D. Particules — pending from the track checklist

- [ ] **9. Input readout** — with audio playing, the context-menu "Input" row
      shows a static-but-plausible dB level on MetaModule; unplugged reads
      "silent". *(particules checklist #2)*

- [ ] **10. Reverb sleep** — a patch with reverb at 0 shows a CPU drop after
      ~1 s; turning reverb up from 0 produces no burp or stale tail; a tail rung
      out just before a quick 0-and-back knob dip is preserved. *(particules
      checklist #7)*

- [ ] **11. Disabled menu-item rendering** — eyeball how MM's menu shim styles
      the two disabled items (the "Input" readout row; "Root" while in a legacy
      pitch-lock mode) — MM may style disabled items differently from VCV.
      *(particules checklist #8)*

## E. Loooop / Löp — VCV↔MM param-ID alignment fix (2026-07-14)

- [ ] **12. VCV patch loads onto MetaModule with knobs on the right controls.**
      The VCV `ParamId` order for Loooop and Löp was reordered to match the
      MetaModule `Elem`/`Elements` order exactly (globals → per-head knobs →
      trailing Crossfade/Trigger/Speed-V-Oct/Grid-exclude "Options"). Rebuild
      and reinstall the VCV plugin, build a test patch on VCV (set distinctive,
      asymmetric values — e.g. Size 1 low, Dry/Wet high, Crossfade Off, head-2
      one-shot on), then load it on MetaModule. Every knob/switch should read the
      same value it had in VCV — especially Size 1, Dry/Wet, and the per-head
      menu params, which were the ones that scrambled before. Inputs/outputs
      (cables) were already aligned and should be unaffected.
      *(fix 2026-07-14; canonical order = MM's; VCV enums changed, MM info files
      unchanged. Patches saved with the OLD VCV build will load shifted —
      expected. MetaModule-native patches are unaffected.)*

## Results

(record outcomes here)
