#include "LoopWaveformRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace {
void vline(uint32_t* buf, int width, int height, int x, int y0, int y1, uint32_t c) {
    if (x < 0 || x >= width) return;
    y0 = std::max(y0, 0);
    y1 = std::min(y1, height - 1);
    for (int y = y0; y <= y1; ++y)
        buf[y * width + x] = c;
}

// Vertical bars at a grid's interior segment boundaries, across the full
// region height. Callers pick the z-order: over the waveform (slicing it into
// chunks), under the lane bars (so head markers stay prominent).
void drawGridBars(uint32_t* buf, int width, int height, unsigned grid,
                  LoopWaveformRenderer::PackFn pack) {
    const uint32_t c = pack(LoopWaveformRenderer::GRID[0],
                            LoopWaveformRenderer::GRID[1],
                            LoopWaveformRenderer::GRID[2], 0xFF);
    const int bw = std::max(1, width / 300);
    for (unsigned k = 1; k < grid; ++k) {
        const int x = int(std::uint64_t(k) * unsigned(width) / grid);
        for (int dx = 0; dx < bw; ++dx)
            vline(buf, width, height, x + dx, 0, height - 1, c);
    }
}
} // namespace

void LoopWaveformRenderer::renderWaveform(uint32_t* buf, int width, int height,
                                          const LoopEngine& engine, PackFn pack) {
    if (width <= 0 || height <= 0) return;
    const uint32_t bg = pack(BG[0], BG[1], BG[2], 0xFF);
    std::fill(buf, buf + std::size_t(width) * height, bg);

    const auto s = engine.displaySnapshot();
    // X-axis: the frozen loop, or the live recording so far (spec: the wave
    // fills the width and compresses as the initial record grows).
    const uint64_t axisLen = (s.loopLen > 0) ? s.loopLen : (s.recording ? s.recordedLen : 0);
    if (axisLen == 0) return;

    const int waveH = height;

    if (waveH > 1) {
        // Stereo waveform: L band over R band, each channel min/max around
        // its own midline, in a neutral dim tone so the saturated head lanes
        // stand out. Below MIN_SPLIT_ROWS the region draws one combined L∪R
        // envelope instead.
        const uint32_t wave = pack(WAVE[0], WAVE[1], WAVE[2], 0xFF);
        const float* sampL = engine.sampleData(0);
        const float* sampR = engine.sampleData(1);

        // Level-aware height (same dB-fullness logic as before), from one
        // shared peak across both channels so the bands keep their relative
        // levels — a loop louder on the left draws taller on the left.
        float peak = 0.f;
        for (uint64_t i = 0; i < axisLen; ++i) {
            peak = std::max(peak, std::abs(sampL[i]));
            peak = std::max(peak, std::abs(sampR[i]));
        }
        float fullness = 0.f;
        if (peak > 1e-6f) {
            const float db = 20.f * std::log10(peak / LEVEL_REF);
            const float t = std::clamp((db - LEVEL_DB_FLOOR) / (0.f - LEVEL_DB_FLOOR), 0.f, 1.f);
            fullness = LEVEL_FLOOR + (HEADROOM - LEVEL_FLOOR) * t;
        }

        // One channel's band; a non-null second sample pointer widens each
        // column to the union of both channels (tiny-display fallback).
        auto drawBand = [&](const float* samp, const float* samp2,
                            int bandTop, int bandH) {
            const float midY = bandTop + (bandH - 1) * 0.5f;
            const float yScale = (peak > 1e-6f)
                ? (bandH - 1) * 0.5f * fullness / peak : 0.f;
            for (int x = 0; x < width; ++x) {
                const uint64_t s0 = uint64_t(x) * axisLen / width;
                uint64_t s1 = uint64_t(x + 1) * axisLen / width;
                if (s1 <= s0) s1 = s0 + 1;
                float lo = samp[s0], hi = samp[s0];
                for (uint64_t i = s0 + 1; i < s1; ++i) {
                    lo = std::min(lo, samp[i]);
                    hi = std::max(hi, samp[i]);
                }
                if (samp2) {
                    for (uint64_t i = s0; i < s1; ++i) {
                        lo = std::min(lo, samp2[i]);
                        hi = std::max(hi, samp2[i]);
                    }
                }
                int y0 = int(std::lround(midY - hi * yScale));
                int y1 = int(std::lround(midY - lo * yScale));
                y0 = std::max(y0, bandTop);
                y1 = std::min(y1, bandTop + bandH - 1);
                vline(buf, width, height, x, y0, y1, wave);
            }
        };

        if (waveH >= MIN_SPLIT_ROWS) {
            const int bandH = waveH / 2;      // odd waveH leaves a 1-row gap between bands
            drawBand(sampL, nullptr, 0, bandH);
            drawBand(sampR, nullptr, waveH - bandH, bandH);
        } else {
            drawBand(sampL, sampR, 0, waveH);
        }
    }

    // Grid bars slice through the waveform. Frozen loops only — a growing
    // initial recording has no meaningful divisions yet.
    if (s.grid >= 2 && s.loopLen > 0)
        drawGridBars(buf, width, height, s.grid, pack);
}

void LoopWaveformRenderer::renderLanes(uint32_t* buf, int width, int height,
                                       int laneH, const LoopEngine& engine, PackFn pack) {
    if (width <= 0 || height <= 0 || laneH <= 0) return;
    const uint32_t bg = pack(BG[0], BG[1], BG[2], 0xFF);
    std::fill(buf, buf + std::size_t(width) * height, bg);
    const auto s = engine.displaySnapshot();
    if (s.loopLen == 0) return;

    if (s.grid >= 2)
        drawGridBars(buf, width, height, s.grid, pack);

    const int nHeads = engine.numHeads();
    const int lanesTop = 0;
    const int hw = std::max(2, width / 90);   // playhead bar width
    for (int i = 0; i < nHeads; ++i) {
        const uint8_t* c = HEAD_COLORS[i];
        const uint32_t dimC = pack(uint8_t(int(c[0]) * DIM_NUM / DIM_DEN),
                                   uint8_t(int(c[1]) * DIM_NUM / DIM_DEN),
                                   uint8_t(int(c[2]) * DIM_NUM / DIM_DEN), 0xFF);
        const uint32_t brightC = pack(c[0], c[1], c[2], 0xFF);
        const uint32_t armedC = pack(uint8_t(int(c[0]) * ARMED_NUM / ARMED_DEN),
                                     uint8_t(int(c[1]) * ARMED_NUM / ARMED_DEN),
                                     uint8_t(int(c[2]) * ARMED_NUM / ARMED_DEN), 0xFF);
        // Non-playing (armed/finished one-shot) lanes draw asleep: every
        // element one dim level down, so a silent head reads as waiting
        // for its trigger rather than broken.
        const uint32_t winC  = s.playing[i] ? dimC : armedC;
        const uint32_t headC = s.playing[i] ? brightC : dimC;
        const int top = lanesTop + i * laneH;
        const int bot = top + laneH - 2;      // bottom row of each lane stays bg (gap)
        // Sub-loop window extent: dimmed bar from start to end.
        const int xs = int(std::lround(s.winStart01[i] * (width - 1)));
        const int xe = int(std::lround(s.winEnd01[i] * (width - 1)));
        for (int x = xs; x <= xe; ++x)
            vline(buf, width, height, x, top, bot, winC);
        // Playhead: bright bar on top of the window bar.
        const int hx = int(std::lround(s.headPos01[i] * (width - 1)));
        for (int dx = 0; dx < hw; ++dx)
            vline(buf, width, height, hx - hw / 2 + dx, top, bot, headC);
    }
}

void LoopWaveformRenderer::render(uint32_t* buf, int width, int height,
                                  const LoopEngine& engine, PackFn pack) {
    if (width <= 0 || height <= 0) return;
    const auto g = geometry(height, engine.numHeads());
    renderWaveform(buf, width, g.waveHeight, engine, pack);
    renderLanes(buf + std::size_t(g.waveHeight) * width,
                width, g.lanesHeight, g.laneHeight, engine, pack);
}
