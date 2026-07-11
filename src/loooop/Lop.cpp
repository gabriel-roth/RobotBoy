#include "plugin.hpp"
#include "LoopDisplay.hpp"
#include "dsp/LoopEngine.hpp"
#include "LooperModuleDSP.hpp"
#include "OverdubControl.hpp"
#include <array>
#include <cmath>
#include <string>
#include <vector>

struct Lop : Module {
    // Param/jack order mirrors Loooop's per-head block (minus the pan/level it
    // lacks) so the two modules' MetaModule menus read the same. Exception:
    // the MM build (Lop_info.hh) keeps an extra menu-only WriteModeAlt between
    // CrossfadeSwitch and GridAlt (MM patch compat; VCV absorbed Write mode
    // into the 5-state Overdub button), so MM ids after Crossfade are offset
    // by one — the same arrangement as Loooop.
    enum ParamId { SIZE_PARAM, POSITION_PARAM, SPEED_PARAM, JITTER_PARAM,
                   TRIG_MODE_PARAM, SPEED_VOCT_PARAM,
                   DRYWET_PARAM, RECORD_PARAM, CLEAR_PARAM, OVERDUB_PARAM, CROSSFADE_PARAM, GRID_PARAM, PARAMS_LEN };
    enum InputId { SIZE_CV_INPUT, POSITION_CV_INPUT, SPEED_CV_INPUT, JITTER_CV_INPUT, TRIG_INPUT, JUMP_INPUT,
                   AUDIO_L_INPUT, AUDIO_R_INPUT, RECORD_TRIG_INPUT, CLEAR_TRIG_INPUT, DRYWET_CV_INPUT, INPUTS_LEN };
    enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { RECORD_LIGHT, OVERDUB_R_LIGHT, OVERDUB_G_LIGHT, OVERDUB_B_LIGHT, LIGHTS_LEN };

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
        // Mode switches — panel (Overdub, Grid) and menu alike — opt out of
        // Randomize (matches Loooop): a randomized one-shot with no trigger
        // patched silences the loop with nothing on the panel to show why.
        configSwitch(OVERDUB_PARAM, 0.f, 4.f, 0.f, "Overdub",
            {"Layer", "Decay", "Add", "Replace", "Lock"})->randomizeEnabled = false;
        configSwitch(TRIG_MODE_PARAM, 0.f, 1.f, 0.f, "Trigger",
            {"Loop start", "One-shot"})->randomizeEnabled = false;
        configSwitch(SPEED_VOCT_PARAM, 0.f, 1.f, 0.f, "Speed CV V/Oct",
            {"Off", "On"})->randomizeEnabled = false;
        // Value 0 = On (default): kept inverted to match the MetaModule alt-param,
        // whose loader zero-inits unset params, so 0 must mean crossfade-on.
        configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade",
            {"On", "Off"})->randomizeEnabled = false;
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
        int od = (int)std::round(params[OVERDUB_PARAM].getValue());
        loooop::applyOverdub(engine, od);
        engine.setCrossfade(params[CROSSFADE_PARAM].getValue() < 0.5f);   // 0 = On
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
        loooop::setOverdubLED(lights, OVERDUB_R_LIGHT, od);
    }
};

struct LopWidget : ModuleWidget {
    LopWidget(Lop* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Lop.svg")));

        auto* display = new LoopDisplayWidget();
        display->engine = module ? &module->engine : nullptr;
        display->demoHeads = 1;
        display->laneDiv = LoopWaveformRenderer::LOP_LANE_DIV;
        display->laneColors = LoopWaveformRenderer::LOP_LANE_COLOR;
        // Screen rect (mm) from the SVG's screen rect element. Kept in sync
        // with vcv/res/Lop.svg and metamodule/Lop_info.hh by
        // metamodule/loooop/sync_info_positions.py — run it after any panel change.
        display->box.pos = mm2px(Vec(1.500, 10.400));
        display->box.size = mm2px(Vec(57.960, 22.350));
        addChild(display);

        // Dark screws per the panel theme (the SVG's drawn screw dots are
        // near-invisible against the background; the widgets carry the look).
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Coords come from res/Lop.svg's hidden components layer (circle ids)
        // — regenerate the panel from panel-specs/lop.yaml and carry any
        // changed positions here by hand.
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.87, 46.05)), module, Lop::SIZE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(23.61, 46.05)), module, Lop::POSITION_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(37.35, 46.05)), module, Lop::SPEED_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(51.09, 46.05)), module, Lop::JITTER_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.87, 58.0)), module, Lop::SIZE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.61, 58.0)), module, Lop::POSITION_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(37.35, 58.0)), module, Lop::SPEED_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(51.09, 58.0)), module, Lop::JITTER_CV_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.16, 74.05)), module, Lop::DRYWET_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(30.48, 74.05)), module, Lop::CLEAR_PARAM));
        addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(48.8, 74.05)), module, Lop::RECORD_PARAM, Lop::RECORD_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.16, 86.0)), module, Lop::DRYWET_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 86.0)), module, Lop::CLEAR_TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.8, 86.0)), module, Lop::RECORD_TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.252, 102.15)), module, Lop::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.992, 102.15)), module, Lop::JUMP_INPUT));
        addParam(createLightParamCentered<OverdubButton>(mm2px(Vec(37.968, 102.15)), module, Lop::OVERDUB_PARAM, Lop::OVERDUB_R_LIGHT));
        addParam(createParamCentered<RoundSmallBlackSnapKnob>(mm2px(Vec(51.708, 102.15)), module, Lop::GRID_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.35, 116.05)), module, Lop::AUDIO_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.05, 116.05)), module, Lop::AUDIO_R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(43.91, 116.05)), module, Lop::OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(53.61, 116.05)), module, Lop::OUT_R_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Lop* m = dynamic_cast<Lop*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        // Overdub (incl. write modes) and Grid are panel controls; only the
        // modes with no panel control live in the menu.
        // Same item language and order as Loooop's menu tail; One-shot is a
        // checkmark (unchecked, a trigger restarts the playhead at its
        // window start — that mode has no name in the interface).
        menu->addChild(createBoolMenuItem("One-shot on trigger", "",
            [m] { return m->params[Lop::TRIG_MODE_PARAM].getValue() > 0.5f; },
            [m](bool v) { m->paramQuantities[Lop::TRIG_MODE_PARAM]->setValue(v ? 1.f : 0.f); }));
        menu->addChild(createBoolMenuItem("Speed CV = V/Oct", "",
            [m] { return m->params[Lop::SPEED_VOCT_PARAM].getValue() > 0.5f; },
            [m](bool v) { m->paramQuantities[Lop::SPEED_VOCT_PARAM]->setValue(v ? 1.f : 0.f); }));
        menu->addChild(createBoolMenuItem("Crossfade", "",
            [m] { return m->params[Lop::CROSSFADE_PARAM].getValue() < 0.5f; },
            [m](bool v) { m->paramQuantities[Lop::CROSSFADE_PARAM]->setValue(v ? 0.f : 1.f); }));
    }
};

Model* modelLop = createModel<Lop, LopWidget>("Lop");
