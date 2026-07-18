# Loooop & Löp changes — design

Date: 2026-07-18
Branch/worktree: `worktree-loooop-lop`

Five changes to the Loooop (4-head) and Löp (1-head) looper modules. Both are
pre-release, so param/element ordering is chosen for clarity, not
back-compatibility with saved patches. The VCV `ParamId`/`InputId` order and the
MetaModule `Elements`/`Elem` order MUST still mirror each other element-for-element
(`metamodule/loooop/*_info.hh`), because VCV↔MetaModule patch conversion maps by
position and we want cross-host portability within this release.

Shared engine: `src/loooop/dsp/LoopEngine.{hpp,cpp}` (both modules construct a
`LoopEngine`). VCV widgets: `src/loooop/Loooop.cpp`, `src/loooop/Lop.cpp`.
MetaModule: `metamodule/loooop/{Loooop,Lop}Core.cc`, `{Loooop,Lop}_info.hh`,
`QlpElements.hh`. Panels: `panel-specs/{loooop,lop}.yaml` → panel-gen →
`res/{Loooop,Lop}.svg` → hand-synced `mm2px` coords in the `.cpp` + MetaModule
info positions + MetaModule PNG.

---

## 1. "Trigger when recording" menu setting (Loooop + Löp, VCV + MetaModule)

New module-level menu control with two mutually-exclusive options:

- **Stops recording** — default; current behavior.
- **Starts overdubbing** — the record toggle that closes the initial recording
  pass instead freezes the loop length and immediately continues recording as an
  overdub pass in the currently selected write mode.

If Overdub = **Lock**, the control is ignored and recording stops (Lock means
overdub is disabled and the loop is untouchable).

### Engine

`LoopEngine::toggleRecord()` → `toggleRecord(bool continueOverdub = false)`.

The flag matters only in the branch that closes the **initial** recording pass
(`recording_ && loopLen_ == 0`):

- If `continueOverdub && overdubEnabled_`:
  - freeze `loopLen_ = writeIdx_`; publish `dispLoopLen_`; `bumpWaveformRevision()`.
  - begin an overdub pass without leaving `recording_`: `writeIdx_ = 0`; arm the
    overdub declick ramp exactly as the overdub-start branch does
    (`odGain_ = xfadeSamples_ ? 0 : 1`, matching `odGainStep_`, `stopPending_ = false`);
    seed the Decay filter from `bufL_/R_[0]` when `writeMode_ == Decay`.
  - `recording_` and `dispRecording_` stay true.
- Else: stop, exactly as today.

`overdubEnabled_` is already `false` whenever Overdub = Lock (set every sample by
`applyOverdub`), so the Lock override needs no special case.

All other `toggleRecord` branches (start recording; toggle/stop an overdub pass;
legacy zero-crossfade path) are unchanged. Subsequent toggles once overdubbing has
begun use the existing overdub-stop logic.

Both the Record button and the Record-trigger jack call `toggleRecord`, so both
honor the setting.

### VCV param

Add a global menu-only switch to each module, placed in the trailing menu-only
block next to `CROSSFADE_PARAM`:

- name suggestion: `TRIG_WHEN_REC_PARAM`
- `configSwitch(..., 0.f, 1.f, 0.f, "Trigger when recording", {"Stops recording", "Starts overdubbing"})`
  with `randomizeEnabled = false` (matches the other mode switches).
- Loooop: single module-wide switch (Record is a global control, not per-head).
- Both `process()` methods read it and pass `> 0.5f` as `continueOverdub` to
  `toggleRecord`.

VCV menu: on Löp a single `createBoolMenuItem`-style entry is not ideal for a
two-named-option choice; use `createIndexSubmenuItem("Trigger when recording",
{"Stops recording", "Starts overdubbing"}, getter, setter)` on both modules so the
two option names show as a radio submenu.

### MetaModule parity

- Add a new `AltParamChoiceLabeled` element type in `QlpElements.hh` (e.g.
  `QlpTrigWhenRecAlt`) with the two option labels, mirroring `QlpCrossfadeAlt` /
  `QlpTrigModeAlt`.
- Append the element to both `Loooop_info.hh` and `Lop_info.hh` in the menu-only
  block, and add the matching `Elem` enum entry — keeping VCV `ParamId` order and
  MetaModule `Elem` order in lockstep.
- `LoooopCore`/`LopCore`: read `getState<TrigWhenRecAlt>() == 1` and pass it as
  `continueOverdub` to `engine_.toggleRecord(...)` at the record edge.

---

## 2. One-shot Size/Position reflected on the display (Loooop + Löp, engine)

Currently an armed one-shot head (`playing == false`) is skipped in
`LoopEngine::process` (`if (!h.playing) continue;`), so `dispWinStart01_`/
`dispWinEnd01_` are never republished and the displayed window is frozen while
the head waits for a trigger. Turning Size (or Position) doesn't move the lane.

Fix: in `process`, when `loopLen_ > 0` and a head is not playing, still compute
`windowBounds(h, winStart, winLen)` and publish:

- `dispWinStart01_[i] = winStart / loopLen_`
- `dispWinEnd01_[i] = (winStart + winLen) / loopLen_`
- `dispPos01_[i]` = the window start, direction-aware
  (`h.speed < 0 ? winStart + winLen - 1 : winStart`) so the parked head marker
  sits where the next trigger will start playback and stays inside the window bar.

No audio is produced for a non-playing head (skip `readHead`/`advanceHead`; leave
`dispPlaying_` false). The renderer already draws a non-playing lane with the
"armed" dim tone from these fields, so no renderer change is needed. Shared engine
⇒ both modules benefit. `playing == false` only ever occurs for one-shot heads, so
this changes nothing for normal looping heads.

---

## 3. Reverse Clear/Record on Löp (panel + wiring)

Today Löp's bottom control row is DryWet (col 1), **Clear** (col 2), **Record**
(col 3). Make it DryWet, **Record** (col 2), **Clear** (col 3) — Record left of
Clear — moving button, trig jack, and label together.

- `panel-specs/lop.yaml`: swap the `col:` values so `RECORD_PARAM`,
  `RECORD_TRIG_INPUT`, and the `Record` label land in col 2 and the `CLEAR_*` /
  `Clear` label land in col 3. The row-3 vertical connectors reference columns, so
  they follow automatically.
- Regenerate `res/Lop.svg` via panel-gen (vcv-panel skill).
- Sync the swapped `mm2px` coordinates by hand into `src/loooop/Lop.cpp`
  (`RECORD_PARAM` + `RECORD_TRIG_INPUT` ↔ `CLEAR_PARAM` + `CLEAR_TRIG_INPUT`
  `addParam`/`addInput` positions).
- Update the MetaModule positions in `metamodule/loooop/Lop_info.hh`
  (`RecordButton`, `ClearButton`, `RecTrigIn`, `ClearTrigIn` x/y) — via
  `sync_info_positions.py` if it covers these, else by hand — and regenerate the
  MetaModule PNG.

Param/Elem *order* is unchanged; only on-panel x/y coordinates move. The
Overdub/Dub-Mode button (bottom audio row, x=30.48) is unaffected.

---

## 4. Rename Magenta → Purple (rename only)

`src/loooop/HeadColors.hpp`: head-4 `name` `"Magenta"` → `"Purple"`. RGB stays
`0xFF5AF0`. This flows automatically into `kHeadNames` → the context-menu label
("Purple playhead") on Loooop.

Update head-4 descriptive comments that name the color for consistency
(`src/loooop/Loooop.cpp` header comment, `LoopWaveformRenderer.hpp`,
`panel-specs/loooop.yaml`). **Leave** the unrelated Lock-LED "magenta" comment in
`LooperModuleDSP.hpp` — that describes the Lock overdub LED color (RGB 1,0,1),
which is a genuinely different magenta, not head 4.

Verify `tests/test_head_colors.py` asserts RGB values (not names); it should need
no change. If it asserts the name string, update it.

---

## 5. Overdub → "Dub Mode" panel label (Loooop + Löp panels)

Change the on-panel label text under the Overdub button from `Overdub` to
`Dub Mode` on both panels:

- `panel-specs/loooop.yaml` and `panel-specs/lop.yaml`: label `text: Overdub` →
  `text: Dub Mode`. Stored title-case; panel-gen uppercases labels on render, so
  the panel shows "DUB MODE" like every other all-caps label.
- Regenerate both `res/*.svg` and the MetaModule PNGs.
- The `OVERDUB_PARAM` control name / tooltip in the info files and `configSwitch`
  ("Overdub mode") is a separate string; leave it or align to "Dub mode" for
  consistency — cosmetic, no functional effect. Keep the `OVERDUB_PARAM`
  identifier unchanged.

---

## Verification

- Engine behavior (items 1–2): unit/headless coverage.
  - "Starts overdubbing" closes the loop and keeps `isRecording()` true, writing an
    overdub pass in the selected write mode; with Lock it stops. Add a
    `tests/loooop` engine test driving `toggleRecord(true)` across the initial-pass
    close in each write mode + Lock.
  - Armed one-shot: after `setOneShot(true)` with a loop present, changing
    `setSize`/`setPosition` updates `displaySnapshot().winStart01/winEnd01`. Add an
    engine test.
- Panel/menu items (3, 5) and the Purple rename (4): existing test suite (head
  colors, panel parity/overlap tests) plus a user GUI checklist at the end (per the
  no-agent-GUI-sim rule) — visual confirmation of the swapped Löp layout, the "DUB
  MODE" label on both panels, the "Purple playhead" menu label, and the new
  "Trigger when recording" submenu.
- Build both targets: `make -C vcv` (+ install copy) and the MetaModule cmake
  build; run the existing test suite.

## Out of scope

- No change to head-4's RGB color (rename only).
- `kMidi` and other inherited dead code untouched.
