# Löp Grey-Zone Shrink Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline) or superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Shrink the Löp grey control zone so it backs only the top two rows (Size/Pos/Speed/Jitter and Trig/Jump/Grid), shift Row 3 (Dry/Wet/Clear/Record) down to close the gap to the audio row, and re-sync all consumers.

**Architecture:** Panel is generated from `panel-specs/lop.yaml` by `~/Dev/vcv-panel-gen`; regenerated `res/Lop.svg` is the source of truth for control centers, which then sync into `src/loooop/Lop.cpp`, `metamodule/loooop/Lop_info.hh`, and the MetaModule faceplate PNG. The zone is a faceplate rect (art only) with no control to sync.

**Tech Stack:** Python panel generator, C++ (VCV + MetaModule), Inkscape, `mm_sync.py`.

## Global Constraints

- Geometry only: NO param/enum/widget changes; NO change to Row 1, Row 2, or the audio-row *contents*, or the screen.
- Zone top edge stays `y: 35.15`. Zone bottom edge encloses the Grid value ring (~3 mm below it) and stays clear of Row 3's labels. Zone backs Row 1 + Row 2 only.
- Audio row stays at cy **116.05** (Loooop bottom-jack alignment). If the tuned Row 3 shift is ~3.1 mm, the audio row lands there naturally and the three `[0, 3.10]` nudges are removed; if the tuned shift differs, re-derive the audio nudge to keep cy 116.05 (never leave it drifted).
- `res/Lop.svg` is generated — never hand-edited. `Lop_info.hh` names/order/structs are a hand-maintained contract; only coordinates sync.
- Build/install per repo convention (no `build-install.sh`): `make -C vcv -j8`, copy dylib/json, rsync `vcv/res/`. Commit messages ≤15 words, no AI attribution. GUI verification is a user-run checklist.

---

### Task 1: Zone + Row 3 shift + SVG regen + render sign-off

**Files:** Modify `panel-specs/lop.yaml`; regenerate `res/Lop.svg`.

- [ ] **Step 1: Edit `panel-specs/lop.yaml`**
  - Zone (line ~39-40): change height to a starting `h: 48` → `{x: 1.5, y: 35.15, w: 57.96, h: 48}`.
  - Row 3 `params` row (`gap_above: 2.0` at line ~84): change to `gap_above: 5.1` (shift Row 3 down ~3.1 mm).
  - Remove the three `nudges:` entries (`AUDIO_L_INPUT`, `OUT_L_OUTPUT`, `OVERDUB_PARAM: [0, 3.10]`) and the now-empty `nudges:` key.
  - Update the zone comment (line ~36-38) to say the rect backs the top two rows only, and the vertical-air comment (line ~41-47) to describe Row 3 shifted down with the audio row landing at cy 116.05 with no nudge.

- [ ] **Step 2: Regenerate the SVG**
```bash
cd /Users/gabrielroth/Dev/RobotBoy
~/Dev/vcv-panel-gen/.venv/bin/python ~/Dev/vcv-panel-gen/panel_gen.py \
    panel-specs/lop.yaml --out res/Lop.svg
```
Expected: exits 0, no LayoutError.

- [ ] **Step 3: Verify geometry**
```bash
cd /Users/gabrielroth/Dev/RobotBoy
python3 - <<'PY'
import xml.etree.ElementTree as ET
r=ET.parse('res/Lop.svg').getroot()
c={}; zone=None
for n in r.iter():
    tag=n.tag.split('}')[-1]; i=n.get('id','').split('#')[0]
    if n.get('cx') is not None: c[i]=(float(n.get('cx')),float(n.get('cy')))
    if tag=='rect' and n.get('id','')=='' and n.get('width')=='57.96':
        zone=(float(n.get('y')),float(n.get('height')))
print('zone y,h:', zone, '-> bottom', round(zone[0]+zone[1],2) if zone else None)
print('Grid', c.get('GRID_PARAM'), 'DryWet', c.get('DRYWET_PARAM'), 'Audio-Overdub', c.get('OVERDUB_PARAM'))
assert abs(c['OVERDUB_PARAM'][1]-116.05)<0.05, 'audio row not at 116.05'
print('OK: audio at 116.05')
PY
```
Expected: zone bottom between the Grid value ring and Row 3 (~83); Dry/Wet cy > 90.85 (shifted down); `OK: audio at 116.05`. If the zone rect can't be located by the `width==57.96` heuristic, read it from the diff instead.

- [ ] **Step 4: Show render and tune**
```bash
open -a Safari /Users/gabrielroth/Dev/RobotBoy/res/Lop.svg
```
User reviews: rect bottom ~3 mm below the Grid value ring, clear of Row 3 labels; Row 3 lower with a tighter gap to the audio row. Adjust zone `h` and Row 3 `gap_above` and rerun Steps 2–3 until the user signs off. If the tuned Row 3 shift is not ~3.1 mm, re-add an audio nudge `[0, dy]` (dy = 116.05 − audio natural cy) so the audio row stays at 116.05. **Do not proceed until the user signs off.**

- [ ] **Step 5: Commit** (stage only these two files — the working tree has unrelated changes)
```bash
cd /Users/gabrielroth/Dev/RobotBoy
git add panel-specs/lop.yaml res/Lop.svg
git commit -m "Löp: shrink grey zone to top two rows, shift Row 3 down"
```

---

### Task 2: Position sync + build + verify

**Files:** Modify `src/loooop/Lop.cpp`, `metamodule/loooop/Lop_info.hh`, `metamodule/assets/Loooop/Lop.png`.

- [ ] **Step 1: Re-sync VCV control coords in `src/loooop/Lop.cpp`** (same one-shot helper as the Overdub relocate — rewrites `mm2px(Vec)` lines from the SVG by enum name, only when changed)
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
    s=f"{v:.3f}".rstrip('0').rstrip('.'); return s if s else "0"
pat=re.compile(r'(mm2px\(Vec\()(-?\d+\.?\d*),\s*(-?\d+\.?\d*)(\))(.*?Lop::)([A-Z0-9_]+)')
path='src/loooop/Lop.cpp'; out=[]; changed=0
for line in open(path).read().splitlines():
    m=pat.search(line)
    if m and m.group(6) in centers:
        nx,ny=centers[m.group(6)]; ox,oy=float(m.group(2)),float(m.group(3))
        if round(ox,3)!=round(nx,3) or round(oy,3)!=round(ny,3):
            line=line[:m.start()]+f"{m.group(1)}{fmt(nx)}, {fmt(ny)}{m.group(4)}{m.group(5)}{m.group(6)}"+line[m.end():]; changed+=1
    out.append(line)
open(path,'w').write("\n".join(out)+"\n")
print(f"rewrote {changed} coordinate line(s)")
PY
```
Expected: `rewrote N coordinate line(s)` (N = the Row 3 controls, plus audio if the nudge changed).

- [ ] **Step 2: Review the diff**
```bash
cd /Users/gabrielroth/Dev/RobotBoy && git diff src/loooop/Lop.cpp
```
Expected: only `mm2px(Vec(...))` coordinates change (Row 3 controls shifted down; Row 1/Row 2 unchanged); enums and widget types unchanged.

- [ ] **Step 3: Sync MetaModule header + display rect**
```bash
cd /Users/gabrielroth/Dev/RobotBoy
python3 metamodule/loooop/sync_info_positions.py
```
Expected: `Lop: ...` sync lines + `Lop: VCV display rect synced`, exit 0. Loooop untouched.

- [ ] **Step 4: Regenerate the MetaModule faceplate PNG**
```bash
cd /Users/gabrielroth/Dev/RobotBoy
python3 ~/Dev/metamodule-plugin-sdk/scripts/SvgToPng.py \
    --input res/Lop.svg --output metamodule/assets/Loooop/ --layer panel
```
Expected: writes `metamodule/assets/Loooop/Lop.png`.

- [ ] **Step 5: Build both + run the suite**
```bash
cd /Users/gabrielroth/Dev/RobotBoy
make -C vcv -j8
cp vcv/plugin.dylib vcv/plugin.json "$HOME/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy/"
rsync -a --delete vcv/res/ "$HOME/Library/Application Support/Rack2/plugins-mac-arm64/RobotBoy/res/"
cmake --build metamodule/build -j8
cd tests && ./run.sh
```
Expected: both builds compile with no errors; `run.sh` all PASS, exit 0.

- [ ] **Step 6: Commit**
```bash
cd /Users/gabrielroth/Dev/RobotBoy
git add src/loooop/Lop.cpp metamodule/loooop/Lop_info.hh metamodule/assets/Loooop/Lop.png
git commit -m "Löp: sync VCV/MetaModule positions for zone shrink"
```

- [ ] **Step 7: GUI checklist** — hand the user: panel renders; grey rect backs only the top two rows with its bottom edge ~3 mm below the Grid value ring; Row 3 lower with a tighter gap to the audio row; all controls hit-test; no behaviour change; MetaModule faceplate matches.

---

## Self-Review
- **Coverage:** zone shrink (T1 S1/S3), Row 3 shift + nudge handling (T1 S1/S4, constraints), enclose-ring bottom edge (T1 S1/S4), audio at 116.05 (T1 S3, constraints), full sync chain (T2), build + tests + checklist (T2). All spec sections map to a task.
- **Placeholders:** none — commands and expected outputs are concrete; the only intentionally-tuned values (zone `h`, Row 3 `gap_above`) are gated by the render-review loop with explicit start values.
- **Consistency:** the Lop.cpp sync helper and enum-name matching are identical to the proven Overdub-relocate task; component ids unchanged.
