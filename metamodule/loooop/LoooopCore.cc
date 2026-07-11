#include "Loooop_info.hh"
#include "CoreModules/SmartCoreProcessor.hh"
#include "CoreModules/register_module.hh"
#include "dsp/LoopEngine.hpp"
#include "display/LoopWaveformRenderer.hpp"
#include "LooperModuleDSP.hpp"
#include <algorithm>
#include <cmath>
#include <span>

namespace MetaModule
{

// MetaModule pixel word layout, matching PixelRGBA::raw() in the SDK.
static uint32_t packARGB(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

class LoooopCore : public SmartCoreProcessor<LoooopInfo> {
public:
    using Info = LoooopInfo;
    using enum Info::Elem;

    LoooopCore() {
        engine_.reset(48000.f);
        for (auto& s : panSm_) s.alpha = loooop::smootherAlpha(48000.f, 0.002f);
    }

    void update() override {
        // Bypass: firmware sets `bypassed`; the core does the routing
        // (LoooopInfo::bypass_routes) — the 4ms cores' idiom.
        if (bypassed) {
            handle_bypass();
            return;
        }

        engine_.setOverdub(getState<OverdubSwitch>() == 1);
        engine_.setCrossfade(getState<CrossfadeSwitch>() == 0);   // index 0 = On (see QlpCrossfadeAlt)
        engine_.setWriteMode(static_cast<LoopEngine::WriteMode>(
            (int)getState<WriteModeAlt>()));
        engine_.setGrid(loooop::gridSegments((int)getState<GridAlt>()));

        // Record: momentary button OR trigger input (rising edge)
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

        auto g0 = updateHead<Speed1Knob, Position1Knob, Size1Knob, Level1Knob, Jitter1Knob,
                   Speed1CvIn, Position1CvIn, Size1CvIn, Level1CvIn, Jitter1CvIn,
                   Trig1In, Jump1In, TrigMode1Alt, SpeedVoct1Alt, Pan1Knob, Pan1CvIn>(0);
        auto g1 = updateHead<Speed2Knob, Position2Knob, Size2Knob, Level2Knob, Jitter2Knob,
                   Speed2CvIn, Position2CvIn, Size2CvIn, Level2CvIn, Jitter2CvIn,
                   Trig2In, Jump2In, TrigMode2Alt, SpeedVoct2Alt, Pan2Knob, Pan2CvIn>(1);
        auto g2 = updateHead<Speed3Knob, Position3Knob, Size3Knob, Level3Knob, Jitter3Knob,
                   Speed3CvIn, Position3CvIn, Size3CvIn, Level3CvIn, Jitter3CvIn,
                   Trig3In, Jump3In, TrigMode3Alt, SpeedVoct3Alt, Pan3Knob, Pan3CvIn>(2);
        auto g3 = updateHead<Speed4Knob, Position4Knob, Size4Knob, Level4Knob, Jitter4Knob,
                   Speed4CvIn, Position4CvIn, Size4CvIn, Level4CvIn, Jitter4CvIn,
                   Trig4In, Jump4In, TrigMode4Alt, SpeedVoct4Alt, Pan4Knob, Pan4CvIn>(3);

        // Stereo in: an unpatched jack follows the patched one (mono -> both).
        auto inLOpt = getInput<AudioInL>(), inROpt = getInput<AudioInR>();
        const auto in = loooop::normalledStereo(
            bool(inLOpt), inLOpt.value_or(0.f), bool(inROpt), inROpt.value_or(0.f));
        float inL = in.l / 5.f;   // ±5V -> ±1
        float inR = in.r / 5.f;

        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine_.process(inL, inR, hs);
        setOutput<Head1OutL>(hs[0].l * 5.f); setOutput<Head1OutR>(hs[0].r * 5.f);
        setOutput<Head2OutL>(hs[1].l * 5.f); setOutput<Head2OutR>(hs[1].r * 5.f);
        setOutput<Head3OutL>(hs[2].l * 5.f); setOutput<Head3OutR>(hs[2].r * 5.f);
        setOutput<Head4OutL>(hs[3].l * 5.f); setOutput<Head4OutR>(hs[3].r * 5.f);
        float wetL = hs[0].l*g0.l + hs[1].l*g1.l + hs[2].l*g2.l + hs[3].l*g3.l;
        float wetR = hs[0].r*g0.r + hs[1].r*g1.r + hs[2].r*g2.r + hs[3].r*g3.r;
        float w = mixSm_.process(loooop::normalizedControl(
            getState<DryWetKnob>(), getInput<DryWetCvIn>().value_or(0.f)));
        setOutput<MixOutL>(loooop::dryWet(inL, wetL, w) * 5.f);
        setOutput<MixOutR>(loooop::dryWet(inR, wetR, w) * 5.f);
        setLED<RecordButton>(engine_.isRecording() ? 1.f : 0.f);
    }

    void set_samplerate(float sr) override {
        engine_.reset(sr);
        const float a = loooop::smootherAlpha(sr, 0.002f);
        mixSm_.alpha = a;
        for (auto& s : panSm_) s.alpha = a;
    }

    // Display callbacks — GUI context (audio runs concurrently; the engine's
    // display snapshot/atomics make the cross-thread reads safe).
    void show_graphic_display(int display_id, std::span<uint32_t> buf,
                              unsigned width, lv_obj_t*) override {
        if (display_id == display_idx<Display>) {
            dispBuf_ = buf;
            dispWidth_ = width;
            dispDirty_ = true;   // paint at least once, even if idle
            cachedWaveRevision_ = UINT32_MAX;
        }
    }

    bool draw_graphic_display(int display_id) override {
        if (display_id != display_idx<Display> || dispBuf_.empty() || dispWidth_ == 0)
            return false;
        const auto snap = engine_.displaySnapshot();
        const bool active = snap.loopLen > 0 || snap.recording;
        if (!active && !dispDirty_) return false;   // idle: skip redraws
        dispDirty_ = active;    // one final repaint after going idle (e.g. clear)
        const int width = int(dispWidth_);
        const int height = int(dispBuf_.size() / dispWidth_);
        const auto geometry = LoopWaveformRenderer::geometry(height, engine_.numHeads());
        const int laneH = geometry.laneHeight;
        const int lanesH = geometry.lanesHeight;
        const int waveH = geometry.waveHeight;
        const auto revision = engine_.waveformRevision();
        // The waveform region is static between peak-array changes, so re-render
        // it only when the revision (or destination geometry) changes; the
        // persistent display canvas (dispBuf_) keeps last frame's pixels
        // otherwise. Rendered straight into the destination — no intermediate
        // cache buffer/copy needed.
        if (cachedWaveRevision_ != revision || cachedWaveWidth_ != width
            || cachedWaveHeight_ != waveH || cachedWaveGrid_ != snap.grid) {
            LoopWaveformRenderer::renderWaveform(
                dispBuf_.data(), width, waveH, engine_, packARGB);
            cachedWaveRevision_ = revision;
            cachedWaveWidth_ = width;
            cachedWaveHeight_ = waveH;
            cachedWaveGrid_ = snap.grid;
        }
        LoopWaveformRenderer::renderLanes(
            dispBuf_.data() + size_t(waveH) * width,
            width, lanesH, laneH, engine_, packARGB);
        return true;
    }

    void hide_graphic_display(int display_id) override {
        if (display_id == display_idx<Display>) {
            dispBuf_ = {};
            dispWidth_ = 0;
        }
    }

private:
    // Knobs read back normalized 0..1; speed maps to -2..+2.
    // CV: 10 V spans each param's full range, summed with the knob, clamped.
    template<Info::Elem S, Info::Elem P, Info::Elem Z, Info::Elem L, Info::Elem J,
             Info::Elem SC, Info::Elem PC, Info::Elem ZC, Info::Elem LC, Info::Elem JC,
             Info::Elem TI, Info::Elem JI, Info::Elem TM, Info::Elem VO,
             Info::Elem PN, Info::Elem PNC>
    LoopEngine::HeadOut updateHead(int h) {
        float spKnob = (getState<S>() - 0.5f) * 4.f;
        float spCv = getInput<SC>().value_or(0.f);
        engine_.setSpeed(h, getState<VO>() == 1
            ? loooop::speedFromVOct(spKnob, spCv)
            : loooop::speedFromControls(spKnob, spCv));
        engine_.setPosition(h, loooop::normalizedControl(
            getState<P>(), getInput<PC>().value_or(0.f)));
        engine_.setSize(h, loooop::normalizedControl(
            getState<Z>(), getInput<ZC>().value_or(0.f)));
        engine_.setLevel(h, loooop::normalizedControl(
            getState<L>(), getInput<LC>().value_or(0.f)));
        engine_.setJitter(h, loooop::normalizedControl(
            getState<J>(), getInput<JC>().value_or(0.f)));

        const bool oneShot = getState<TM>() == 1;
        engine_.setOneShot(h, oneShot);
        const bool trig = getInput<TI>().value_or(0.f) > 1.f;
        if (trig && !headTrigPrev_[h]) {
            if (oneShot) engine_.triggerOneShot(h);
            else engine_.restartHead(h);
        }
        headTrigPrev_[h] = trig;
        float jv = getInput<JI>().value_or(0.f);
        if (std::fabs(jv - lastJumpV_[h]) > 0.05f) {
            engine_.jumpHead(h, std::clamp(jv / 10.f, 0.f, 1.f));
            lastJumpV_[h] = jv;
        }

        float pan = panSm_[h].process(loooop::panControl(
            (getState<PN>() - 0.5f) * 2.f, getInput<PNC>().value_or(0.f)));
        LoopEngine::HeadOut g;
        g.l = loooop::panLeftGain(pan);   // gain for L contribution to mix
        g.r = loooop::panRightGain(pan);  // gain for R contribution to mix
        return g;
    }

    LoopEngine engine_;
    bool recPrev_ = false, clrPrev_ = false;
    bool clrTrigPrev_ = false;
    bool headTrigPrev_[LoopEngine::NUM_HEADS] = {};
    float lastJumpV_[LoopEngine::NUM_HEADS] = {};
    loooop::OnePoleSmoother mixSm_{1.f, loooop::smootherAlpha(48000.f, 0.002f)};
    loooop::OnePoleSmoother panSm_[LoopEngine::NUM_HEADS];   // alpha set for 48 kHz in the constructor, refreshed in set_samplerate
    std::span<uint32_t> dispBuf_{};
    unsigned dispWidth_ = 0;
    bool dispDirty_ = false;
    std::uint32_t cachedWaveRevision_ = UINT32_MAX;
    std::uint32_t cachedWaveGrid_ = UINT32_MAX;
    int cachedWaveWidth_ = -1, cachedWaveHeight_ = -1;
};

void register_loooop_modules() {
    register_module<LoooopCore, LoooopInfo>(ROBOTBOY_BRAND);
}

void register_lop_modules();

} // namespace MetaModule

#ifndef ROBOTBOY_COMBINED
#ifdef METAMODULE_BUILTIN
// Simulator built-in build: ext-plugins.cmake generates a call to
// init_<libname>(rack::plugin::Plugin*) and the build fails to link without it.
namespace rack::plugin { struct Plugin; }
void init_Loooop(rack::plugin::Plugin*) {
    MetaModule::register_loooop_modules();
    MetaModule::register_lop_modules();
}
#else
// .mmplugin build: the firmware loader looks up the unmangled `init` symbol
// (dynloader.hh fallback) and the SDK links with -Wl,--require-defined=init,
// so this entry point must have C linkage.
extern "C" void init() {
    MetaModule::register_loooop_modules();
    MetaModule::register_lop_modules();
}
#endif
#endif // ROBOTBOY_COMBINED
// Combined RobotBoy build: metamodule/register.cc owns the single init() and calls
// register_loooop_modules()/register_lop_modules() directly.
