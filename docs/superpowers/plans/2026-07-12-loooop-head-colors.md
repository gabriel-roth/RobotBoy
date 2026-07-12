# Loooop Head Colors — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recolor Loooop's four playheads to Red / Yellow / Blue / Purple, driven by one canonical C++ table, with a test guarding the panel-spec against drift.

**Architecture:** A new Rack-free header `src/loooop/HeadColors.hpp` holds the one `{r,g,b,name}` table. `HEAD_COLORS` (display) and `kHeadNames` (menu) derive from it. The panel-spec YAML keeps its own tint list (it needs per-head opacity) and is regenerated into the SVG + MM PNG; a Python unittest asserts its hues match the table.

**Tech Stack:** C++20 (VCV + MetaModule), `panel_gen.py` (SVG gen), `SvgToPng.py` (MM faceplate), Python `unittest`.

## Global Constraints

- Palette (full-saturation RGB): Red `#FF3B30`, Yellow `#FFF70A`, Blue `#3F8CFF`, Purple `#BF5AF2`. Order = H1..H4 left to right.
- `NUM_HEADS` = `LoopEngine::NUM_HEADS` = 4.
- `HeadColors.hpp` MUST stay Rack-free (no `rack.hpp`) — the MetaModule core includes it transitively.
- No RGB literal or color name may be typed twice: `HEAD_COLORS` and `kHeadNames` reference `kHeadColors` by index.
- Do NOT touch: Löp, `kOverdubColors`, the `#ff0000`/`#00ff00`/`#0000ff` component-marker circles in the SVG, README/Loooop.md prose.
- Panel tint opacity stays per-head: yellow `#fff70a33` (→ 0.2), the rest default 0.14.

---

### Task 1: Canonical head-color table + drift-check test (TDD)

**Files:**
- Create: `src/loooop/HeadColors.hpp`
- Create: `tests/test_head_colors.py`

**Interfaces:**
- Produces: `loooop::kHeadColors[4]` of `struct HeadColor { uint8_t r,g,b; const char* name; }`.

- [ ] **Step 1: Write the drift-check test first**

Create `tests/test_head_colors.py`:

```python
import os, re, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def parse_table_hues():
    src = open(os.path.join(ROOT, "src/loooop/HeadColors.hpp")).read()
    body = src[src.index("kHeadColors"):]
    rows = re.findall(r"\{\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,", body)
    return ["%02x%02x%02x" % (int(r,16), int(g,16), int(b,16)) for r,g,b in rows]

def parse_spec_hues():
    spec = open(os.path.join(ROOT, "panel-specs/loooop.yaml")).read()
    line = next(l for l in spec.splitlines() if "tints:" in l and "#" in l)
    hexes = re.findall(r"#([0-9A-Fa-f]{6,8})", line)
    return [h[:6].lower() for h in hexes]   # strip any alpha suffix

class HeadColorSync(unittest.TestCase):
    def test_panel_tints_match_canonical_table(self):
        table = parse_table_hues()
        spec = parse_spec_hues()
        self.assertEqual(len(table), 4, f"expected 4 table colors, got {table}")
        self.assertEqual(table, spec,
            f"panel-specs/loooop.yaml tints {spec} drifted from kHeadColors {table}")

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it, expect failure (header does not exist yet)**

Run: `cd tests && python3 -m unittest test_head_colors -v`
Expected: FAIL — `FileNotFoundError` for `HeadColors.hpp` (or, once the header exists but the spec is unchanged, an `assertEqual` mismatch on H2/H4).

- [ ] **Step 3: Create the canonical table**

Create `src/loooop/HeadColors.hpp`:

```cpp
#pragma once
#include <cstdint>

namespace loooop {

// The one place to change a Loooop head color. RGB is the full-saturation
// display-lane color; `name` seeds the context-menu label ("<name> playhead").
// The panel tint hues (panel-specs/loooop.yaml) mirror these and are guarded by
// tests/test_head_colors.py; the panel keeps its own per-head tint opacity.
struct HeadColor { uint8_t r, g, b; const char* name; };

inline constexpr HeadColor kHeadColors[4] = {
    {0xFF, 0x3B, 0x30, "Red"},
    {0xFF, 0xF7, 0x0A, "Yellow"},
    {0x3F, 0x8C, 0xFF, "Blue"},
    {0xBF, 0x5A, 0xF2, "Purple"},
};

} // namespace loooop
```

- [ ] **Step 4: Update the panel spec so hues match (see Task 4 for the SVG regen)**

Edit `panel-specs/loooop.yaml`: `tints: ['#ff3b30', '#30d158', '#3f8cff', '#fff70a33']`
→ `tints: ['#ff3b30', '#fff70a33', '#3f8cff', '#bf5af2']`. Also update the nearby
comment that describes H4 as "gold/olive" / "match HEAD_COLORS[3]" to note H2 is
now the yellow tint and H4 is purple.

- [ ] **Step 5: Run the test, expect pass**

Run: `cd tests && python3 -m unittest test_head_colors -v`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/loooop/HeadColors.hpp tests/test_head_colors.py panel-specs/loooop.yaml
git commit -m "Loooop: canonical head-color table + panel-spec drift test"
```

---

### Task 2: Derive `HEAD_COLORS` from the table

**Files:**
- Modify: `src/loooop/display/LoopWaveformRenderer.hpp:20-28`

**Interfaces:**
- Consumes: `loooop::kHeadColors` (Task 1).
- Produces: `HEAD_COLORS` unchanged in type/shape (`uint8_t[NUM_HEADS][3]`), values now sourced from the table.

- [ ] **Step 1: Include the header and derive**

At the top of `LoopWaveformRenderer.hpp`, add `#include "../HeadColors.hpp"` next to the existing `#include "../dsp/LoopEngine.hpp"`. Replace the `HEAD_COLORS` block (and update its comment) with:

```cpp
    // Per-head lane/cluster colors — the single source is loooop::kHeadColors
    // (src/loooop/HeadColors.hpp): H1 red, H2 yellow, H3 blue, H4 purple. The
    // panel group tints (panel-specs/loooop.yaml) mirror these hues.
    static_assert(std::size(loooop::kHeadColors) == LoopEngine::NUM_HEADS,
                  "kHeadColors must have one entry per head");
    static constexpr uint8_t HEAD_COLORS[LoopEngine::NUM_HEADS][3] = {
        {loooop::kHeadColors[0].r, loooop::kHeadColors[0].g, loooop::kHeadColors[0].b},
        {loooop::kHeadColors[1].r, loooop::kHeadColors[1].g, loooop::kHeadColors[1].b},
        {loooop::kHeadColors[2].r, loooop::kHeadColors[2].g, loooop::kHeadColors[2].b},
        {loooop::kHeadColors[3].r, loooop::kHeadColors[3].g, loooop::kHeadColors[3].b},
    };
```

Add `#include <iterator>` (for `std::size`) if it is not already transitively available — verify at build (Task 6).

- [ ] **Step 2: Deferred** — compilation is verified by the full builds in Task 6.

---

### Task 3: Derive `kHeadNames` from the table

**Files:**
- Modify: `src/loooop/Loooop.cpp:11-14`

**Interfaces:**
- Consumes: `loooop::kHeadColors` (Task 1).

- [ ] **Step 1: Include and derive**

Ensure `Loooop.cpp` includes the table (add `#include "HeadColors.hpp"` with the other includes). Replace the `kHeadNames` definition + its comment:

```cpp
// Playhead display names follow the head colors on the panel/display
// (loooop::kHeadColors / LoopWaveformRenderer::HEAD_COLORS): H1 red, H2 yellow,
// H3 blue, H4 purple. Derived from the one color table so names can't drift.
static const std::string kHeadNames[LoopEngine::NUM_HEADS] = {
    std::string(loooop::kHeadColors[0].name) + " playhead",
    std::string(loooop::kHeadColors[1].name) + " playhead",
    std::string(loooop::kHeadColors[2].name) + " playhead",
    std::string(loooop::kHeadColors[3].name) + " playhead",
};
```

- [ ] **Step 2: Deferred** — verified by the VCV build in Task 6.

---

### Task 4: Regenerate SVG + MetaModule faceplate

**Files:**
- Modify (generated): `res/Loooop.svg`
- Modify (generated): `metamodule/assets/Loooop/Loooop.png`

- [ ] **Step 1: Regenerate the SVG from the (already-updated) spec**

```bash
source ~/Dev/python-scripts/.venv/bin/activate
python ~/Dev/vcv-panel-gen/panel_gen.py panel-specs/loooop.yaml --out res/Loooop.svg
```

- [ ] **Step 2: Verify the diff is color-only**

Run: `git diff --stat res/Loooop.svg` then inspect: `git diff res/Loooop.svg | grep -E '^[-+].*fill'`
Expected: only the two changed tint rects differ — H2 `#30d158`→`#fff70a` (and its `fill-opacity` 0.14→0.2) and H4 `#fff70a`→`#bf5af2` (0.2→0.14). If anything structural changed (positions, ids), STOP and reconcile — positions must not move.

- [ ] **Step 3: Regenerate the MetaModule faceplate PNG**

```bash
python3 /Users/gabrielroth/Dev/metamodule-plugin-sdk/scripts/SvgToPng.py \
    --input res/Loooop.svg --output metamodule/assets/ --layer panel
```

Verify it wrote `metamodule/assets/Loooop/Loooop.png` (the existing path). If the
tool emits a flat `metamodule/assets/Loooop.png` instead, move it into the
`Loooop/` subdir so the bundle path (`RobotBoy/Loooop/Loooop.png`) is preserved.
Confirm with `ls -l metamodule/assets/Loooop/Loooop.png` (mtime updated).

- [ ] **Step 4: Commit**

```bash
git add res/Loooop.svg metamodule/assets/Loooop/Loooop.png
git commit -m "Loooop: regenerate panel SVG + MM faceplate for new head tints"
```

---

### Task 5: Build, test, verify

- [ ] **Step 1: Run the test suite**

Run: `cd tests && ./run.sh`
Expected: all C++ tests pass and the python guard tests pass, including
`test_head_colors` (panel tints == table).

- [ ] **Step 2: Build VCV**

Run: `make -C vcv -j8`
Expected: builds clean to `plugin.dylib` (exit 0). This exercises `HEAD_COLORS`
(display) and `kHeadNames` (menu) on the VCV side.

- [ ] **Step 3: Build MetaModule**

Run: `cmake --build metamodule/build -j8`
Expected: `Built target plugin`, all symbols resolved. Exercises `HEAD_COLORS`
in the shared renderer on the MM core.

- [ ] **Step 4: Commit any build-only fixes** (e.g. an added `#include <iterator>`), if needed.

---

## User-run checklist (visual — not agent-run)

Load Loooop in VCV Rack and (optionally) the MetaModule simulator:

1. Panel tint zones read **Red, Yellow, Blue, Purple** left to right; the purple
   zone reads acceptably at 0.14 opacity on the dark panel (nudge the alpha in
   the spec + regenerate if it's too faint/strong).
2. The display lanes and window bars match those colors; a recorded loop shows
   yellow (H2) and purple (H4) where it used to show green and yellow.
3. Right-click → the playhead submenus read "Yellow playhead" and "Purple
   playhead" (no more Green).
4. **Retake `screenshots/Loooop.png`** for the README/docs.

## Self-Review

- Spec coverage: table (T1), HEAD_COLORS (T2), kHeadNames (T3), panel spec+SVG+PNG (T1 step4 + T4), drift test (T1), builds (T5). Covered.
- Type consistency: `loooop::kHeadColors` / `HeadColor{r,g,b,name}` used identically in T1–T3. `HEAD_COLORS` keeps its `uint8_t[NUM_HEADS][3]` type so the renderer interface is unchanged.
- No placeholders; every code/edit step shows the code.
