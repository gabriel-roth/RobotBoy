# Löp Overdub Relocate + Row Swap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the Löp Overdub button into the bottom audio row (In / Overdub / Out) and swap the two center control rows so they can be evenly spaced.

**Architecture:** Löp's panel is generated from a declarative spec (`panel-specs/lop.yaml`) by the external `vcv-panel-gen` tool. Editing the spec and regenerating produces the canonical `res/Lop.svg`; the SVG's hidden components layer is the single source of truth for every control's center. Those centers are then propagated by hand-run sync into three consumers: the VCV widget positions in `src/loooop/Lop.cpp` (`mm2px(Vec(...))` lines), the hand-maintained MetaModule header `metamodule/loooop/Lop_info.hh` (+ the VCV display rect), and the MetaModule faceplate PNG.

**Tech Stack:** Python panel generator (`~/Dev/vcv-panel-gen`), C++ (VCV Rack + MetaModule plugin), Inkscape (faceplate PNG), `mm_sync.py` (header position sync).

## Global Constraints

- Panel is **12 HP**, dark, no group tints — one flat control set. Do not change width, screen, or Row 1 (Size/Pos/Speed/Jitter).
- **The `OVERDUB_PARAM` enum position in `src/loooop/Lop.cpp` must not change** — only its panel coordinates move. Reordering params scrambles existing `.vcv` patches; this task must not touch the `ParamId` enum.
- **Never hand-edit `res/Lop.svg`** — it is generated. Edit the spec and regenerate.
- **`metamodule/loooop/Lop_info.hh` is hand-maintained** for element names/order/structs; only its *coordinates* are synced (by `mm_sync.py --strict` via `sync_info_positions.py`). Do not hand-edit coordinates there.
- Build/install per repo convention (no `build-install.sh`): `make -C vcv -j8`, then copy `plugin.dylib`/`plugin.json` and rsync `vcv/res/` into the Rack2 plugins dir.
- Commit messages: short (≤15 words), no AI attribution / no `Co-Authored-By` line.
- GUI/visual confirmation is a **user-run checklist**, never agent-driven simulator/GUI testing.

## Reference: current vs. target layout

Current control rows (`panel-specs/lop.yaml` `rows:`):
- Row 1 (params): Size / Pos / Speed / Jitter
- Row 2 (params, center): Dry/Wet / Clear / Record
- Row 3 (params, center): Trig / Jump / **Overdub** / Grid  ← weight palindrome (1.09/0.91) to fit the wide OVERDUB label
- Audio (inputs): In / Out (stereo pairs)

Target:
- Row 1 (params): Size / Pos / Speed / Jitter — *unchanged*
- Row 2 (params, center): **Trig / Jump / Grid** — 3 equal columns, no weights
- Row 3 (params, center): **Dry/Wet / Clear / Record** — 3 equal columns
- Audio (inputs): **In / Overdub / Out** — Overdub is a `type: buttons` item on the jack center line, label below

**Why `type: buttons` (not `control: button`):** `control:` is only valid in a `params` row (`spec.py:339`), and a `params` row can't hold a stereo `pair` (`spec.py:401`). `ROW_TYPES["buttons"]` (`constants.py:98`) is a first-class param-kind item type that the jacks-row layout branch (`layout.py:523,571`) places on `row_cy` — the same center line as the In/Out jacks. So the button lives honestly in the audio row with no rule change and no nudge hack. The component id stays `OVERDUB_PARAM#OverdubButton`, so the MetaModule name-match sync is unaffected.

## File structure

- `panel-specs/lop.yaml` — the panel spec. Edited in Task 1.
- `res/Lop.svg` — generated panel (canonical). Regenerated in Task 1. `vcv/res/Lop.svg` is auto-synced from it at build time by `vcv/Makefile:10` (`cp -R ../res res`); never edit it directly.
- `src/loooop/Lop.cpp` — VCV widget: control `mm2px(Vec(...))` lines (174–195) re-synced in Task 2; param enum (line ~19) untouched.
- `metamodule/loooop/Lop_info.hh` — MetaModule element coords, synced in Task 2.
- `metamodule/assets/Loooop/Lop.png` — MetaModule faceplate, regenerated in Task 2.
- `metamodule/loooop/sync_info_positions.py` — existing sync driver (header coords + VCV display rect). Run, not edited.

---

### Task 1: Panel spec edit + SVG regeneration + visual sign-off

**Files:**
- Modify: `panel-specs/lop.yaml` (the `rows:` block and its explanatory comments)
- Generated: `res/Lop.svg`

**Interfaces:**
- Produces: `res/Lop.svg` whose components layer contains, among others, `OVERDUB_PARAM#OverdubButton` centered on the audio jack line (cy ≈ 116.05) horizontally between the In pair (max cx) and Out pair (min cx). Task 2 reads these centers by component id.

- [ ] **Step 1: Rewrite the `rows:` block in `panel-specs/lop.yaml`**

Replace the two center `params` rows, their preceding palindrome comment block, and the audio `inputs` row with the following. Row 1 (Size/Pos/Speed/Jitter) and the `screen` row are unchanged.

```yaml
  # Row 2: Trig / Jump / Grid — three equal columns. With Overdub moved to the
  # audio row, the wide OVERDUB column is gone, so no per-column weights are
  # needed. label_gap 1.8 keeps the labels clear of the controls and the Grid
  # value ring below the knob.
  - type: params
    align: center
    gap_above: 2.0
    label_gap: 1.8
    items:
      - {label: Trig, name: TRIG_INPUT, type: inputs}
      - {label: Jump, name: JUMP_INPUT, type: inputs}
      - {label: Grid, name: GRID_PARAM, small: true,
         widget: RoundSmallBlackSnapKnob,
         values: ["Ø", "4", "8", "16", "32", "64"]}
  # Row 3: Dry/Wet knob+cv stack, then Clear and Record as stacked button+trig
  # groups (button over jack, tied by the connecting bar) — three equal columns.
  - type: params
    align: center
    gap_above: 2.0
    label_gap: 0.7
    stack_gap: 1.6
    items:
      - {label: Dry/Wet, name: DRYWET_PARAM, cv: DRYWET_CV_INPUT}
      - {label: Clear,  name: CLEAR_PARAM,  control: button,
         trig: CLEAR_TRIG_INPUT}
      - {label: Record, name: RECORD_PARAM, control: button,
         trig: RECORD_TRIG_INPUT}
  # Audio row: In / Overdub / Out. Overdub is a param-kind button (type:
  # buttons) sharing the jacks row — placed on the jack center line between the
  # stereo pairs, label below to match the In/Out labels.
  - type: inputs
    label_side: below
    gap_above: 1.3
    items:
      - {label: In,      name: AUDIO_L_INPUT, pair: AUDIO_R_INPUT}
      - {label: Overdub, name: OVERDUB_PARAM, type: buttons,
         widget: OverdubButton}
      - {label: Out,     name: OUT_L_OUTPUT,  pair: OUT_R_OUTPUT, type: outputs}
```

- [ ] **Step 2: Update the two header comment blocks in `panel-specs/lop.yaml`**

Update the top-of-file row description (currently describing row 2 = Dry/Wet…, row 3 = Trig/Jump/Overdub/Grid) to the new order, and the vertical-air comment (currently naming "Trig/Jump/Overdub/Grid labels" and "In/Out row"). Replace the row-list lines with:

```
#   row 1: Size / Pos / Speed / Jitter knob+cv stacks;
#   row 2: Trig and Jump jacks, then the Grid knob — Grid mirrors Loooop's: a
#          small 6-position snap knob (Off/4/8/16/32/64) with its values
#          printed close around it (Ø = off); RoundSmallBlackSnapKnob is shared
#          from src/loooop;
#   row 3: Dry/Wet knob+cv stack, then Clear and Record as stacked button+trig
#          groups (button over jack, tied by the connecting bar).
# The last row is In / Overdub / Out: the stereo In and Out pairs under single
# labels, with the Overdub mode button on the jack center line between them
# (label below, matching the jack labels).
```

And replace the vertical-air comment block with:

```
# Vertical air: rows 1 and 3 pull their label/knob/jack stacks in (label_gap
# 0.7, stack_gap 1.6 vs the 1.0/2.0 defaults); row 2 spends the space as
# label_gap 1.8, so the Trig/Jump labels and the Grid value ring below the knob
# stay clear.
#
# The In/Overdub/Out row drops to sit at the same height (cy 116.05) as
# Loooop's bottom In/Out jacks (re-tuned for the tighter rows above).
```

- [ ] **Step 3: Regenerate the SVG**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy
~/Dev/vcv-panel-gen/.venv/bin/python ~/Dev/vcv-panel-gen/panel_gen.py \
    panel-specs/lop.yaml --out res/Lop.svg
```
Expected: exits 0 and prints a one-line component summary. A `LayoutError` (collision/overflow — e.g. the "Overdub" label too wide for its column between the pairs) means the layout doesn't fit; adjust spacing/weights and rerun. It must succeed before continuing.

- [ ] **Step 4: Assert the Overdub landed on the jack line between In and Out**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy
python3 - <<'PY'
import xml.etree.ElementTree as ET
c={}
for n in ET.parse('res/Lop.svg').getroot().iter():
    i=n.get('id','').split('#')[0]
    if n.get('cx'): c[i]=(float(n['cx']) if False else float(n.get('cx')), float(n.get('cy')))
od=c['OVERDUB_PARAM']; inr=c['AUDIO_R_INPUT']; outl=c['OUT_L_OUTPUT']
print('Overdub', od, 'In-R', inr, 'Out-L', outl)
assert abs(od[1]-inr[1])<0.01 and abs(od[1]-outl[1])<0.01, 'Overdub not on jack line'
assert inr[0] < od[0] < outl[0], 'Overdub not horizontally between In and Out'
print('OK: Overdub on jack line, between In and Out')
PY
```
Expected: prints coordinates then `OK: Overdub on jack line, between In and Out`. If the assert fails, the layout is wrong — revisit the spec.

- [ ] **Step 5: Show the panel for visual sign-off and re-tune**

Run:
```bash
open -a Safari /Users/gabrielroth/Dev/RobotBoy/res/Lop.svg
```
(The components layer is `display:none`, so this shows faceplate art only — VCV draws the actual knobs/ports on top later.) Ask the user to eyeball: rows 2 and 3 read as evenly spaced; Overdub sits centered between In and Out with its label underneath aligned to the In/Out labels. Adjust `gap_above` / `label_gap` / `stack_gap` on the affected rows per feedback, rerunning Steps 3–4 after each change, until the user approves. **Do not proceed to Task 2 until the user signs off on the render.**

- [ ] **Step 6: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy
git add panel-specs/lop.yaml res/Lop.svg
git commit -m "Löp: move Overdub into In/Out row, swap+even center rows"
```

---

### Task 2: Propagate positions to VCV, MetaModule header, and faceplate

**Files:**
- Modify: `src/loooop/Lop.cpp` (control `mm2px(Vec(...))` lines only)
- Modify: `metamodule/loooop/Lop_info.hh` (coords, via sync)
- Generated: `metamodule/assets/Loooop/Lop.png`

**Interfaces:**
- Consumes: `res/Lop.svg` component centers from Task 1.
- Produces: `Lop.cpp`, `Lop_info.hh`, and `Lop.png` all consistent with the SVG. No new symbols.

- [ ] **Step 1: Re-sync the VCV control coordinates in `src/loooop/Lop.cpp`**

RobotBoy has no coord-sync script, so drive it with this one-shot helper. It reads each control center from the SVG and rewrites the matching `mm2px(Vec(x, y))` line by enum name, only when the value actually changed (keeps the diff minimal; leaves Row 1 and the `display->box` lines untouched — those have no `Lop::<NAME>` after the `Vec`, and the display rect is handled in Step 3).

```bash
cd /Users/gabrielroth/Dev/RobotBoy
python3 - <<'PY'
import re, xml.etree.ElementTree as ET
centers={}
for n in ET.parse('res/Lop.svg').getroot().iter():
    i=n.get('id','').split('#')[0]
    if n.get('cx') is not None and n.get('cy') is not None:
        centers[i]=(float(n.get('cx')), float(n.get('cy')))
def fmt(v):
    s=f"{v:.3f}".rstrip('0').rstrip('.')
    return s if s else "0"
pat=re.compile(r'(mm2px\(Vec\()(-?\d+\.?\d*),\s*(-?\d+\.?\d*)(\))(.*?Lop::)([A-Z0-9_]+)')
path='src/loooop/Lop.cpp'; out=[]; changed=0
for line in open(path).read().splitlines():
    m=pat.search(line)
    if m and m.group(6) in centers:
        nx,ny=centers[m.group(6)]
        ox,oy=float(m.group(2)),float(m.group(3))
        if round(ox,3)!=round(nx,3) or round(oy,3)!=round(ny,3):
            line=line[:m.start()]+f"{m.group(1)}{fmt(nx)}, {fmt(ny)}{m.group(4)}{m.group(5)}{m.group(6)}"+line[m.end():]
            changed+=1
    out.append(line)
open(path,'w').write("\n".join(out)+"\n")
print(f"rewrote {changed} coordinate line(s)")
PY
```
Expected: prints `rewrote N coordinate line(s)` (N ≈ 12–14 — the two center rows, the audio row, and Overdub; Row 1 unchanged).

- [ ] **Step 2: Review the coordinate diff**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy && git diff src/loooop/Lop.cpp
```
Expected: only `mm2px(Vec(...))` coordinates change for TRIG_INPUT, JUMP_INPUT, GRID_PARAM, DRYWET_PARAM/CV, CLEAR_PARAM/TRIG, RECORD_PARAM/TRIG, AUDIO_L/R_INPUT, OUT_L/R_OUTPUT, and OVERDUB_PARAM. Confirm the `ParamId`/`InputId`/`LightId` enums and the `<OverdubButton>` widget type are **unchanged**, and OVERDUB_PARAM's y ≈ 116.05.

- [ ] **Step 3: Sync the MetaModule header coords and the VCV display rect**

Run:
```bash
cd /Users/gabrielroth/Dev/RobotBoy
python3 metamodule/loooop/sync_info_positions.py
```
Expected: prints `Lop: ...` sync lines and `Lop: VCV display rect synced`, exits 0. This runs `mm_sync.py --strict` — a nonzero exit means a name mismatch between `Lop_info.hh` / `sync-map-lop.yaml` and the SVG (should not happen, since ids are unchanged). Loooop is processed too but its SVG is unchanged, so its files stay byte-identical.

- [ ] **Step 4: Regenerate the MetaModule faceplate PNG**

Run (Inkscape confirmed on PATH):
```bash
cd /Users/gabrielroth/Dev/RobotBoy
python3 ~/Dev/metamodule-plugin-sdk/scripts/SvgToPng.py \
    --input res/Lop.svg --output metamodule/assets/Loooop/ --layer panel
```
Expected: writes `metamodule/assets/Loooop/Lop.png` (rendered at 47.44 dpi; `--layer panel` drops the value-ring layer). Confirm the file's mtime updated.

- [ ] **Step 5: Commit**

```bash
cd /Users/gabrielroth/Dev/RobotBoy
git add src/loooop/Lop.cpp metamodule/loooop/Lop_info.hh metamodule/assets/Loooop/Lop.png
git commit -m "Löp: sync VCV/MetaModule positions for Overdub relocate"
```

---

### Task 3: Build, regression tests, and GUI verification checklist

**Files:** none modified (build + verify).

**Interfaces:**
- Consumes: the synced sources from Task 2.

- [ ] **Step 1: Build the VCV plugin**

```bash
cd /Users/gabrielroth/Dev/RobotBoy
make -C vcv -j8
```
Expected: compiles to `vcv/plugin.dylib` with no errors. (The Makefile first re-syncs `../res` → `vcv/res`, picking up the new `Lop.svg`.)

- [ ] **Step 2: Install into VCV Rack**

```bash
cp vcv/plugin.dylib vcv/plugin.json \
   "$HOME/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy/"
rsync -a --delete vcv/res/ \
   "$HOME/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy/res/"
```
Expected: both commands exit 0.

- [ ] **Step 3: Build the MetaModule plugin**

```bash
cd /Users/gabrielroth/Dev/RobotBoy
cmake --build metamodule/build -j8
```
Expected: builds with no errors. (If the build dir doesn't exist yet, configure first: `cmake -B metamodule/build -S metamodule -DCMAKE_TOOLCHAIN_FILE=~/Dev/metamodule-plugin-sdk/cmake/arm-none-eabi-gcc.cmake`.)

- [ ] **Step 4: Run the regression suite**

```bash
cd /Users/gabrielroth/Dev/RobotBoy/tests && ./run.sh
```
Expected: all C++ DSP tests and Python guard tests PASS, exit 0. (This change is panel-only, so behavior tests are unaffected — this confirms nothing regressed.)

- [ ] **Step 5: Produce the user GUI verification checklist**

Panel/GUI appearance is verified by the user, not by an agent-driven simulator. Present this checklist for the user to run in VCV Rack (and optionally the MetaModule simulator):

  1. Löp loads without error and the panel renders.
  2. Bottom row reads **In | Overdub | Out**; the Overdub button is centered between the stereo pairs, on the same line as the jacks, with "Overdub" labelled underneath.
  3. Row 2 is **Trig / Jump / Grid**, Row 3 is **Dry/Wet / Clear / Record**, and both look evenly spaced.
  4. Every control hit-tests where it's drawn (click each: Overdub, Grid, Trig, Jump, Dry/Wet, Clear, Record, In, Out).
  5. The Overdub button still cycles its mode and its LED colour updates (no behaviour change).
  6. The MetaModule faceplate matches the VCV panel (Overdub in the same spot; RgbLight overlay aligned).

- [ ] **Step 6: Finish the branch**

Once the user confirms the checklist, use `superpowers:finishing-a-development-branch` to decide integration (these commits are on `main` per Löp's history). No build artifacts to commit beyond Task 1–2.

---

## Self-Review

- **Spec coverage:** Overdub → audio row (Task 1 Step 1 + assert Step 4); row swap + even spacing / weights removed (Task 1 Step 1); label underneath (`label_side: below`, Task 1 Step 1); param enum untouched / no patch scramble (Global Constraints + Task 2 Step 2); full SVG → cpp → header → faceplate sync chain (Task 2); drift/sync passes (Task 2 Step 3); build + verify (Task 3). All spec sections map to a task.
- **Placeholder scan:** no TBD/TODO; every code/command step shows actual content and expected output.
- **Type consistency:** component ids (`OVERDUB_PARAM`, `AUDIO_L_INPUT`, …) are used identically across the SVG assert, the cpp sync regex, and the MM sync map; the widget stays `OverdubButton`; enum names are explicitly frozen.
- **Ambiguity:** the one structural choice (`type: buttons` vs. changing the `control:` rule) is resolved and justified in the Reference section.
