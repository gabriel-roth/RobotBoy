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
    {0xFF, 0x5A, 0xF0, "Purple"},
};

} // namespace loooop
