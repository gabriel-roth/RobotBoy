#include "Lop_info.hh"
#include "CoreModules/SmartCoreProcessor.hh"
#include "CoreModules/register_module.hh"
#include "dsp/LoopEngine.hpp"
#include "display/LoopWaveformRenderer.hpp"
#include "LooperModuleDSP.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>
#include <string>

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

        engine_.setCrossfade(getState<CrossfadeSwitch>() == 0);   // index 0 = On (see QlpCrossfadeAlt)

        // Overdub: five-position FlipSwitch (Layer/Decay/Add/Replace/Lock). Its
        // colour comes from the per-mode frame image, so no LED to drive here;
        // getState returns the position index (0..4) directly.
        int od = std::clamp((int)getState<OverdubSwitch>(), 0, 4);
        loooop::applyOverdub(engine_, od);

        engine_.setGrid(loooop::gridSegments((int)(getState<GridKnob>() * 5.f + 0.5f)));

        // Record: momentary button + jack, routed through the shared
        // RecordGateHelper. Trigger mode ORs the button's and jack's rising
        // edges INDEPENDENTLY (matching VCV's two-independent-Schmitt-
        // triggers behavior) rather than the previous single combined-level
        // latch -- a deliberate behavior change here: the button is no longer
        // dead while the jack is held high. Gate mode reinterprets the jack's
        // own rising/falling edges as start/stop. Same >1.0f jack threshold
        // as before (no hysteresis on MetaModule).
        const bool recPressed = getState<RecordButton>() == MomentaryButton::State_t::PRESSED;
        const bool recJackHigh = getInput<RecTrigIn>().value_or(0.f) > 1.0f;
        if (!recordGateInited_) {
            recordGate_.syncTo(recJackHigh);   // patch load / first update(): no phantom edge
            recordGateInited_ = true;
        }
        const bool recGateMode = getState<RecGateAlt>() == 1;
        const auto recAction = recordGate_.step(recGateMode, recJackHigh, recPressed, engine_.isRecording());
        const bool trigWhenRec = getState<TrigWhenRecAlt>() == 1;
        switch (recAction) {
            case loooop::RecordGateHelper::Action::Toggle:
            case loooop::RecordGateHelper::Action::Close:
                engine_.toggleRecord(trigWhenRec);
                break;
            case loooop::RecordGateHelper::Action::None:
                break;
        }

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
            ? voctMemo_.get(spKnob, spCv)
            : loooop::speedFromControls(spKnob, spCv));
        engine_.setPosition(0, loooop::normalizedControl(
            getState<PositionKnob>(), getInput<PositionCvIn>().value_or(0.f)));
        engine_.setSize(0, loooop::normalizedControl(
            getState<SizeKnob>(), getInput<SizeCvIn>().value_or(0.f)));
        engine_.setJitter(0, loooop::normalizedControl(
            getState<JitterKnob>(), getInput<JitterCvIn>().value_or(0.f)));

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
        const auto in = loooop::normalledStereo(
            bool(inLOpt), inLOpt.value_or(0.f), bool(inROpt), inROpt.value_or(0.f));
        float inL = in.l * 0.2f;   // ±5V -> ±1
        float inR = in.r * 0.2f;

        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine_.process(inL, inR, hs);
        float w = mixSm_.process(loooop::normalizedControl(
            getState<DryWetKnob>(), getInput<DryWetCvIn>().value_or(0.f)));
        setOutput<OutL>(loooop::dryWet(inL, hs[0].l, w) * 5.f);
        setOutput<OutR>(loooop::dryWet(inR, hs[0].r, w) * 5.f);
        setLED<RecordButton>(engine_.isRecording() ? 1.f : 0.f);
    }

    void set_samplerate(float sr) override {
        engine_.setSampleRate(sr);   // preserve a recorded loop (matches VCV onSampleRateChange)
        mixSm_.alpha = loooop::smootherAlpha(sr, 0.002f);
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
        // The waveform region is static between recorded-audio (revision) changes, so re-render
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
    LoopEngine engine_;
    loooop::RecordGateHelper recordGate_;
    bool recordGateInited_ = false;   // primes recordGate_.syncTo() on the first update()
    bool clrPrev_ = false, clrTrigPrev_ = false, trigPrev_ = false;
    float lastJumpV_ = 0.f;
    loooop::VOctSpeedMemo voctMemo_;
    loooop::OnePoleSmoother mixSm_{1.f, loooop::smootherAlpha(48000.f, 0.002f)};
    std::span<uint32_t> dispBuf_{};
    unsigned dispWidth_ = 0;
    bool dispDirty_ = false;
    std::uint32_t cachedWaveRevision_ = UINT32_MAX;
    std::uint32_t cachedWaveGrid_ = UINT32_MAX;
    int cachedWaveWidth_ = -1, cachedWaveHeight_ = -1;
};

void register_lop_modules() {
    register_module<LopCore, LopInfo>(ROBOTBOY_BRAND);
}

} // namespace MetaModule
