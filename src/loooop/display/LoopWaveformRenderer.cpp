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
        const auto& minsL = engine.peakMins(0);
        const auto& maxsL = engine.peakMaxs(0);
        const auto& minsR = engine.peakMins(1);
        const auto& maxsR = engine.peakMaxs(1);
        const uint64_t binSize = engine.peakBinSize();

        // Level-aware height (same dB-fullness logic as before), from one
        // shared peak across both channels so the bands keep their relative
        // levels — a loop louder on the left draws taller on the left.
        const std::size_t lastBin =
            std::min(std::size_t((axisLen - 1) / binSize), std::size_t(LoopEngine::PEAK_BINS - 1));
        float peak = 0.f;
        for (std::size_t b = 0; b <= lastBin; ++b) {
            peak = std::max(peak, std::max(std::abs(minsL[b]), std::abs(maxsL[b])));
            peak = std::max(peak, std::max(std::abs(minsR[b]), std::abs(maxsR[b])));
        }
        float fullness = 0.f;
        if (peak > 1e-6f) {
            const float db = 20.f * std::log10(peak / LEVEL_REF);
            const float t = std::clamp((db - LEVEL_DB_FLOOR) / (0.f - LEVEL_DB_FLOOR), 0.f, 1.f);
            fullness = LEVEL_FLOOR + (HEADROOM - LEVEL_FLOOR) * t;
        }

        // One channel's band; a non-null second min/max pair widens each
        // column to the union of both channels (tiny-display fallback).
        auto drawBand = [&](const float* mins, const float* maxs,
                            const float* mins2, const float* maxs2,
                            int bandTop, int bandH) {
            const float midY = bandTop + (bandH - 1) * 0.5f;
            const float yScale = (peak > 1e-6f)
                ? (bandH - 1) * 0.5f * fullness / peak : 0.f;
            for (int x = 0; x < width; ++x) {
                const uint64_t s0 = uint64_t(x) * axisLen / width;
                uint64_t s1 = uint64_t(x + 1) * axisLen / width;
                if (s1 <= s0) s1 = s0 + 1;
                auto b0 = std::size_t(s0 / binSize);
                auto b1 = std::size_t((s1 - 1) / binSize);
                b0 = std::min(b0, std::size_t(LoopEngine::PEAK_BINS - 1));
                b1 = std::min(b1, std::size_t(LoopEngine::PEAK_BINS - 1));
                float lo = mins[b0], hi = maxs[b0];
                for (std::size_t b = b0 + 1; b <= b1; ++b) {
                    lo = std::min(lo, mins[b]);
                    hi = std::max(hi, maxs[b]);
                }
                if (mins2) {
                    for (std::size_t b = b0; b <= b1; ++b) {
                        lo = std::min(lo, mins2[b]);
                        hi = std::max(hi, maxs2[b]);
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
            drawBand(minsL.data(), maxsL.data(), nullptr, nullptr, 0, bandH);
            drawBand(minsR.data(), maxsR.data(), nullptr, nullptr, waveH - bandH, bandH);
        } else {
            drawBand(minsL.data(), maxsL.data(), minsR.data(), maxsR.data(), 0, waveH);
        }
    }

}

void LoopWaveformRenderer::renderLanes(uint32_t* buf, int width, int height,
                                       int laneH, const LoopEngine& engine, PackFn pack) {
    if (width <= 0 || height <= 0 || laneH <= 0) return;
    const uint32_t bg = pack(BG[0], BG[1], BG[2], 0xFF);
    std::fill(buf, buf + std::size_t(width) * height, bg);
    const auto s = engine.displaySnapshot();
    if (s.loopLen == 0) return;

    const int nHeads = engine.numHeads();
    const int lanesTop = 0;
    const int hw = std::max(2, width / 90);   // playhead bar width
    for (int i = 0; i < nHeads; ++i) {
        const uint8_t* c = HEAD_COLORS[i];
        const uint32_t dimC = pack(uint8_t(int(c[0]) * DIM_NUM / DIM_DEN),
                                   uint8_t(int(c[1]) * DIM_NUM / DIM_DEN),
                                   uint8_t(int(c[2]) * DIM_NUM / DIM_DEN), 0xFF);
        const uint32_t brightC = pack(c[0], c[1], c[2], 0xFF);
        const int top = lanesTop + i * laneH;
        const int bot = top + laneH - 2;      // bottom row of each lane stays bg (gap)
        // Sub-loop window extent: dimmed bar from start to end.
        const int xs = int(std::lround(s.winStart01[i] * (width - 1)));
        const int xe = int(std::lround(s.winEnd01[i] * (width - 1)));
        for (int x = xs; x <= xe; ++x)
            vline(buf, width, height, x, top, bot, dimC);
        // Playhead: bright bar on top of the window bar.
        const int hx = int(std::lround(s.headPos01[i] * (width - 1)));
        for (int dx = 0; dx < hw; ++dx)
            vline(buf, width, height, hx - hw / 2 + dx, top, bot, brightC);
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
