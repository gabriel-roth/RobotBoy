#include "plugin.hpp"
#include "RackWavetableProvider.hpp"
#include "dsp/src/wavetable/wavetable_oscillator.h"
#include <cmath>

struct Ondes : Module {
    enum ParamId {
        PITCH_PARAM,
        POSITION_PARAM,
        POSITION_AMT_PARAM,
        BANK_PARAM,
        BANK_AMT_PARAM,
        PARAMS_LEN
    };
    enum InputId { VOCT_INPUT, POSITION_INPUT, BANK_INPUT, INPUTS_LEN };
    enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    particules_dsp::WavetableOscillator osc_;
    RackWavetableProvider wavetable_provider_;

    // Pitch knob cache: pitchKnobToSemitones() is a linear search; skip when unchanged.
    float cached_pitch_knob_      = -999.f;
    float cached_pitch_semitones_ = 0.f;

    Ondes() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
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
        float raw_pitch = params[PITCH_PARAM].getValue();
        if (raw_pitch != cached_pitch_knob_) {
            cached_pitch_knob_      = raw_pitch;
            cached_pitch_semitones_ = pitchKnobToSemitones(raw_pitch);
        }
        float pitch = cached_pitch_semitones_ + inputs[VOCT_INPUT].getVoltage() * 12.f;
        float position = clamp(
            params[POSITION_PARAM].getValue()
            + inputs[POSITION_INPUT].getVoltage() * 0.2f * params[POSITION_AMT_PARAM].getValue(),
            0.f, 1.f);
        float bank = clamp(
            params[BANK_PARAM].getValue()
            + inputs[BANK_INPUT].getVoltage() * 0.2f * params[BANK_AMT_PARAM].getValue(),
            0.f, 1.f);

        particules_dsp::StereoFrame out;
        osc_.Process(pitch, bank, position, &out, 1);
        outputs[OUT_OUTPUT].setVoltage(out.l * 5.f);  // oscillator is mono: out.l == out.r
    }
};

struct OndesWidget : ModuleWidget {
    OndesWidget(Ondes* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Ondes.svg")));

        addChild(createWidget<ScrewBlack>(Vec(0, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(15.24, 27)),     module, Ondes::PITCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(22.86, 69.167)),      module, Ondes::POSITION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(7.62, 69.167)),       module, Ondes::BANK_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(22.86, 89.867)),            module, Ondes::POSITION_AMT_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(7.62, 89.867)),             module, Ondes::BANK_AMT_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 38.7)),   module, Ondes::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.86, 80.867)), module, Ondes::POSITION_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.62, 80.867)),  module, Ondes::BANK_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(15.24, 114)),  module, Ondes::OUT_OUTPUT));
    }
};

Model* modelOndes = createModel<Ondes, OndesWidget>("Ondes");
