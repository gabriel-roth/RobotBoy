#include "plugin.hpp"
#include "LoopDisplay.hpp"
#include "dsp/LoopEngine.hpp"
#include "LooperModuleDSP.hpp"
#include <array>
#include <cmath>
#include <string>
#include <vector>

struct Lop : Module {
    // Param/jack order mirrors Loooop's per-head block (minus the pan/level it
    // lacks) so the two modules' MetaModule menus read the same and patch ids
    // line up with metamodule/loooop/Lop_info.hh.
    enum ParamId { SIZE_PARAM, POSITION_PARAM, SPEED_PARAM, JITTER_PARAM,
                   TRIG_MODE_PARAM, SPEED_VOCT_PARAM,
                   DRYWET_PARAM, RECORD_PARAM, CLEAR_PARAM, OVERDUB_PARAM, CROSSFADE_PARAM, WRITE_MODE_PARAM, GRID_PARAM, PARAMS_LEN };
    enum InputId { SIZE_CV_INPUT, POSITION_CV_INPUT, SPEED_CV_INPUT, JITTER_CV_INPUT, TRIG_INPUT, JUMP_INPUT,
                   AUDIO_L_INPUT, AUDIO_R_INPUT, RECORD_TRIG_INPUT, CLEAR_TRIG_INPUT, DRYWET_CV_INPUT, INPUTS_LEN };
    enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { RECORD_LIGHT, LIGHTS_LEN };

    LoopEngine engine{1};      // single playhead; head level stays at its 1.0 default
    dsp::SchmittTrigger recordTrig, recordBtn, clearBtn, clearTrig, headTrig;
    float lastJumpV = 0.f;
    loooop::OnePoleSmoother mixSm{1.f, 1.f};   // value matches DRYWET default
    float smootherRate = 0.f;

    Lop() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(SPEED_PARAM, -2.f, 2.f, 1.f, "Speed");
        configParam(POSITION_PARAM, 0.f, 1.f, 0.5f, "Position");
        configParam(SIZE_PARAM, 0.f, 1.f, 1.f, "Size");
        configParam(JITTER_PARAM, 0.f, 1.f, 0.f, "Jitter");
        configParam(DRYWET_PARAM, 0.f, 1.f, 1.f, "Dry/wet");
        configButton(RECORD_PARAM, "Record/Overdub");
        configButton(CLEAR_PARAM, "Clear");
        // Menu switches opt out of Randomize (matches Loooop): a randomized
        // one-shot with no trigger patched silences the loop with nothing on
        // the panel to show why.
        configSwitch(OVERDUB_PARAM, 0.f, 1.f, 1.f, "Overdub",
            {"Off", "On"})->randomizeEnabled = false;
        configSwitch(TRIG_MODE_PARAM, 0.f, 1.f, 0.f, "Trigger",
            {"Loop start", "One-shot"})->randomizeEnabled = false;
        configSwitch(SPEED_VOCT_PARAM, 0.f, 1.f, 0.f, "Speed CV V/Oct",
            {"Off", "On"})->randomizeEnabled = false;
        // Value 0 = On (default): kept inverted to match the MetaModule alt-param,
        // whose loader zero-inits unset params, so 0 must mean crossfade-on.
        configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade",
            {"On", "Off"})->randomizeEnabled = false;
        configSwitch(WRITE_MODE_PARAM, 0.f, 3.f, 0.f, "Write mode",
            {"Add", "Replace", "Layer", "Decay"})->randomizeEnabled = false;
        configSwitch(GRID_PARAM, 0.f, 5.f, 0.f, "Grid",
            {"Off", "4", "8", "16", "32", "64"})->randomizeEnabled = false;
        configInput(AUDIO_L_INPUT, "Audio left");
        configInput(AUDIO_R_INPUT, "Audio right");
        configInput(RECORD_TRIG_INPUT, "Record trigger");
        configInput(CLEAR_TRIG_INPUT, "Clear trigger");
        configInput(SPEED_CV_INPUT, "Speed CV");
        configInput(POSITION_CV_INPUT, "Position CV");
        configInput(SIZE_CV_INPUT, "Size CV");
        configInput(JITTER_CV_INPUT, "Jitter CV");
        configInput(DRYWET_CV_INPUT, "Dry/wet CV");
        configInput(TRIG_INPUT, "Trigger");
        configInput(JUMP_INPUT, "Jump");
        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
        configBypass(AUDIO_L_INPUT, OUT_L_OUTPUT);
        configBypass(AUDIO_R_INPUT, OUT_R_OUTPUT);
        engine.reset(48000.f);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        engine.setSampleRate(e.sampleRate);
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        engine.clear();   // Initialize should silence the loop too (audio-safe; bumps the display revision)
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != smootherRate) {
            smootherRate = args.sampleRate;
            mixSm.alpha = loooop::smootherAlpha(smootherRate, 0.002f);
        }
        engine.setOverdub(params[OVERDUB_PARAM].getValue() > 0.5f);
        engine.setCrossfade(params[CROSSFADE_PARAM].getValue() < 0.5f);   // 0 = On
        engine.setWriteMode(static_cast<LoopEngine::WriteMode>(
            (int)std::round(params[WRITE_MODE_PARAM].getValue())));
        engine.setGrid(loooop::gridSegments(
            (int)std::round(params[GRID_PARAM].getValue())));
        // Evaluate both triggers into locals before OR-ing: `||` short-
        // circuits, so `a || b` would skip calling b.process() (and updating
        // its Schmitt state) on any sample where a is already true.
        bool recBtn  = recordBtn.process(params[RECORD_PARAM].getValue());
        bool recTrig = recordTrig.process(inputs[RECORD_TRIG_INPUT].getVoltage(), 0.1f, 2.f);
        if (recBtn || recTrig)
            engine.toggleRecord();
        bool clrBtn  = clearBtn.process(params[CLEAR_PARAM].getValue());
        bool clrTrig = clearTrig.process(inputs[CLEAR_TRIG_INPUT].getVoltage(), 0.1f, 2.f);
        if (clrBtn || clrTrig)
            engine.clear();

        // CV: 10 V spans each param's full range, summed with the knob, clamped.
        float spKnob = params[SPEED_PARAM].getValue();
        float spCv = inputs[SPEED_CV_INPUT].getVoltage();
        engine.setSpeed(0, params[SPEED_VOCT_PARAM].getValue() > 0.5f
            ? loooop::speedFromVOct(spKnob, spCv)
            : loooop::speedFromControls(spKnob, spCv));
        engine.setPosition(0, loooop::normalizedControl(
            params[POSITION_PARAM].getValue(), inputs[POSITION_CV_INPUT].getVoltage()));
        engine.setSize(0, loooop::normalizedControl(
            params[SIZE_PARAM].getValue(), inputs[SIZE_CV_INPUT].getVoltage()));
        engine.setJitter(0, loooop::normalizedControl(
            params[JITTER_PARAM].getValue(), inputs[JITTER_CV_INPUT].getVoltage()));

        bool oneShot = params[TRIG_MODE_PARAM].getValue() > 0.5f;
        engine.setOneShot(0, oneShot);
        if (headTrig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 2.f)) {
            if (oneShot) engine.triggerOneShot(0);
            else engine.restartHead(0);
        }
        float jv = inputs[JUMP_INPUT].getVoltage();
        if (std::fabs(jv - lastJumpV) > 0.05f) {
            engine.jumpHead(0, clamp(jv / 10.f, 0.f, 1.f));
            lastJumpV = jv;
        }

        // Stereo in: an unpatched jack follows the patched one (mono -> both).
        const auto in = loooop::normalledStereo(
            inputs[AUDIO_L_INPUT].isConnected(), inputs[AUDIO_L_INPUT].getVoltage(),
            inputs[AUDIO_R_INPUT].isConnected(), inputs[AUDIO_R_INPUT].getVoltage());
        const float inL = in.l;
        const float inR = in.r;

        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine.process(inL / 5.f, inR / 5.f, hs);   // ±5V <-> ±1
        const float w = mixSm.process(loooop::normalizedControl(
            params[DRYWET_PARAM].getValue(), inputs[DRYWET_CV_INPUT].getVoltage()));
        outputs[OUT_L_OUTPUT].setVoltage(loooop::dryWet(inL / 5.f, hs[0].l, w) * 5.f);
        outputs[OUT_R_OUTPUT].setVoltage(loooop::dryWet(inR / 5.f, hs[0].r, w) * 5.f);
        lights[RECORD_LIGHT].setBrightness(engine.isRecording() ? 1.f : 0.f);
    }
};

struct LopWidget : ModuleWidget {
    LopWidget(Lop* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Lop.svg")));

        auto* display = new LoopDisplayWidget();
        display->engine = module ? &module->engine : nullptr;
        display->demoHeads = 1;
        // Screen rect (mm) from the SVG's screen rect element. Kept in sync
        // with vcv/res/Lop.svg and metamodule/Lop_info.hh by
        // metamodule/sync_info_positions.py — run it after any panel change.
        display->box.pos = mm2px(Vec(1.500, 10.400));
        display->box.size = mm2px(Vec(57.960, 22.350));
        addChild(display);

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Placeholder Vec() coords — scripts/sync_positions.py patches them
        // from res/Lop.svg circle ids.
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.16, 46.35)), module, Lop::SPEED_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48, 46.35)), module, Lop::POSITION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(48.8, 46.35)), module, Lop::JITTER_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.16, 58.7)), module, Lop::SPEED_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 58.7)), module, Lop::POSITION_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.8, 58.7)), module, Lop::JITTER_CV_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.87, 75.05)), module, Lop::SIZE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(23.61, 75.05)), module, Lop::DRYWET_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.87, 87.4)), module, Lop::SIZE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.61, 87.4)), module, Lop::DRYWET_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.89, 116.05)), module, Lop::AUDIO_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(21.59, 116.05)), module, Lop::AUDIO_R_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.87, 102.1)), module, Lop::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.61, 102.1)), module, Lop::JUMP_INPUT));
        addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(37.35, 91.25)), module, Lop::RECORD_PARAM, Lop::RECORD_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(37.35, 102.1)), module, Lop::RECORD_TRIG_INPUT));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(51.09, 91.25)), module, Lop::CLEAR_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(51.09, 102.1)), module, Lop::CLEAR_TRIG_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(39.37, 116.05)), module, Lop::OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(49.07, 116.05)), module, Lop::OUT_R_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Lop* m = dynamic_cast<Lop*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolMenuItem("Overdub", "",
            [m] { return m->params[Lop::OVERDUB_PARAM].getValue() > 0.5f; },
            [m](bool v) { m->paramQuantities[Lop::OVERDUB_PARAM]->setValue(v ? 1.f : 0.f); }));
        menu->addChild(createBoolMenuItem("Crossfade loop seams", "",
            [m] { return m->params[Lop::CROSSFADE_PARAM].getValue() < 0.5f; },
            [m](bool v) { m->paramQuantities[Lop::CROSSFADE_PARAM]->setValue(v ? 0.f : 1.f); }));
        static const std::vector<std::string> kWriteModes = {"Add", "Replace", "Layer", "Decay"};
        menu->addChild(createIndexSubmenuItem("Write mode", kWriteModes,
            [m] { return (int)std::round(m->params[Lop::WRITE_MODE_PARAM].getValue()); },
            [m](int v) { m->paramQuantities[Lop::WRITE_MODE_PARAM]->setValue((float)v); }));
        static const std::vector<std::string> kGridLabels = {"Off", "4", "8", "16", "32", "64"};
        menu->addChild(createIndexSubmenuItem("Grid", kGridLabels,
            [m] { return (int)std::round(m->params[Lop::GRID_PARAM].getValue()); },
            [m](int v) { m->paramQuantities[Lop::GRID_PARAM]->setValue((float)v); }));
        static const std::vector<std::string> kTrigModes = {"Loop start", "One-shot"};
        menu->addChild(createIndexSubmenuItem("Trigger", kTrigModes,
            [m] { return (int)std::round(m->params[Lop::TRIG_MODE_PARAM].getValue()); },
            [m](int v) { m->paramQuantities[Lop::TRIG_MODE_PARAM]->setValue((float)v); }));
        menu->addChild(createBoolMenuItem("Speed CV is V/Oct", "",
            [m] { return m->params[Lop::SPEED_VOCT_PARAM].getValue() > 0.5f; },
            [m](bool v) { m->paramQuantities[Lop::SPEED_VOCT_PARAM]->setValue(v ? 1.f : 0.f); }));
    }
};

Model* modelLop = createModel<Lop, LopWidget>("Lop");
