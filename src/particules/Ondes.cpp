#include "plugin.hpp"
#include "RackWavetableProvider.hpp"
#include "WavetableFrame.hpp"
#include "ondes_pitch_map.hpp"
#include "dsp/src/wavetable/wavetable_oscillator.h"
#include <cmath>

// Ondes' own pitch-knob quantity: plain linear +-24 st (see
// ondes_pitch_map.hpp), independent of the shared notched PitchParamQuantity
// in plugin.hpp that Particules/Retours use.
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

static constexpr const char* kBankGroupNames[RackWavetableProvider::kNumBankGroups] = {
    "1 - Sines", "2 - Formants", "3 - Braids"
};

struct Ondes : Module {
    enum ParamId {
        PITCH_PARAM,
        POSITION_PARAM,
        POSITION_AMT_PARAM,
        BANK_PARAM,
        BANK_AMT_PARAM,
        PARAMS_LEN
    };
    enum InputId { VOCT_INPUT, BANK_INPUT, POSITION_INPUT, INPUTS_LEN };
    enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    particules_dsp::WavetableOscillator osc_;
    RackWavetableProvider wavetable_provider_;

    // Last post-CV bank/wave (0-1), read by the panel display. UI-thread read of
    // an audio-thread write is a benign race (display only), as in Fundamental.
    float lastBank = 0.f;
    float lastWave = 0.f;

    Ondes() {
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

        osc_.Init(APP->engine->getSampleRate());
        osc_.SetProvider(&wavetable_provider_);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        osc_.Init(e.sampleRate);
        osc_.SetProvider(&wavetable_provider_);
    }

    void process(const ProcessArgs& args) override {
        float pitch = ondesKnobToSemitones(params[PITCH_PARAM].getValue())
                    + inputs[VOCT_INPUT].getVoltage() * 12.f;
        float position = clamp(
            params[POSITION_PARAM].getValue()
            + inputs[POSITION_INPUT].getVoltage() * 0.2f * params[POSITION_AMT_PARAM].getValue(),
            0.f, 1.f);
        float bank = clamp(
            params[BANK_PARAM].getValue()
            + inputs[BANK_INPUT].getVoltage() * 0.2f * params[BANK_AMT_PARAM].getValue(),
            0.f, 1.f);

        lastBank = bank;
        lastWave = position;

        particules_dsp::StereoFrame out;
        osc_.Process(pitch, bank, position, &out, 1);
        outputs[OUT_OUTPUT].setVoltage(out.l * 5.f);  // oscillator is mono: out.l == out.r
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* groups = json_array();
        for (int g = 0; g < RackWavetableProvider::kNumBankGroups; ++g)
            json_array_append_new(groups, json_boolean(wavetable_provider_.isGroupEnabled(g)));
        json_object_set_new(root, "bankGroupsEnabled", groups);
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* groups = json_object_get(root, "bankGroupsEnabled");
        if (groups && json_is_array(groups)) {
            int n = (int)json_array_size(groups);
            for (int g = 0; g < RackWavetableProvider::kNumBankGroups && g < n; ++g) {
                json_t* v = json_array_get(groups, g);
                wavetable_provider_.setGroupEnabled(g, json_boolean_value(v));
            }
            osc_.SetProvider(&wavetable_provider_);
        }
    }
};

// Draws the current bilinearly-interpolated wavetable frame as a cyan trace
// that morphs as Bank/Position (and their CV) change. Bare trace, no text.
struct WavetableDisplay : LedDisplay {
    Ondes* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            nvgScissor(args.vg, RECT_ARGS(args.clipBox));

            float bank = module ? module->lastBank : 0.f;
            float wave = module ? module->lastWave : 0.f;
            static const RackWavetableProvider kFallbackProvider;  // module==nullptr: browser thumbnail
            const RackWavetableProvider& provider =
                module ? module->wavetable_provider_ : kFallbackProvider;

            Rect scope = Rect(Vec(0, 0), box.size).shrink(Vec(4, 5));
            const int n = particules_dsp::kWavetableSize;
            nvgBeginPath(args.vg);
            for (int i = 0; i <= n; ++i) {
                float s = robotboy::wavetableFrameSample(provider, bank, wave, i % n);
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
        auto* m = dynamic_cast<Ondes*>(module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Waveform banks"));
        for (int g = 0; g < RackWavetableProvider::kNumBankGroups; ++g) {
            menu->addChild(createBoolMenuItem(kBankGroupNames[g], "",
                [m, g] { return m->wavetable_provider_.isGroupEnabled(g); },
                [m, g](bool v) {
                    m->wavetable_provider_.setGroupEnabled(g, v);
                    m->osc_.SetProvider(&m->wavetable_provider_);
                },
                /*disabled=*/!m->wavetable_provider_.canDisableGroup(g)));
        }
    }
};

Model* modelOndes = createModel<Ondes, OndesWidget>("Ondes");
