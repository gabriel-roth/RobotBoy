#pragma once
#include "plugin.hpp"
#include "../../src/dsp/LoopEngine.hpp"
#include "../../src/display/LoopWaveformRenderer.hpp"
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
    std::vector<uint32_t> pix;
    int img = -1;

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
            const int w = std::max(1, (int)std::round(box.size.x)) * kOversample;
            const int h = std::max(1, (int)std::round(box.size.y)) * kOversample;
            if ((int)pix.size() != w * h) {
                pix.assign((size_t)w * h, 0);
                if (img >= 0) { nvgDeleteImage(args.vg, img); img = -1; }
            }
            const LoopEngine& eng = engine ? *engine : demoEngine(demoHeads);
            LoopWaveformRenderer::render(pix.data(), w, h, eng, loopDisplayPackRGBA);
            if (img < 0)
                img = nvgCreateImageRGBA(args.vg, w, h, 0, (const unsigned char*)pix.data());
            else
                nvgUpdateImage(args.vg, img, (const unsigned char*)pix.data());
            NVGpaint paint = nvgImagePattern(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0.f, img, 1.f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
            nvgFillPaint(args.vg, paint);
            nvgFill(args.vg);
        }
        Widget::drawLayer(args, layer);
    }

    // Free the GPU image when the nanovg context goes away, so removing the
    // module (or closing Rack) doesn't leak the texture.
    void onContextDestroy(const ContextDestroyEvent& e) override {
        if (img >= 0) { nvgDeleteImage(e.vg, img); img = -1; }
        Widget::onContextDestroy(e);
    }
};
