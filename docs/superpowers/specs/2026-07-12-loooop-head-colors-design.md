# Loooop head colors → Red / Yellow / Blue / Purple, with a single source of truth

**Date:** 2026-07-12
**Module:** Loooop (VCV Rack + MetaModule). Löp is out of scope.

## Goal

Recolor Loooop's four playheads to match the MetaModule-style palette, left to
right: **Red, Yellow, Blue, Purple**. At the same time, collapse the duplicated
color definitions into **one canonical table** so a future color change is a
single edit, guarded by a test.

## Color change

Palette "B" (the Apple-style palette this plugin already uses on MetaModule).
Full-saturation RGB is the display-lane color; the panel tint is a translucent
wash of the same hue.

| Head | Current | New | Changed? |
|------|---------|-----|----------|
| H1 | Red `#FF3B30` | Red `#FF3B30` | no |
| H2 | Green `#30D158` | **Yellow `#FFF70A`** | yes |
| H3 | Blue `#3F8CFF` | Blue `#3F8CFF` | no |
| H4 | Yellow `#FFF70A` | **Purple `#BF5AF2`** | yes |

**Explicitly unchanged:** Löp's lane (stays purple `#BF5AF2` — it is still
distinguishable from Loooop by lane count and height); the Overdub mode colors
(`kOverdubColors` — Layer/Decay/Add/Replace/Lock); the `#ff0000`/`#00ff00`/
`#0000ff` circles in `res/Loooop.svg` (those are component-placement markers
keyed by id, e.g. `id="SIZE1_PARAM#RoundBlackKnob"`, not head colors).

## Where head colors live today (the duplication)

1. **`src/loooop/display/LoopWaveformRenderer.hpp`** — `HEAD_COLORS[NUM_HEADS][3]`
   (`NUM_HEADS` = 4). Drives the display lanes, window bars, and cluster markers
   on **both** VCV (widget) and MetaModule (core), since the renderer is
   Rack-free and shared.
2. **`src/loooop/Loooop.cpp`** — `kHeadNames[NUM_HEADS]` (`"Red playhead"`, …),
   the color names shown in the VCV context-menu playhead submenus.
3. **`panel-specs/loooop.yaml`** — `tints:` list, consumed by
   `~/Dev/vcv-panel-gen/panel_gen.py` to generate `res/Loooop.svg` (the panel
   tint rectangles). The generated SVG is also the source for the MetaModule
   faceplate PNG.

(1) and (2) are compiled C++. (3) is a YAML file consumed by a Python generator.
There are cross-referencing "must match" comments between (1) and (3) today.

## Design

### Single canonical table (new)

New Rack-free header **`src/loooop/HeadColors.hpp`**:

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

This header must stay Rack-free (no `rack.hpp`) so the MetaModule core and the
Rack-free `LoopWaveformRenderer` can include it. `4` matches
`LoopEngine::NUM_HEADS`; a `static_assert` in the renderer ties them together.

### Derive `HEAD_COLORS` from the table

`LoopWaveformRenderer.hpp` includes `HeadColors.hpp` and builds `HEAD_COLORS`
from `kHeadColors` so no RGB literal is retyped. The renderer's public interface
(`const uint8_t (*headColors)[3]`, defaulting to `HEAD_COLORS`, overridable by
`LOP_LANE_COLOR`) is unchanged — only how `HEAD_COLORS` is populated changes.
`static_assert(std::size(kHeadColors) == NUM_HEADS)`. The old
"H1 red, H2 green, …" comment is updated to the new palette.

### Derive `kHeadNames` from the table

`Loooop.cpp` builds each menu label as `std::string(kHeadColors[i].name) + " playhead"`
instead of hardcoding the four strings. The old "H1 red … H4 yellow" comment is
updated. Behavior is identical except the two changed names ("Yellow playhead",
"Purple playhead").

### Panel spec + generated artifacts

- `panel-specs/loooop.yaml` `tints:` → `['#ff3b30', '#fff70a33', '#3f8cff', '#bf5af2']`.
  Rationale: the `33` alpha suffix (→ `fill-opacity` 0.2, the "read as gold on the
  dark panel" tuning) moves with the yellow to H2; H4 purple takes the default
  0.14 like red/blue. Update the H4-specific "gold/olive" comment in the spec.
- Regenerate `res/Loooop.svg` from the spec via `panel_gen.py`. The diff must be
  **color-only** (tint `fill`/`fill-opacity`); verify before committing.
- Regenerate the MetaModule faceplate `metamodule/assets/Loooop/Loooop.png` from
  the new SVG (same SVG→PNG step the plugin already uses), so MetaModule shows
  the new tints.
- Positions do not change, so `sync_info_positions.py` is **not** required.

### Drift-check test (new)

**`tests/test_head_colors.py`** (Python, run by the existing `tests/` harness):

1. Parse `panel-specs/loooop.yaml` `tints` → 4 hex strings; strip any 8th/7th–8th
   alpha nibble to get the 6-digit hue.
2. Parse `src/loooop/HeadColors.hpp` `kHeadColors` → 4 `(r,g,b)` triples → hex hues.
3. Assert the two ordered lists of hues are equal.

This fails the build if the panel spec drifts from the canonical table. (It does
not check opacity — that is a panel-only concern — nor the `name` field, which
has no second copy to drift against.)

## Out of scope

- Löp colors, Löp panel, Löp docs.
- Overdub mode colors (`kOverdubColors`).
- README.md / Loooop.md language — verified to name no specific head colors
  (Loooop.md line 24 says "colored playhead markers"; the color words at lines
  60–64 are Overdub modes). Only the screenshot changes.
- `screenshots/Loooop.png` — the **user** will retake it after the rebuild.

## Verification

- `make -C vcv -j8` builds clean (VCV widget uses `HEAD_COLORS`/`kHeadNames`).
- `cmake --build metamodule/build -j8` builds clean (MM core uses `HEAD_COLORS`).
- The `tests/` suite passes, including the new `test_head_colors.py`.
- Regenerated `res/Loooop.svg` diff is color-only.
- Manual (user, in sim/Rack): the four heads read Red/Yellow/Blue/Purple on the
  panel tints, the display lanes, and the menu labels; the purple panel tint
  reads acceptably at 0.14 opacity (nudge if needed, per `picking-panel-rect-colors`).
