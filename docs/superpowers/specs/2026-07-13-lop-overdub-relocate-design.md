# Löp panel: relocate Overdub, swap center rows, even the spacing

## Goal

Rework the Löp (`Lop`) panel layout so the Overdub button moves out of the
crowded third control row into the bottom audio row, freeing the two center
rows to be evenly spaced.

## Current layout

Under the screen, `panel-specs/lop.yaml` defines:

- **Row 1** (params): Size / Pos / Speed / Jitter
- **Row 2** (params, center): Dry/Wet / Clear / Record
- **Row 3** (params, center): Trig / Jump / **Overdub** / Grid
- **Audio row** (inputs): In / Out (stereo pairs)

The Overdub label is ~14.4 mm wide at label size, so row 3 uses a weight
palindrome (inner columns 1.09, outer 0.91) to keep the four-column grid
centered on the panel. That palindrome and its explanatory block comment exist
only to accommodate the wide OVERDUB column.

## Target layout

- **Row 1** (params): Size / Pos / Speed / Jitter — *unchanged*
- **Row 2** (params, center): **Trig / Jump / Grid** — old row 3 with Overdub
  removed; three equal-weight columns, no weights
- **Row 3** (params, center): **Dry/Wet / Clear / Record** — old row 2, moved
  down; three equal-weight columns
- **Audio row** (inputs): **In / Overdub / Out** — Overdub button centered
  between the stereo pairs, label underneath

## Decisions

1. **Overdub is relocated, not recreated.** It keeps `control: button`,
   `widget: OverdubButton`, and `OVERDUB_PARAM`. Only its panel position
   changes. The param enum is untouched, so existing `.vcv` patches are not
   scrambled (contrast the earlier Loooop param-reorder incident).

2. **Even spacing falls out for free.** With the wide OVERDUB label gone from
   the center rows, both center rows have three equal columns. Delete the
   weight values (`weight: 1.09` / `weight: 0.91`) from all items in the two
   center rows and delete the palindrome block comment.

3. **Overdub label below the button.** Set `label_side: below` on the Overdub
   item so its label matches the In/Out jack labels in the same row. The
   generator already supports per-item `label_side`.

4. **Overdub sits centered on the jack line.** In the audio row the button's
   center aligns with the In/Out jack centers, and its label sits at the same
   baseline as the In/Out labels — one clean row, no vertical nudge for the
   button.

5. **Spacing is re-tuned by hand, reviewed by render.** The row swap changes
   which physical row previously carried the extra vertical air (old row 3 had
   `label_gap: 1.8` for the Grid value ring). After the swap, pick fresh
   `gap_above` / `label_gap` / `stack_gap` values, regenerate the SVG, and the
   user eyeballs the render and calls out nudges. No forced uniform grid.

## Downstream sync (required, per the lop.yaml header note)

`metamodule/loooop/Lop_info.hh` is HAND-MAINTAINED. After editing the spec:

1. Regenerate the Löp panel SVG from `lop.yaml`.
2. Run `python3 metamodule/loooop/sync_info_positions.py` to carry the new
   control positions into `Lop_info.hh` and the VCV display rect (delegates to
   `~/Dev/vcv-panel-gen/mm_sync.py --strict`, name-matched through
   `sync-map-lop.yaml`).
3. Regenerate the MetaModule faceplate PNG.

## Verification

- The panel-spec drift test still passes.
- The Löp SVG renders with the target layout; the two center rows read as
  evenly spaced; Overdub sits between In and Out with its label underneath.
- Build the VCV plugin and confirm Löp loads with Overdub in its new spot and
  all controls hit-test correctly.
- MetaModule faceplate/positions match (RgbLight overlay for Overdub still
  aligned, per the MM labeled-controls approach).
- Overdub still toggles mode as before (no functional change).

## Out of scope

- No change to Overdub's behavior, param range, or the OverdubButton widget
  itself — only its position and label side.
- No change to Row 1 or the screen.
- kMidi (inherited dead code) is untouched.

## Worktree

Per project convention, module work happens on the `lop-track` worktree/branch,
not `main`.
