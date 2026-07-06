#include "Lop_info.hh"
#include "CoreModules/SmartCoreProcessor.hh"
#include "CoreModules/register_module.hh"
#include "dsp/LoopEngine.hpp"
#include "display/LoopWaveformRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <span>

namespace MetaModule
{

// MetaModule pixel word layout, matching PixelRGBA::raw() in the SDK.
static uint32_t packARGB(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

class LopCore : public SmartCoreProcessor<LopInfo> {
public:
    using Info = LopInfo;
    using enum Info::Elem;

    LopCore() : engine_(1) { engine_.reset(48000.f); }

    void update() override {
        // Bypass: firmware sets `bypassed`; the core does the routing
        // (LopInfo::bypass_routes) — the 4ms cores' idiom.
        if (bypassed) {
            handle_bypass();
            return;
        }

        engine_.setOverdub(getState<OverdubSwitch>() == 1);
        engine_.setCrossfade(getState<CrossfadeSwitch>() == 0);   // index 0 = On (see QlpCrossfadeAlt)

        bool recPressed = getState<RecordButton>() == MomentaryButton::State_t::PRESSED;
        bool recTrig = getInput<RecTrigIn>().value_or(0.f) > 1.0f;
        bool recEdge = (recPressed || recTrig) && !recPrev_;
        recPrev_ = recPressed || recTrig;
        if (recEdge) engine_.toggleRecord();

        bool clrPressed = getState<ClearButton>() == MomentaryButton::State_t::PRESSED;
        if (clrPressed && !clrPrev_) engine_.clear();
        clrPrev_ = clrPressed;

        bool clrTrig = getInput<ClearTrigIn>().value_or(0.f) > 1.f;
        if (clrTrig && !clrTrigPrev_) engine_.clear();
        clrTrigPrev_ = clrTrig;

        // Knobs read back normalized 0..1; speed maps to -2..+2.
        // CV: 10 V spans each param's full range, summed with the knob, clamped.
        float spKnob = (getState<SpeedKnob>() - 0.5f) * 4.f;
        float spCv = getInput<SpeedCvIn>().value_or(0.f);
        engine_.setSpeed(0, getState<SpeedVoctAlt>() == 1
            ? std::clamp(spKnob * std::exp2(std::clamp(spCv, -5.f, 5.f)), -16.f, 16.f)
            : std::clamp(spKnob + spCv * 0.4f, -2.f, 2.f));
        engine_.setPosition(0, std::clamp(getState<PositionKnob>()
            + getInput<PositionCvIn>().value_or(0.f) * 0.1f, 0.f, 1.f));
        engine_.setSize(0, std::clamp(getState<SizeKnob>()
            + getInput<SizeCvIn>().value_or(0.f) * 0.1f, 0.f, 1.f));
        engine_.setJitter(0, std::clamp(getState<JitterKnob>()
            + getInput<JitterCvIn>().value_or(0.f) * 0.1f, 0.f, 1.f));

        const bool oneShot = getState<TrigModeAlt>() == 1;
        engine_.setOneShot(0, oneShot);
        const bool trig = getInput<TrigIn>().value_or(0.f) > 1.f;
        if (trig && !trigPrev_) {
            if (oneShot) engine_.triggerOneShot(0);
            else engine_.restartHead(0);
        }
        trigPrev_ = trig;
        float jv = getInput<JumpIn>().value_or(0.f);
        if (std::fabs(jv - lastJumpV_) > 0.05f) {
            engine_.jumpHead(0, std::clamp(jv / 10.f, 0.f, 1.f));
            lastJumpV_ = jv;
        }

        // Stereo in: an unpatched jack follows the patched one (mono -> both).
        auto inLOpt = getInput<AudioInL>(), inROpt = getInput<AudioInR>();
        float inL = (inLOpt ? *inLOpt : inROpt.value_or(0.f)) / 5.f;   // ±5V -> ±1
        float inR = (inROpt ? *inROpt : inLOpt.value_or(0.f)) / 5.f;

        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine_.process(inL, inR, hs);
        float w = std::clamp(getState<DryWetKnob>()
            + getInput<DryWetCvIn>().value_or(0.f) * 0.1f, 0.f, 1.f);
        setOutput<OutL>((inL * (1.f - w) + hs[0].l * w) * 5.f);
        setOutput<OutR>((inR * (1.f - w) + hs[0].r * w) * 5.f);
        setLED<RecordButton>(engine_.isRecording() ? 1.f : 0.f);
    }

    void set_samplerate(float sr) override { engine_.reset(sr); }

    // Display callbacks — GUI context (audio runs concurrently; the engine's
    // display snapshot/atomics make the cross-thread reads safe).
    void show_graphic_display(int display_id, std::span<uint32_t> buf,
                              unsigned width, lv_obj_t*) override {
        if (display_id == display_idx<Display>) {
            dispBuf_ = buf;
            dispWidth_ = width;
            dispDirty_ = true;   // paint at least once, even if idle
        }
    }

    bool draw_graphic_display(int display_id) override {
        if (display_id != display_idx<Display> || dispBuf_.empty() || dispWidth_ == 0)
            return false;
        const auto snap = engine_.displaySnapshot();
        const bool active = snap.loopLen > 0 || snap.recording;
        if (!active && !dispDirty_) return false;   // idle: skip redraws
        dispDirty_ = active;    // one final repaint after going idle (e.g. clear)
        LoopWaveformRenderer::render(dispBuf_.data(),
                                     int(dispWidth_),
                                     int(dispBuf_.size() / dispWidth_),
                                     engine_, packARGB);
        return true;
    }

    void hide_graphic_display(int display_id) override {
        if (display_id == display_idx<Display>) {
            dispBuf_ = {};
            dispWidth_ = 0;
        }
    }

private:
    LoopEngine engine_;
    bool recPrev_ = false, clrPrev_ = false, clrTrigPrev_ = false, trigPrev_ = false;
    float lastJumpV_ = 0.f;
    std::span<uint32_t> dispBuf_{};
    unsigned dispWidth_ = 0;
    bool dispDirty_ = false;
};

void register_lop_modules() {
    register_module<LopCore, LopInfo>("Foobar");
}

} // namespace MetaModule
