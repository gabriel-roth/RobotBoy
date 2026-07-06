#include "plugin.hpp"
#include "dsp/LoopEngine.hpp"
#include "LoopDisplay.hpp"
#include <cmath>
#include <array>
#include <vector>
#include <string>

struct Loooop : Module {
    // Params and inputs are grouped PER HEAD so the MetaModule mapping menu
    // lists everything for one head together; this mirrors the Elem order in
    // metamodule/loooop/Loooop_info.hh so patch ids line up between the two
    // builds. Each head is a contiguous block of HEAD_PARAMS params /
    // HEAD_INPUTS inputs — index a head with `X1_PARAM + HEAD_PARAMS * h`.
    enum ParamId { SIZE1_PARAM, POSITION1_PARAM, SPEED1_PARAM, JITTER1_PARAM, PAN1_PARAM, LEVEL1_PARAM, TRIG_MODE1_PARAM, SPEED_VOCT1_PARAM,
                   SIZE2_PARAM, POSITION2_PARAM, SPEED2_PARAM, JITTER2_PARAM, PAN2_PARAM, LEVEL2_PARAM, TRIG_MODE2_PARAM, SPEED_VOCT2_PARAM,
                   SIZE3_PARAM, POSITION3_PARAM, SPEED3_PARAM, JITTER3_PARAM, PAN3_PARAM, LEVEL3_PARAM, TRIG_MODE3_PARAM, SPEED_VOCT3_PARAM,
                   SIZE4_PARAM, POSITION4_PARAM, SPEED4_PARAM, JITTER4_PARAM, PAN4_PARAM, LEVEL4_PARAM, TRIG_MODE4_PARAM, SPEED_VOCT4_PARAM,
                   DRYWET_PARAM, RECORD_PARAM, CLEAR_PARAM, OVERDUB_PARAM, CROSSFADE_PARAM,
                   PARAMS_LEN };
    enum InputId { SIZE1_CV_INPUT, POSITION1_CV_INPUT, SPEED1_CV_INPUT, JITTER1_CV_INPUT, PAN1_CV_INPUT, LEVEL1_CV_INPUT, TRIG1_INPUT, JUMP1_INPUT,
                   SIZE2_CV_INPUT, POSITION2_CV_INPUT, SPEED2_CV_INPUT, JITTER2_CV_INPUT, PAN2_CV_INPUT, LEVEL2_CV_INPUT, TRIG2_INPUT, JUMP2_INPUT,
                   SIZE3_CV_INPUT, POSITION3_CV_INPUT, SPEED3_CV_INPUT, JITTER3_CV_INPUT, PAN3_CV_INPUT, LEVEL3_CV_INPUT, TRIG3_INPUT, JUMP3_INPUT,
                   SIZE4_CV_INPUT, POSITION4_CV_INPUT, SPEED4_CV_INPUT, JITTER4_CV_INPUT, PAN4_CV_INPUT, LEVEL4_CV_INPUT, TRIG4_INPUT, JUMP4_INPUT,
                   AUDIO_L_INPUT, AUDIO_R_INPUT, RECORD_TRIG_INPUT, CLEAR_TRIG_INPUT, DRYWET_CV_INPUT,
                   INPUTS_LEN };
    static constexpr int HEAD_PARAMS = 8;   // per-head param stride: Size,Pos,Speed,Jitter,Pan,Level,TrigMode,SpeedVoct
    static constexpr int HEAD_INPUTS = 8;   // per-head input stride: SizeCV,PosCV,SpeedCV,JitterCV,PanCV,LevelCV,Trig,Jump
    enum OutputId { HEAD1_L_OUTPUT, HEAD1_R_OUTPUT, HEAD2_L_OUTPUT, HEAD2_R_OUTPUT,
                    HEAD3_L_OUTPUT, HEAD3_R_OUTPUT, HEAD4_L_OUTPUT, HEAD4_R_OUTPUT,
                    MIX_L_OUTPUT, MIX_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { RECORD_LIGHT, LIGHTS_LEN };

    LoopEngine engine;
    dsp::SchmittTrigger recordTrig, recordBtn, clearBtn, clearTrig, headTrig[LoopEngine::NUM_HEADS];
    float lastJumpV[LoopEngine::NUM_HEADS] = {};

    Loooop() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
            const std::string n = std::to_string(h + 1);
            configParam(SPEED1_PARAM + HEAD_PARAMS * h, -2.f, 2.f, 1.f, "Head " + n + " speed");
            configParam(POSITION1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.5f, "Head " + n + " position");
            configParam(SIZE1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 1.f, "Head " + n + " size");
            // Level defaults to 0.25: four phase-locked heads at defaults sum to
            // unity, matching the old single-head loudness.
            configParam(LEVEL1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.25f, "Head " + n + " level");
            configParam(JITTER1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f, "Head " + n + " jitter");
            configSwitch(TRIG_MODE1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f, "Head " + n + " trigger",
                {"Loop start", "One-shot"});
            configSwitch(SPEED_VOCT1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f, "Head " + n + " speed CV V/Oct",
                {"Off", "On"});
            configInput(SPEED1_CV_INPUT + HEAD_INPUTS * h, "Head " + n + " speed CV");
            configInput(POSITION1_CV_INPUT + HEAD_INPUTS * h, "Head " + n + " position CV");
            configInput(SIZE1_CV_INPUT + HEAD_INPUTS * h, "Head " + n + " size CV");
            configInput(LEVEL1_CV_INPUT + HEAD_INPUTS * h, "Head " + n + " level CV");
            configInput(JITTER1_CV_INPUT + HEAD_INPUTS * h, "Head " + n + " jitter CV");
            configParam(PAN1_PARAM + HEAD_PARAMS * h, -1.f, 1.f, 0.f, "Head " + n + " pan");
            configInput(PAN1_CV_INPUT + HEAD_INPUTS * h, "Head " + n + " pan CV");
            configInput(TRIG1_INPUT + HEAD_INPUTS * h, "Head " + n + " trigger");
            configInput(JUMP1_INPUT + HEAD_INPUTS * h, "Head " + n + " jump");
            configOutput(HEAD1_L_OUTPUT + 2 * h, "Head " + n + " left");
            configOutput(HEAD1_L_OUTPUT + 2 * h + 1, "Head " + n + " right");
        }
        configParam(DRYWET_PARAM, 0.f, 1.f, 1.f, "Mix dry/wet");
        configButton(RECORD_PARAM, "Record/Overdub");
        configButton(CLEAR_PARAM, "Clear");
        configSwitch(OVERDUB_PARAM, 0.f, 1.f, 1.f, "Overdub", {"Off", "On"});
        // Value 0 = On (default): kept inverted to match the MetaModule alt-param,
        // whose loader zero-inits unset params, so 0 must mean crossfade-on.
        configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade", {"On", "Off"});
        configInput(AUDIO_L_INPUT, "Audio left");
        configInput(AUDIO_R_INPUT, "Audio right");
        configInput(RECORD_TRIG_INPUT, "Record trigger");
        configInput(CLEAR_TRIG_INPUT, "Clear trigger");
        configInput(DRYWET_CV_INPUT, "Mix dry/wet CV");
        configOutput(MIX_L_OUTPUT, "Mix left");
        configOutput(MIX_R_OUTPUT, "Mix right");
        configBypass(AUDIO_L_INPUT, MIX_L_OUTPUT);
        configBypass(AUDIO_R_INPUT, MIX_R_OUTPUT);
        engine.reset(48000.f);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        engine.reset(e.sampleRate);
    }

    void process(const ProcessArgs& args) override {
        engine.setOverdub(params[OVERDUB_PARAM].getValue() > 0.5f);
        engine.setCrossfade(params[CROSSFADE_PARAM].getValue() < 0.5f);   // 0 = On
        if (recordBtn.process(params[RECORD_PARAM].getValue()) ||
            recordTrig.process(inputs[RECORD_TRIG_INPUT].getVoltage(), 0.1f, 2.f))
            engine.toggleRecord();
        if (clearBtn.process(params[CLEAR_PARAM].getValue()) ||
            clearTrig.process(inputs[CLEAR_TRIG_INPUT].getVoltage(), 0.1f, 2.f))
            engine.clear();

        // CV: 10 V spans each param's full range, summed with the knob, clamped.
        for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
            float spKnob = params[SPEED1_PARAM + HEAD_PARAMS * h].getValue();
            float spCv = inputs[SPEED1_CV_INPUT + HEAD_INPUTS * h].getVoltage();
            engine.setSpeed(h, params[SPEED_VOCT1_PARAM + HEAD_PARAMS * h].getValue() > 0.5f
                ? clamp(spKnob * std::exp2(clamp(spCv, -5.f, 5.f)), -16.f, 16.f)
                : clamp(spKnob + spCv * 0.4f, -2.f, 2.f));
            engine.setPosition(h, clamp(params[POSITION1_PARAM + HEAD_PARAMS * h].getValue()
                + inputs[POSITION1_CV_INPUT + HEAD_INPUTS * h].getVoltage() * 0.1f, 0.f, 1.f));
            engine.setSize(h, clamp(params[SIZE1_PARAM + HEAD_PARAMS * h].getValue()
                + inputs[SIZE1_CV_INPUT + HEAD_INPUTS * h].getVoltage() * 0.1f, 0.f, 1.f));
            engine.setLevel(h, clamp(params[LEVEL1_PARAM + HEAD_PARAMS * h].getValue()
                + inputs[LEVEL1_CV_INPUT + HEAD_INPUTS * h].getVoltage() * 0.1f, 0.f, 1.f));
            engine.setJitter(h, clamp(params[JITTER1_PARAM + HEAD_PARAMS * h].getValue()
                + inputs[JITTER1_CV_INPUT + HEAD_INPUTS * h].getVoltage() * 0.1f, 0.f, 1.f));

            bool oneShot = params[TRIG_MODE1_PARAM + HEAD_PARAMS * h].getValue() > 0.5f;
            engine.setOneShot(h, oneShot);
            if (headTrig[h].process(inputs[TRIG1_INPUT + HEAD_INPUTS * h].getVoltage(), 0.1f, 2.f)) {
                if (oneShot) engine.triggerOneShot(h);
                else engine.restartHead(h);
            }
            float jv = inputs[JUMP1_INPUT + HEAD_INPUTS * h].getVoltage();
            if (std::fabs(jv - lastJumpV[h]) > 0.05f) {
                engine.jumpHead(h, clamp(jv / 10.f, 0.f, 1.f));
                lastJumpV[h] = jv;
            }
        }

        // Stereo in: an unpatched jack follows the patched one (mono -> both).
        float inL = inputs[AUDIO_L_INPUT].isConnected()
            ? inputs[AUDIO_L_INPUT].getVoltage() : inputs[AUDIO_R_INPUT].getVoltage();
        float inR = inputs[AUDIO_R_INPUT].isConnected()
            ? inputs[AUDIO_R_INPUT].getVoltage() : inputs[AUDIO_L_INPUT].getVoltage();

        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine.process(inL / 5.f, inR / 5.f, hs);   // ±5V <-> ±1
        float wetL = 0.f, wetR = 0.f;
        for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
            outputs[HEAD1_L_OUTPUT + 2 * h].setVoltage(hs[h].l * 5.f);
            outputs[HEAD1_L_OUTPUT + 2 * h + 1].setVoltage(hs[h].r * 5.f);
            // Pan is a balance on each head's contribution to the mix only;
            // the individual head outputs above are unaffected. Center = unity.
            float pan = clamp(params[PAN1_PARAM + HEAD_PARAMS * h].getValue()
                + inputs[PAN1_CV_INPUT + HEAD_INPUTS * h].getVoltage() * 0.2f, -1.f, 1.f);
            float gL = pan <= 0.f ? 1.f : 1.f - pan;
            float gR = pan >= 0.f ? 1.f : 1.f + pan;
            wetL += hs[h].l * gL; wetR += hs[h].r * gR;
        }
        float w = clamp(params[DRYWET_PARAM].getValue()
            + inputs[DRYWET_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
        outputs[MIX_L_OUTPUT].setVoltage((inL / 5.f * (1.f - w) + wetL * w) * 5.f);
        outputs[MIX_R_OUTPUT].setVoltage((inR / 5.f * (1.f - w) + wetR * w) * 5.f);
        lights[RECORD_LIGHT].setBrightness(engine.isRecording() ? 1.f : 0.f);
    }
};

struct LoooopWidget : ModuleWidget {
    LoooopWidget(Loooop* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Loooop.svg")));

        auto* display = new LoopDisplayWidget();
        display->engine = module ? &module->engine : nullptr;
        // Screen rect (mm) from the SVG's screen rect element. Kept in sync
        // with vcv/res/Loooop.svg and metamodule/Loooop_info.hh by
        // metamodule/sync_info_positions.py — run it after any panel change.
        display->box.pos = mm2px(Vec(1.500, 10.400));
        display->box.size = mm2px(Vec(190.040, 22.350));
        addChild(display);

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Top knob row: Speed / Pos / Jitter per head, with a CV jack under each.
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.467, 46.35)), module, Loooop::SPEED1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(24.88, 46.35)), module, Loooop::POSITION1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.293, 75.05)), module, Loooop::JITTER1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(87.227, 46.35)), module, Loooop::SPEED2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(72.64, 46.35)), module, Loooop::POSITION2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(58.053, 75.05)), module, Loooop::JITTER2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(134.99, 46.35)), module, Loooop::SPEED3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(120.4, 46.35)), module, Loooop::POSITION3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(105.81, 75.05)), module, Loooop::JITTER3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(182.75, 46.35)), module, Loooop::SPEED4_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(168.16, 46.35)), module, Loooop::POSITION4_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(153.57, 75.05)), module, Loooop::JITTER4_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.467, 58.7)), module, Loooop::SPEED1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.88, 58.7)), module, Loooop::POSITION1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.293, 87.4)), module, Loooop::JITTER1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(87.227, 58.7)), module, Loooop::SPEED2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(72.64, 58.7)), module, Loooop::POSITION2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58.053, 87.4)), module, Loooop::JITTER2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(134.99, 58.7)), module, Loooop::SPEED3_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(120.4, 58.7)), module, Loooop::POSITION3_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(105.81, 87.4)), module, Loooop::JITTER3_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(182.75, 58.7)), module, Loooop::SPEED4_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(168.16, 58.7)), module, Loooop::POSITION4_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(153.57, 87.4)), module, Loooop::JITTER4_CV_INPUT));

        // Lower knob row: Size / Level per head + centered global Dry/Wet, CV under each.
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.293, 46.35)), module, Loooop::SIZE1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.467, 75.05)), module, Loooop::LEVEL1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(58.053, 46.35)), module, Loooop::SIZE2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(87.227, 75.05)), module, Loooop::LEVEL2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(127.75, 116.05)), module, Loooop::DRYWET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(105.81, 46.35)), module, Loooop::SIZE3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(134.99, 75.05)), module, Loooop::LEVEL3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(153.57, 46.35)), module, Loooop::SIZE4_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(182.75, 75.05)), module, Loooop::LEVEL4_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.293, 58.7)), module, Loooop::SIZE1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.467, 87.4)), module, Loooop::LEVEL1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58.053, 58.7)), module, Loooop::SIZE2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(87.227, 87.4)), module, Loooop::LEVEL2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(140.1, 116.05)), module, Loooop::DRYWET_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(105.81, 58.7)), module, Loooop::SIZE3_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(134.99, 87.4)), module, Loooop::LEVEL3_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(153.57, 58.7)), module, Loooop::SIZE4_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(182.75, 87.4)), module, Loooop::LEVEL4_CV_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(24.88, 75.05)), module, Loooop::PAN1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(72.64, 75.05)), module, Loooop::PAN2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(120.4, 75.05)), module, Loooop::PAN3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(168.16, 75.05)), module, Loooop::PAN4_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.88, 87.4)), module, Loooop::PAN1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(72.64, 87.4)), module, Loooop::PAN2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(120.4, 87.4)), module, Loooop::PAN3_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(168.16, 87.4)), module, Loooop::PAN4_CV_INPUT));

        // Per-head jack row: Trig, Jump, Out L, Out R.
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.47, 102.1)), module, Loooop::TRIG1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.41, 102.1)), module, Loooop::JUMP1_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(30.97, 102.1)), module, Loooop::HEAD1_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.67, 102.1)), module, Loooop::HEAD1_R_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(56.23, 102.1)), module, Loooop::TRIG2_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(67.17, 102.1)), module, Loooop::JUMP2_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(78.73, 102.1)), module, Loooop::HEAD2_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(88.43, 102.1)), module, Loooop::HEAD2_R_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(103.99, 102.1)), module, Loooop::TRIG3_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(114.93, 102.1)), module, Loooop::JUMP3_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(126.49, 102.1)), module, Loooop::HEAD3_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(136.19, 102.1)), module, Loooop::HEAD3_R_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(151.75, 102.1)), module, Loooop::TRIG4_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(162.69, 102.1)), module, Loooop::JUMP4_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(174.25, 102.1)), module, Loooop::HEAD4_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(183.95, 102.1)), module, Loooop::HEAD4_R_OUTPUT));

        // Global bottom row: In L/R, Record (LED button) + trig, Clear + trig, Mix L/R.
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(16.854, 116.05)), module, Loooop::AUDIO_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(26.554, 116.05)), module, Loooop::AUDIO_R_INPUT));
        addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(53.687, 116.05)), module, Loooop::RECORD_PARAM, Loooop::RECORD_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(64.537, 116.05)), module, Loooop::RECORD_TRIG_INPUT));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(91.095, 116.05)), module, Loooop::CLEAR_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(101.95, 116.05)), module, Loooop::CLEAR_TRIG_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(166.49, 116.05)), module, Loooop::MIX_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(176.19, 116.05)), module, Loooop::MIX_R_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Loooop* m = dynamic_cast<Loooop*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolMenuItem("Overdub", "",
            [m] { return m->params[Loooop::OVERDUB_PARAM].getValue() > 0.5f; },
            [m](bool v) { m->paramQuantities[Loooop::OVERDUB_PARAM]->setValue(v ? 1.f : 0.f); }));
        menu->addChild(createBoolMenuItem("Crossfade loop seams", "",
            [m] { return m->params[Loooop::CROSSFADE_PARAM].getValue() < 0.5f; },
            [m](bool v) { m->paramQuantities[Loooop::CROSSFADE_PARAM]->setValue(v ? 0.f : 1.f); }));
        static const std::vector<std::string> kTrigModes = {"Loop start", "One-shot"};
        for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
            menu->addChild(createSubmenuItem("Head " + std::to_string(h + 1), "",
                [m, h](Menu* sub) {
                    sub->addChild(createIndexSubmenuItem("Trigger", kTrigModes,
                        [m, h] { return (int)std::round(
                            m->params[Loooop::TRIG_MODE1_PARAM + Loooop::HEAD_PARAMS * h].getValue()); },
                        [m, h](int v) {
                            m->paramQuantities[Loooop::TRIG_MODE1_PARAM + Loooop::HEAD_PARAMS * h]->setValue((float)v); }));
                    sub->addChild(createBoolMenuItem("Speed CV is V/Oct", "",
                        [m, h] { return m->params[Loooop::SPEED_VOCT1_PARAM + Loooop::HEAD_PARAMS * h].getValue() > 0.5f; },
                        [m, h](bool v) {
                            m->paramQuantities[Loooop::SPEED_VOCT1_PARAM + Loooop::HEAD_PARAMS * h]->setValue(v ? 1.f : 0.f); }));
                }));
        }
    }
};

Model* modelLoooop = createModel<Loooop, LoooopWidget>("Loooop");
