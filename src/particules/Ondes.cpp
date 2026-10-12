#include "Ondes.hpp"
#include "beads_menu.hpp"
#include "ondes_pitch_map.hpp"

#include <cmath>

// Ondes' own pitch-knob quantity: plain linear +-24 st (see
// ondes_pitch_map.hpp), independent of the notched PitchParamQuantity
// that Particules/Retours use.
struct OndesPitchParamQuantity : ParamQuantity {
    float getDisplayValue() override { return ondesKnobToSemitones(getValue()); }
    void setDisplayValue(float semitones) override { setValue(ondesSemitonesToKnob(semitones)); }
    std::string getDisplayValueString() override {
        float st = getDisplayValue();
        if (std::fabs(st - std::round(st)) < 0.05f) return string::f("%d", (int)std::round(st));
        return string::f("%.1f", st);
    }
    std::string getUnit() override { return " st"; }
};

Ondes::Ondes() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    configParam<OndesPitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
    configParam(POSITION_PARAM, 0.f, 1.f, 0.5f, "Position");
    configParam(POSITION_AMT_PARAM, -1.f, 1.f, 0.f, "Position CV amount");
    configParam(BANK_PARAM, 0.f, 1.f, 0.f, "Bank");
    configParam(BANK_AMT_PARAM, -1.f, 1.f, 0.f, "Bank CV amount");
    configInput(VOCT_INPUT, "Pitch (V/oct)");
    configInput(POSITION_INPUT, "Position CV");
    configInput(BANK_INPUT, "Bank CV");
    configOutput(OUT_OUTPUT, "Audio out");

    beads_ = MakeOndesBeads(*this);
    classic_engine_ = MakeOndesClassic(*this);
    classic_.store(false);
    active_.store(beads_.get(), std::memory_order_release);
}

void Ondes::requestMode(bool classic) {   // UI thread
    pending_mode_.store(classic ? 1 : 0, std::memory_order_release);
}

void Ondes::applyPendingMode() {   // audio thread
    int pending = pending_mode_.load(std::memory_order_acquire);
    if (pending < 0) return;
    const bool want = pending != 0;
    if (want != classicMode()) {
        classic_.store(want, std::memory_order_relaxed);
        active_.store(want ? classic_engine_.get() : beads_.get(), std::memory_order_release);
    }
    pending_mode_.compare_exchange_strong(pending, kModeNone, std::memory_order_acq_rel);
}

void Ondes::onSampleRateChange(const SampleRateChangeEvent& e) {
    beads_->onSampleRateChange(e.sampleRate);
    classic_engine_->onSampleRateChange(e.sampleRate);
}

void Ondes::process(const ProcessArgs& args) {
    applyPendingMode();
    active_.load(std::memory_order_relaxed)->process(args);
}

json_t* Ondes::dataToJson() {
    json_t* root = json_object();
    // A switch not yet applied is saved as the mode, so the menu's undo
    // snapshot records it.
    json_object_set_new(root, "classic", json_boolean(displayedClassicMode()));
    return root;
}

void Ondes::dataFromJson(json_t* root) {
    // v2.1 saved "bankGroupsEnabled" on every save; the beta saved nothing.
    // Its values are ignored: classic mode always has all 24 banks.
    json_t* classic_j = json_object_get(root, "classic");
    const bool classic = robotboy::DetectClassic(
        classic_j != nullptr, json_is_true(classic_j),
        json_object_get(root, "bankGroupsEnabled") != nullptr,
        robotboy::ClassicFingerprint::kPresentBankGroups);
    // Applied at the next process(), on the audio thread.
    pending_mode_.store(classic ? 1 : 0, std::memory_order_release);
}

// Draws the active engine's current wave as a cyan trace that morphs as
// Bank/Position (and their CV) change. Bare trace, no text.
struct WavetableDisplay : LedDisplay {
    Ondes* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            nvgScissor(args.vg, RECT_ARGS(args.clipBox));

            const OndesEngine* engine = module ? module->active_.load(std::memory_order_acquire) : nullptr;
            const int n = 256;   // both engines' table size
            Rect scope = Rect(Vec(0, 0), box.size).shrink(Vec(4, 5));
            nvgBeginPath(args.vg);
            for (int i = 0; i <= n; ++i) {
                const float s = engine ? engine->displaySample(i % n, n)
                                       : OndesPreviewSample(i % n, n);
                Vec p;
                p.x = float(i) / n;
                p.y = 0.5f - 0.5f * s;
                p = scope.pos + scope.size.mult(p);
                if (i == 0) nvgMoveTo(args.vg, VEC_ARGS(p));
                else        nvgLineTo(args.vg, VEC_ARGS(p));
            }
            nvgLineCap(args.vg, NVG_ROUND);
            nvgMiterLimit(args.vg, 2.f);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStrokeColor(args.vg, nvgRGB(0x00, 0xe5, 0xff));  // cyan
            nvgStroke(args.vg);

            nvgResetScissor(args.vg);
        }
        LedDisplay::drawLayer(args, layer);
    }
};

struct OndesWidget : ModuleWidget {
    OndesWidget(Ondes* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Ondes.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.145, 51.35)), module, Ondes::PITCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.66, 74.35)),  module, Ondes::BANK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(28.98, 74.35)),  module, Ondes::POSITION_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(11.66, 97.05)),         module, Ondes::BANK_AMT_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(28.98, 97.05)),         module, Ondes::POSITION_AMT_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(26.495, 51.35)), module, Ondes::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.66, 86.7)),   module, Ondes::BANK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(28.98, 86.7)),   module, Ondes::POSITION_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(20.32, 113.7)), module, Ondes::OUT_OUTPUT));

        WavetableDisplay* display = createWidget<WavetableDisplay>(mm2px(Vec(3.0, 9.4)));
        display->box.size = mm2px(Vec(34.64, 24.5));
        display->module = module;
        addChild(display);
    }

    void appendContextMenu(Menu* menu) override {
        Ondes* module = dynamic_cast<Ondes*>(this->module);
        if (!module) return;

        menu->addChild(new MenuSeparator);
        // Classic mode: the v2.1 oscillator and its 24 banks. Ondes has no
        // buffer, so the switch is instant.
        menu->addChild(createBoolMenuItem("Classic mode", "",
            [=]() { return module->displayedClassicMode(); },
            [=](bool on) {
                withMenuUndo(module, on ? "enable classic mode" : "disable classic mode",
                    [=]() { module->requestMode(on); });
            }));
    }
};

Model* modelOndes = createModel<Ondes, OndesWidget>("Ondes");
