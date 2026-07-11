#include "plugin.hpp"
#include "dsp/LoopEngine.hpp"
#include "LoopDisplay.hpp"
#include "LooperModuleDSP.hpp"
#include <cmath>
#include <array>
#include <vector>
#include <string>

// Playhead display names follow the head colors on the panel/display
// (LoopWaveformRenderer::HEAD_COLORS): H1 red, H2 green, H3 blue, H4 yellow.
static const std::string kHeadNames[LoopEngine::NUM_HEADS] = {
    "Red playhead", "Green playhead", "Blue playhead", "Yellow playhead"};

struct Loooop : Module {
    // Global params and jacks come FIRST (so the MetaModule manual lists them
    // at the top), then params and inputs grouped PER HEAD so the MetaModule
    // mapping menu lists everything for one head together. This mirrors the
    // Elem order in metamodule/loooop/Loooop_info.hh, except that build has an
    // extra menu-only WriteModeAlt at the end of its globals, so ids after the
    // globals are offset by one there. Each head is a contiguous block of
    // HEAD_PARAMS params / HEAD_INPUTS inputs — index a head with
    // `X1_PARAM + HEAD_PARAMS * h`.
    enum ParamId { RECORD_PARAM, OVERDUB_PARAM, CLEAR_PARAM, GRID_PARAM, DRYWET_PARAM, CROSSFADE_PARAM,
                   SIZE1_PARAM, POSITION1_PARAM, SPEED1_PARAM, JITTER1_PARAM, PAN1_PARAM, LEVEL1_PARAM, TRIG_MODE1_PARAM, SPEED_VOCT1_PARAM, EXCLUDE_GRID1_PARAM,
                   SIZE2_PARAM, POSITION2_PARAM, SPEED2_PARAM, JITTER2_PARAM, PAN2_PARAM, LEVEL2_PARAM, TRIG_MODE2_PARAM, SPEED_VOCT2_PARAM, EXCLUDE_GRID2_PARAM,
                   SIZE3_PARAM, POSITION3_PARAM, SPEED3_PARAM, JITTER3_PARAM, PAN3_PARAM, LEVEL3_PARAM, TRIG_MODE3_PARAM, SPEED_VOCT3_PARAM, EXCLUDE_GRID3_PARAM,
                   SIZE4_PARAM, POSITION4_PARAM, SPEED4_PARAM, JITTER4_PARAM, PAN4_PARAM, LEVEL4_PARAM, TRIG_MODE4_PARAM, SPEED_VOCT4_PARAM, EXCLUDE_GRID4_PARAM,
                   PARAMS_LEN };
    enum InputId { AUDIO_L_INPUT, AUDIO_R_INPUT, RECORD_TRIG_INPUT, CLEAR_TRIG_INPUT, DRYWET_CV_INPUT,
                   SIZE1_CV_INPUT, POSITION1_CV_INPUT, SPEED1_CV_INPUT, JITTER1_CV_INPUT, PAN1_CV_INPUT, LEVEL1_CV_INPUT, TRIG1_INPUT, JUMP1_INPUT,
                   SIZE2_CV_INPUT, POSITION2_CV_INPUT, SPEED2_CV_INPUT, JITTER2_CV_INPUT, PAN2_CV_INPUT, LEVEL2_CV_INPUT, TRIG2_INPUT, JUMP2_INPUT,
                   SIZE3_CV_INPUT, POSITION3_CV_INPUT, SPEED3_CV_INPUT, JITTER3_CV_INPUT, PAN3_CV_INPUT, LEVEL3_CV_INPUT, TRIG3_INPUT, JUMP3_INPUT,
                   SIZE4_CV_INPUT, POSITION4_CV_INPUT, SPEED4_CV_INPUT, JITTER4_CV_INPUT, PAN4_CV_INPUT, LEVEL4_CV_INPUT, TRIG4_INPUT, JUMP4_INPUT,
                   INPUTS_LEN };
    static constexpr int HEAD_PARAMS = 9;   // per-head param stride: Size,Pos,Speed,Jitter,Pan,Level,TrigMode,SpeedVoct,ExcludeGrid
    static constexpr int HEAD_INPUTS = 8;   // per-head input stride: SizeCV,PosCV,SpeedCV,JitterCV,PanCV,LevelCV,Trig,Jump
    enum OutputId { MIX_L_OUTPUT, MIX_R_OUTPUT,
                    HEAD1_L_OUTPUT, HEAD1_R_OUTPUT, HEAD2_L_OUTPUT, HEAD2_R_OUTPUT,
                    HEAD3_L_OUTPUT, HEAD3_R_OUTPUT, HEAD4_L_OUTPUT, HEAD4_R_OUTPUT,
                    OUTPUTS_LEN };
    enum LightId { RECORD_LIGHT, OVERDUB_R_LIGHT, OVERDUB_G_LIGHT, OVERDUB_B_LIGHT, LIGHTS_LEN };

    LoopEngine engine;
    dsp::SchmittTrigger recordTrig, recordBtn, clearBtn, clearTrig, headTrig[LoopEngine::NUM_HEADS];
    float lastJumpV[LoopEngine::NUM_HEADS] = {};
    loooop::OnePoleSmoother panSm[LoopEngine::NUM_HEADS];
    loooop::OnePoleSmoother mixSm{1.f, 1.f};   // value matches DRYWET default
    float smootherRate = 0.f;

    Loooop() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
            const std::string& n = kHeadNames[h];
            configParam(SPEED1_PARAM + HEAD_PARAMS * h, -2.f, 2.f, 1.f, n + " speed");
            configParam(POSITION1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.5f, n + " position");
            configParam(SIZE1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 1.f, n + " size");
            // Level defaults to 0.25: four phase-locked heads at defaults sum to
            // unity, matching the old single-head loudness.
            configParam(LEVEL1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.25f, n + " level");
            configParam(JITTER1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f, n + " jitter");
            // 0 = restart-at-window-start (unnamed default), 1 = one-shot.
            // Menu switches opt out of Randomize: a randomized one-shot with
            // no trigger patched silences the head with nothing on the panel
            // to show why.
            configSwitch(TRIG_MODE1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f, n + " one-shot",
                {"Off", "On"})->randomizeEnabled = false;
            configSwitch(SPEED_VOCT1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f, n + " speed CV V/Oct",
                {"Off", "On"})->randomizeEnabled = false;
            configSwitch(EXCLUDE_GRID1_PARAM + HEAD_PARAMS * h, 0.f, 1.f, 0.f,
                n + " exclude from Grid", {"Off", "On"})->randomizeEnabled = false;
            configInput(SPEED1_CV_INPUT + HEAD_INPUTS * h, n + " speed CV");
            configInput(POSITION1_CV_INPUT + HEAD_INPUTS * h, n + " position CV");
            configInput(SIZE1_CV_INPUT + HEAD_INPUTS * h, n + " size CV");
            configInput(LEVEL1_CV_INPUT + HEAD_INPUTS * h, n + " level CV");
            configInput(JITTER1_CV_INPUT + HEAD_INPUTS * h, n + " jitter CV");
            configParam(PAN1_PARAM + HEAD_PARAMS * h, -1.f, 1.f, 0.f, n + " pan");
            configInput(PAN1_CV_INPUT + HEAD_INPUTS * h, n + " pan CV");
            configInput(TRIG1_INPUT + HEAD_INPUTS * h, n + " trigger");
            configInput(JUMP1_INPUT + HEAD_INPUTS * h, n + " jump");
            configOutput(HEAD1_L_OUTPUT + 2 * h, n + " left");
            configOutput(HEAD1_L_OUTPUT + 2 * h + 1, n + " right");
        }
        configParam(DRYWET_PARAM, 0.f, 1.f, 1.f, "Mix dry/wet");
        configButton(RECORD_PARAM, "Record/Overdub");
        configButton(CLEAR_PARAM, "Clear");
        // Mode switches opt out of Randomize like the per-head menu switches:
        // Lock overdub gates the Record button, and randomized Grid/Crossfade
        // read as broken behavior, not as an inspiring patch variation.
        configSwitch(OVERDUB_PARAM, 0.f, 4.f, 0.f, "Overdub",
            {"Layer", "Decay", "Add", "Replace", "Lock"})->randomizeEnabled = false;
        // Value 0 = On (default): kept inverted to match the MetaModule alt-param,
        // whose loader zero-inits unset params, so 0 must mean crossfade-on.
        configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade", {"On", "Off"})->randomizeEnabled = false;
        configSwitch(GRID_PARAM, 0.f, 5.f, 0.f, "Grid",
            {"Off", "4", "8", "16", "32", "64"})->randomizeEnabled = false;
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
        engine.setSampleRate(e.sampleRate);
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        engine.clear();   // Initialize should silence the loop too (audio-safe; bumps the display revision)
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != smootherRate) {
            smootherRate = args.sampleRate;
            const float a = loooop::smootherAlpha(smootherRate, 0.002f);
            for (auto& s : panSm) s.alpha = a;
            mixSm.alpha = a;
        }
        // Overdub is one 5-state control: four write modes + Lock (= overdub
        // off, loop untouchable). While Locked the last write mode stays set;
        // the engine ignores it with overdub off.
        static constexpr LoopEngine::WriteMode kOverdubModes[4] = {
            LoopEngine::WriteMode::Layer, LoopEngine::WriteMode::Decay,
            LoopEngine::WriteMode::Add,   LoopEngine::WriteMode::Replace};
        int od = (int)std::round(params[OVERDUB_PARAM].getValue());
        engine.setOverdub(od != 4);   // 4 = Lock
        if (od >= 0 && od < 4)
            engine.setWriteMode(kOverdubModes[od]);
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
        for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
            float spKnob = params[SPEED1_PARAM + HEAD_PARAMS * h].getValue();
            float spCv = inputs[SPEED1_CV_INPUT + HEAD_INPUTS * h].getVoltage();
            engine.setSpeed(h, params[SPEED_VOCT1_PARAM + HEAD_PARAMS * h].getValue() > 0.5f
                ? loooop::speedFromVOct(spKnob, spCv)
                : loooop::speedFromControls(spKnob, spCv));
            engine.setPosition(h, loooop::normalizedControl(
                params[POSITION1_PARAM + HEAD_PARAMS * h].getValue(),
                inputs[POSITION1_CV_INPUT + HEAD_INPUTS * h].getVoltage()));
            engine.setSize(h, loooop::normalizedControl(
                params[SIZE1_PARAM + HEAD_PARAMS * h].getValue(),
                inputs[SIZE1_CV_INPUT + HEAD_INPUTS * h].getVoltage()));
            engine.setLevel(h, loooop::normalizedControl(
                params[LEVEL1_PARAM + HEAD_PARAMS * h].getValue(),
                inputs[LEVEL1_CV_INPUT + HEAD_INPUTS * h].getVoltage()));
            engine.setJitter(h, loooop::normalizedControl(
                params[JITTER1_PARAM + HEAD_PARAMS * h].getValue(),
                inputs[JITTER1_CV_INPUT + HEAD_INPUTS * h].getVoltage()));
            engine.setGridExclude(h,
                params[EXCLUDE_GRID1_PARAM + HEAD_PARAMS * h].getValue() > 0.5f);

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
        const auto in = loooop::normalledStereo(
            inputs[AUDIO_L_INPUT].isConnected(), inputs[AUDIO_L_INPUT].getVoltage(),
            inputs[AUDIO_R_INPUT].isConnected(), inputs[AUDIO_R_INPUT].getVoltage());
        const float inL = in.l;
        const float inR = in.r;

        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        engine.process(inL / 5.f, inR / 5.f, hs);   // ±5V <-> ±1
        float wetL = 0.f, wetR = 0.f;
        for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
            outputs[HEAD1_L_OUTPUT + 2 * h].setVoltage(hs[h].l * 5.f);
            outputs[HEAD1_L_OUTPUT + 2 * h + 1].setVoltage(hs[h].r * 5.f);
            // Pan is a balance on each head's contribution to the mix only;
            // the individual head outputs above are unaffected. Center = unity.
            const float pan = panSm[h].process(loooop::panControl(
                params[PAN1_PARAM + HEAD_PARAMS * h].getValue(),
                inputs[PAN1_CV_INPUT + HEAD_INPUTS * h].getVoltage()));
            const float gL = loooop::panLeftGain(pan);
            const float gR = loooop::panRightGain(pan);
            wetL += hs[h].l * gL; wetR += hs[h].r * gR;
        }
        const float w = mixSm.process(loooop::normalizedControl(
            params[DRYWET_PARAM].getValue(), inputs[DRYWET_CV_INPUT].getVoltage()));
        outputs[MIX_L_OUTPUT].setVoltage(loooop::dryWet(inL / 5.f, wetL, w) * 5.f);
        outputs[MIX_R_OUTPUT].setVoltage(loooop::dryWet(inR / 5.f, wetR, w) * 5.f);
        lights[RECORD_LIGHT].setBrightness(engine.isRecording() ? 1.f : 0.f);
        // Overdub state color (Layer/Decay/Add/Replace/Lock), Quality-button
        // style: an RGB LED in the bezel driven from a color table.
        static constexpr float kOverdubColors[5][3] = {
            {0.247f, 0.549f, 1.f},      // Layer   - blue   #3f8cff
            {1.f,    0.624f, 0.039f},   // Decay   - amber  #ff9f0a
            {0.188f, 0.820f, 0.345f},   // Add     - green  #30d158
            {1.f,    0.231f, 0.188f},   // Replace - red    #ff3b30
            {0.749f, 0.353f, 0.949f},   // Lock    - purple #bf5af2
        };
        lights[OVERDUB_R_LIGHT].setBrightness(kOverdubColors[od][0]);
        lights[OVERDUB_G_LIGHT].setBrightness(kOverdubColors[od][1]);
        lights[OVERDUB_B_LIGHT].setBrightness(kOverdubColors[od][2]);
    }
};

// Five-state overdub button, Quality-button style (see Particules): the
// stock light bezel with an RGB LED, made non-momentary so a click cycles
// the stepped param Layer/Decay/Add/Replace/Lock with wraparound. The
// module drives the LED color from the state in process().
struct OverdubButton : VCVLightBezel<RedGreenBlueLight> {
    OverdubButton() {
        momentary = false;
    }
};

// Rack ships no small snap knob; the Grid selector wants the small body so
// its printed value ring can sit close in.
struct RoundSmallBlackSnapKnob : RoundSmallBlackKnob {
    RoundSmallBlackSnapKnob() {
        snap = true;
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

        // Per-head knobs and their CV jacks. On the panel each head is a
        // block: Size/Position/Speed on the upper knob row, Jitter/Pan/Level on
        // the lower, with a CV jack directly under each knob. These calls are
        // grouped by control type across heads, not by head — addParam/addInput
        // order is cosmetic and doesn't affect param ids. This block adds the
        // Speed/Position/Jitter knobs and CV jacks.
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

        // The remaining per-head knobs and CV jacks (Size/Level, then Pan)
        // plus the centered global Dry/Wet knob and its CV.
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.293, 46.35)), module, Loooop::SIZE1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.467, 75.05)), module, Loooop::LEVEL1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(58.053, 46.35)), module, Loooop::SIZE2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(87.227, 75.05)), module, Loooop::LEVEL2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(143.49, 116.05)), module, Loooop::DRYWET_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(105.81, 46.35)), module, Loooop::SIZE3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(134.99, 75.05)), module, Loooop::LEVEL3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(153.57, 46.35)), module, Loooop::SIZE4_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(182.75, 75.05)), module, Loooop::LEVEL4_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.293, 58.7)), module, Loooop::SIZE1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.467, 87.4)), module, Loooop::LEVEL1_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58.053, 58.7)), module, Loooop::SIZE2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(87.227, 87.4)), module, Loooop::LEVEL2_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(155.84, 116.05)), module, Loooop::DRYWET_CV_INPUT));
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
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.35, 102.1)), module, Loooop::TRIG1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.03, 102.1)), module, Loooop::JUMP1_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.71, 102.1)), module, Loooop::HEAD1_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.41, 102.1)), module, Loooop::HEAD1_R_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(55.11, 102.1)), module, Loooop::TRIG2_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(67.79, 102.1)), module, Loooop::JUMP2_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(80.47, 102.1)), module, Loooop::HEAD2_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(90.17, 102.1)), module, Loooop::HEAD2_R_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(102.87, 102.1)), module, Loooop::TRIG3_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(115.55, 102.1)), module, Loooop::JUMP3_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(128.23, 102.1)), module, Loooop::HEAD3_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(137.93, 102.1)), module, Loooop::HEAD3_R_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(150.63, 102.1)), module, Loooop::TRIG4_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(163.31, 102.1)), module, Loooop::JUMP4_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(175.99, 102.1)), module, Loooop::HEAD4_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(185.69, 102.1)), module, Loooop::HEAD4_R_OUTPUT));

        // Global bottom row: In L/R, Record (LED button) + trig, Clear + trig, Mix L/R.
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.35, 116.05)), module, Loooop::AUDIO_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.05, 116.05)), module, Loooop::AUDIO_R_INPUT));
        addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(36.452, 116.05)), module, Loooop::RECORD_PARAM, Loooop::RECORD_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(47.302, 116.05)), module, Loooop::RECORD_TRIG_INPUT));
        addParam(createLightParamCentered<OverdubButton>(mm2px(Vec(69.543, 116.05)), module, Loooop::OVERDUB_PARAM, Loooop::OVERDUB_R_LIGHT));
        addParam(createParamCentered<RoundSmallBlackSnapKnob>(mm2px(Vec(122.69, 116.05)), module, Loooop::GRID_PARAM));
        addParam(createParamCentered<VCVButton>(mm2px(Vec(91.785, 116.05)), module, Loooop::CLEAR_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(102.64, 116.05)), module, Loooop::CLEAR_TRIG_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(175.99, 116.05)), module, Loooop::MIX_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(185.69, 116.05)), module, Loooop::MIX_R_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Loooop* m = dynamic_cast<Loooop*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolMenuItem("Crossfade loop seams", "",
            [m] { return m->params[Loooop::CROSSFADE_PARAM].getValue() < 0.5f; },
            [m](bool v) { m->paramQuantities[Loooop::CROSSFADE_PARAM]->setValue(v ? 0.f : 1.f); }));
        // Commands at the top level, playheads (by color) in the submenus.
        // One-shot is a checkmark per playhead; unchecked (default) a trigger
        // restarts the playhead at its window start — that mode has no name
        // in the interface.
        menu->addChild(createSubmenuItem("One-shot", "", [m](Menu* sub) {
            for (int h = 0; h < LoopEngine::NUM_HEADS; ++h)
                sub->addChild(createBoolMenuItem(kHeadNames[h], "",
                    [m, h] { return m->params[Loooop::TRIG_MODE1_PARAM + Loooop::HEAD_PARAMS * h].getValue() > 0.5f; },
                    [m, h](bool v) {
                        m->paramQuantities[Loooop::TRIG_MODE1_PARAM + Loooop::HEAD_PARAMS * h]->setValue(v ? 1.f : 0.f); }));
        }));
        menu->addChild(createSubmenuItem("Speed CV is V/Oct", "", [m](Menu* sub) {
            for (int h = 0; h < LoopEngine::NUM_HEADS; ++h)
                sub->addChild(createBoolMenuItem(kHeadNames[h], "",
                    [m, h] { return m->params[Loooop::SPEED_VOCT1_PARAM + Loooop::HEAD_PARAMS * h].getValue() > 0.5f; },
                    [m, h](bool v) {
                        m->paramQuantities[Loooop::SPEED_VOCT1_PARAM + Loooop::HEAD_PARAMS * h]->setValue(v ? 1.f : 0.f); }));
        }));
        menu->addChild(createSubmenuItem("Exclude from Grid", "", [m](Menu* sub) {
            for (int h = 0; h < LoopEngine::NUM_HEADS; ++h)
                sub->addChild(createBoolMenuItem(kHeadNames[h], "",
                    [m, h] { return m->params[Loooop::EXCLUDE_GRID1_PARAM + Loooop::HEAD_PARAMS * h].getValue() > 0.5f; },
                    [m, h](bool v) {
                        m->paramQuantities[Loooop::EXCLUDE_GRID1_PARAM + Loooop::HEAD_PARAMS * h]->setValue(v ? 1.f : 0.f); }));
        }));
    }
};

Model* modelLoooop = createModel<Loooop, LoooopWidget>("Loooop");
