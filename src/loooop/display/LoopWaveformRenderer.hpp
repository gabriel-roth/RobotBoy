#pragma once
#include "../dsp/LoopEngine.hpp"
#include "../HeadColors.hpp"
#include <cstdint>
#include <iterator>

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
    static constexpr uint8_t GRID[3] = {0x2E, 0x3A, 0x46};   // segment bars: above BG, below WAVE
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
    // Löp's single lane draws purple — a color no Loooop head uses, so the
    // two modules' displays can't be mistaken for each other — and at twice
    // Loooop's lane height (height/4 vs height/8), the waveform giving up
    // the difference. Both platforms (VCV widget, MM core) pass these.
    static constexpr uint8_t LOP_LANE_COLOR[1][3] = {{0xBF, 0x5A, 0xF2}};
    static constexpr int LANE_DIV = 8;
    static constexpr int LOP_LANE_DIV = 4;
    // Window-extent bars use the head color dimmed by DIM_NUM/DIM_DEN.
    static constexpr int DIM_NUM = 2, DIM_DEN = 5;
    // A non-playing head (one-shot armed or finished, awaiting a trigger)
    // draws asleep: window bar dimmed to ARMED_NUM/ARMED_DEN, playhead
    // marker at the window-dim level instead of bright.
    static constexpr int ARMED_NUM = 1, ARMED_DEN = 5;
    // Preferred lane band height (height/laneDiv). Use geometry() to cap the
    // complete lane region to the actual destination height.
    static constexpr int laneHeight(int height, int laneDiv = LANE_DIV) {
        return height / laneDiv < 3 ? 3 : height / laneDiv;
    }
    struct Geometry {
        int laneHeight;
        int lanesHeight;
        int waveHeight;
    };
    static constexpr Geometry geometry(int height, int numHeads,
                                       int laneDiv = LANE_DIV) {
        if (height <= 0 || numHeads <= 0) return {0, 0, 0};
        const int laneH = laneHeight(height, laneDiv) < height / numHeads
            ? laneHeight(height, laneDiv) : height / numHeads;
        const int lanesH = numHeads * laneH;
        return {laneH, lanesH, height - lanesH};
    }

    // Wave regions shorter than this draw one combined L∪R envelope instead
    // of split stereo bands (a sub-4-row band is illegible).
    static constexpr int MIN_SPLIT_ROWS = 8;

    // VCV offscreen-texture width bound. The waveform re-render is cached
    // (once per waveformRevision change), and its scan is O(loopLen) regardless
    // of render width, so this caps texture memory / paint cost, not the scan.
    // VCV applies it (nvgImagePattern stretches the capped texture to panel
    // width — imperceptible). MetaModule renders at native framebuffer width,
    // far below this, so it does not bind there. We can lower this freely if MM
    // ever needs it; below MM's native width it has no effect without an added
    // upscale path, and the O(loopLen) scan is the real MM cost.
    static constexpr int WAVE_WIDTH_CAP = 1024;
    static constexpr int cappedWidth(int w) { return w < WAVE_WIDTH_CAP ? w : WAVE_WIDTH_CAP; }

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
    static void renderWaveform(uint32_t* buf, int width, int height,
                               const LoopEngine& engine, PackFn pack);
    static void renderLanes(uint32_t* buf, int width, int height, int laneHeight,
                            const LoopEngine& engine, PackFn pack,
                            const uint8_t (*headColors)[3] = HEAD_COLORS);
};
