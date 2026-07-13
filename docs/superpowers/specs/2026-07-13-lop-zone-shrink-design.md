# Löp panel: shrink the grey zone to the top two rows

## Goal

Make the grey control zone (the neutral rect behind the controls) back only
the **top two** control rows — Row 1 (Size/Pos/Speed/Jitter) and Row 2
(Trig/Jump/Grid) — and no longer extend behind Row 3 (Dry/Wet/Clear/Record) or
the audio row. Shift Row 3 down to close the gap to the audio row and open
room above Row 3 for the rect's bottom edge.

## Current state (`panel-specs/lop.yaml`)

- Zone: `{x: 1.5, y: 35.15, w: 57.96, h: 73.6}` → bottom edge at **y=108.75**,
  sitting behind all three control rows (through Row 3's jacks at cy 102.8).
- Control centers (cy): Row 1 knobs 46.05 / CV 58.0 · Row 2 74.15 · Row 3 knobs
  90.85 / jacks 102.8 · Audio 116.05.
- Row 3 `gap_above: 2.0`. Audio row placed via nudges `[0, 3.10]` on
  `AUDIO_L_INPUT`, `OUT_L_OUTPUT`, `OVERDUB_PARAM` (natural cy 112.95 → 116.05).

## Target

1. **Zone shrinks from the bottom.** Top edge unchanged (`y: 35.15`, just under
   the screen). Bottom edge moves up into the gap between Row 2 and Row 3, so
   the rect backs only Row 1 and Row 2. The bottom edge **encloses the Grid
   value ring** (the Ø/4/8/16/32/64 numbers below the Grid knob belong to
   Row 2), sitting ~3 mm below the ring. New height ≈ 48 (down from 73.6);
   exact value tuned on the render.

2. **Row 3 shifts down** ~3–4 mm (increase its `gap_above`). This closes the
   Row 3 → audio gap and opens space above Row 3 for the rect's bottom edge and
   Row 3's labels.

3. **Audio row stays at cy 116.05** — preserves the Loooop bottom-jack
   alignment. Row 3 moves down toward it. Shifting Row 3 down ~3.1 mm puts the
   audio row at cy 116.05 *naturally*, so the three `[0, 3.10]` nudges can be
   dropped (a simplification). Final shift amount tuned on the render; if it
   diverges from 3.1, the audio nudge is re-derived to keep cy 116.05 rather
   than reintroduced blindly.

## Decisions

- **Only geometry changes.** No param/enum/widget changes; no change to Row 1,
  Row 2 contents, the screen, or the audio row contents. The zone is a
  faceplate rect (art only), and Row 3's downshift only moves coordinates.
- **Exact bottom-edge y and Row 3 shift are eyeball calls**, tuned by
  regenerating `res/Lop.svg` and reviewing the render (the established
  "re-tune, you review" loop).

## Downstream sync (same chain as the Overdub relocate)

Row 3's controls move, so after finalizing the spec + SVG:
1. Regenerate `res/Lop.svg`.
2. Re-sync the `mm2px(Vec(...))` control coords in `src/loooop/Lop.cpp` (the
   Row 3 controls and, if the nudge changes, the audio row).
3. Run `metamodule/loooop/sync_info_positions.py` (header coords + display rect).
4. Regenerate the MetaModule faceplate PNG.

The zone rect itself has no VCV/MM control to sync — it only affects the SVG
faceplate and the regenerated MetaModule PNG.

## Verification

- SVG regenerates without LayoutError; the grey rect backs Row 1 + Row 2 only,
  its bottom edge ~3 mm below the Grid value ring and clear of Row 3's labels.
- Row 3 sits lower, with a smaller gap to the audio row and more space above.
- Audio row still at cy 116.05.
- Coordinates consistent across SVG / `Lop.cpp` / `Lop_info.hh`; VCV + MetaModule
  build; regression suite green.
- User GUI checklist: panel renders, rect placement reads right, all controls
  hit-test, no behaviour change.

## Out of scope

- Zone color/opacity (unchanged — this is geometry only; use the
  picking-panel-rect-colors skill separately if that ever comes up).
- Any DSP or behaviour change.
