#pragma once
#include "plugin.hpp"
#include "dsp/LoopEngine.hpp"
#include "display/LoopWaveformRenderer.hpp"
#include <cmath>
#include <vector>

// nanovg wants R,G,B,A bytes in memory: on little-endian that is the word
// a<<24 | b<<16 | g<<8 | r.
inline uint32_t loopDisplayPackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
}

struct LoopDisplayWidget : Widget {
    const LoopEngine* engine = nullptr;          // module engine; nullptr in browser
    int demoHeads = LoopEngine::NUM_HEADS;       // browser-preview head count
    static constexpr int kOversample = 2;
    std::vector<uint32_t> wavePix, lanePix;
    int waveImg = -1, laneImg = -1;
    std::uint32_t cachedWaveRevision = UINT32_MAX;
    std::uint32_t cachedWaveGrid = UINT32_MAX;

    // Module-browser preview: a canned decaying-sine loop drawn by the same
    // renderer code path. In-place init: LoopEngine is non-copyable (atomics).
    static const LoopEngine& demoEngine(int numHeads) {
        static LoopEngine d4{LoopEngine::NUM_HEADS}, d1{1};
        static bool primed4 = false, primed1 = false;
        LoopEngine& d = numHeads == 1 ? d1 : d4;
        bool& primed = numHeads == 1 ? primed1 : primed4;
        if (!primed) {
            primed = true;
            d.reset(48000.f, 1.f);
            d.toggleRecord();
            for (int i = 0; i < 24000; ++i)
                d.process(std::sin(6.2831853f * i / 2400.f) * (1.f - i / 24000.f));
            d.toggleRecord();
        }
        return d;
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            const int w = LoopWaveformRenderer::cappedWidth(
                std::max(1, (int)std::round(box.size.x)) * kOversample);
            const int h = std::max(1, (int)std::round(box.size.y)) * kOversample;
            const LoopEngine& eng = engine ? *engine : demoEngine(demoHeads);
            const auto geometry = LoopWaveformRenderer::geometry(h, eng.numHeads());
            const int laneH = geometry.laneHeight;
            const int lanesH = geometry.lanesHeight;
            const int waveH = geometry.waveHeight;
            if ((int)wavePix.size() != w * waveH) {
                wavePix.assign((size_t)w * waveH, 0);
                if (waveImg >= 0) { nvgDeleteImage(args.vg, waveImg); waveImg = -1; }
                cachedWaveRevision = UINT32_MAX;
            }
            if ((int)lanePix.size() != w * lanesH) {
                lanePix.assign((size_t)w * lanesH, 0);
                if (laneImg >= 0) { nvgDeleteImage(args.vg, laneImg); laneImg = -1; }
            }

            const auto revision = eng.waveformRevision();
            const auto grid = eng.displaySnapshot().grid;
            if (waveImg < 0 || cachedWaveRevision != revision || cachedWaveGrid != grid) {
                LoopWaveformRenderer::renderWaveform(
                    wavePix.data(), w, waveH, eng, loopDisplayPackRGBA);
                if (waveImg < 0)
                    waveImg = nvgCreateImageRGBA(args.vg, w, waveH, 0,
                        (const unsigned char*)wavePix.data());
                else
                    nvgUpdateImage(args.vg, waveImg, (const unsigned char*)wavePix.data());
                cachedWaveRevision = revision;
                cachedWaveGrid = grid;
            }
            LoopWaveformRenderer::renderLanes(
                lanePix.data(), w, lanesH, laneH, eng, loopDisplayPackRGBA);
            if (laneImg < 0)
                laneImg = nvgCreateImageRGBA(args.vg, w, lanesH, 0,
                    (const unsigned char*)lanePix.data());
            else
                nvgUpdateImage(args.vg, laneImg, (const unsigned char*)lanePix.data());

            const float waveBoxH = box.size.y * float(waveH) / float(h);
            const float laneBoxH = box.size.y - waveBoxH;
            auto drawImage = [&](int image, float y, float height) {
                NVGpaint paint = nvgImagePattern(
                    args.vg, 0.f, y, box.size.x, height, 0.f, image, 1.f);
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 0.f, y, box.size.x, height);
                nvgFillPaint(args.vg, paint);
                nvgFill(args.vg);
            };
            drawImage(waveImg, 0.f, waveBoxH);
            drawImage(laneImg, waveBoxH, laneBoxH);
        }
        Widget::drawLayer(args, layer);
    }

    // Free the GPU image when the nanovg context goes away, so removing the
    // module (or closing Rack) doesn't leak the texture.
    void onContextDestroy(const ContextDestroyEvent& e) override {
        if (waveImg >= 0) { nvgDeleteImage(e.vg, waveImg); waveImg = -1; }
        if (laneImg >= 0) { nvgDeleteImage(e.vg, laneImg); laneImg = -1; }
        Widget::onContextDestroy(e);
    }
};
