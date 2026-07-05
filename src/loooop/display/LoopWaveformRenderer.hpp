#pragma once
#include "../dsp/LoopEngine.hpp"
#include <cstdint>

// Rasterizes the loop display — a stereo waveform on top (left channel over
// right, each min/max around its own band midline), with four color-coded
// per-head lanes stacked below — into a caller-provided 32-bit pixel buffer.
// Wave regions shorter than MIN_SPLIT_ROWS fall back to a single combined
// L∪R envelope. Platform-agnostic: the pixel byte order is supplied by the
// caller as a pack function (MetaModule uses ARGB words, VCV/nanovg uses
// RGBA bytes). This class is the single source of truth for the display's
// appearance on both platforms.
class LoopWaveformRenderer {
public:
    using PackFn = uint32_t (*)(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    // Colors, {r, g, b}
    static constexpr uint8_t BG[3]   = {0x10, 0x14, 0x18};   // near-black blue
    static constexpr uint8_t WAVE[3] = {0x51, 0x66, 0x7A};   // muted blue-gray (neutral)
    // Per-head lane/cluster colors — must match the panel group tints
    // (vcv/panel-spec.yaml `groups.tints`): H1 red, H2 green, H3 blue, H4 yellow.
    static constexpr uint8_t HEAD_COLORS[LoopEngine::NUM_HEADS][3] = {
        {0xFF, 0x3B, 0x30},
        {0x30, 0xD1, 0x58},
        {0x3F, 0x8C, 0xFF},
        {0xFF, 0xF7, 0x0A},
    };
    // Window-extent bars use the head color dimmed by DIM_NUM/DIM_DEN.
    static constexpr int DIM_NUM = 2, DIM_DEN = 5;
    // Lane band height in rows; four lanes stack at the bottom of the display.
    static constexpr int laneHeight(int height) {
        return height / 8 < 3 ? 3 : height / 8;
    }

    // Wave regions shorter than this draw one combined L∪R envelope instead
    // of split stereo bands (a sub-4-row band is illegible).
    static constexpr int MIN_SPLIT_ROWS = 8;

    // Level-aware height: the shape is normalized to the loop's own peak (so
    // detail stays visible at any level), but the overall height is scaled by a
    // "fullness" derived from the loop's absolute peak on a dB scale — loud
    // loops fill to HEADROOM, quiet ones draw shorter, down to LEVEL_FLOOR so a
    // faint (but non-silent) loop never vanishes. 0 dB / full is the ±10 V
    // signal ceiling shared by VCV and the MetaModule SDK; since the engine sees
    // volts/5, that ceiling is a peak of LEVEL_REF (2.0). Nominal ±5 V audio
    // (peak 1.0) sits ~6 dB below full. Nothing clips — hotter caps at HEADROOM.
    static constexpr float HEADROOM      = 0.85f;    // fullness at 0 dB (peak == LEVEL_REF) and above
    static constexpr float LEVEL_REF     = 2.0f;     // peak that reads as 0 dB / full (±10 V, volts/5)
    static constexpr float LEVEL_FLOOR   = 0.20f;    // min fullness for a faint, non-silent loop
    static constexpr float LEVEL_DB_FLOOR = -48.0f;  // peak at/below this dB below LEVEL_REF draws at LEVEL_FLOOR

    static void render(uint32_t* buf, int width, int height,
                       const LoopEngine& engine, PackFn pack);
};
