#include "plugin.hpp"
#include "MF20Filter.hpp"
#include "dsp_utils.hpp"
#include "engine.hpp"

struct MF20FilterModule : Module {
    enum ParamIds {
        CUTOFF_PARAM,           // LP cutoff (log2 Hz)
        RES_PARAM,              // LP resonance
        HP_CUTOFF_PARAM,        // HP cutoff (log2 Hz)
        HP_RES_PARAM,           // HP resonance
        DRIVE_PARAM,
        LP_CUTOFF_CV_PARAM,     // attenuverter for LP cutoff CV
        HP_CUTOFF_CV_PARAM,     // attenuverter for HP cutoff CV
        TOTAL_CUTOFF_CV_PARAM,  // attenuverter for Total cutoff CV (both filters)
        NUM_PARAMS
    };
    enum InputIds {
        AUDIO_INPUT,
        AUDIO_INPUT_R,
        LP_CUTOFF_INPUT,
        HP_CUTOFF_INPUT,
        TOTAL_CUTOFF_INPUT,     // sweeps both LP and HP cutoff (MS-20 "Total")
        NUM_INPUTS
    };
    enum OutputIds {
        LP_OUTPUT,
        LP_OUTPUT_R,
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    // Per-voice DSP state (up to 16 voices).
    EnginePool _pool;

    // Cutoff clamp floor (Hz). The knob's own range floors at 20 Hz (configParam),
    // but CV-modulated cutoff may reach the filter core's low limit, so deep
    // negative CV can nearly close the filter like an MS-20's "Total" sweep.
    static constexpr float kCutoffFloorHz = 1.f;

    // Rate divider — modulate() runs every ~2.5 ms instead of every sample.
    int _modulationSteps = 100;
    int _steps = 100;  // ≥ _modulationSteps: first process() modulates immediately (saved K35 mode, g targets)

    // Shared modulation targets (same for all voices; per-voice CV added in modulate()).
    float _drive = 1.f;
    float _sampleRate = 44100.f;     // mirrors the engine rate for modulate-rate math

    // Drive smoothing: targets computed once per modulate block, slewed
    // per-sample (5 ms) so Drive sweeps don't zipper. Two smoothers so the
    // audio path needs no sqrt or divide: √drive feeds the OTA pre-gain,
    // 1/√drive feeds the diode clip character.
    OnePoleSmoother _driveSqrtSlew   { 1.f };
    OnePoleSmoother _clipThreshSlew  { 1.f };
    float _driveSqrtTarget  = 1.f;
    float _clipThreshTarget = 1.f;
    float _driveSqrt = 1.f;   // current smoothed value, used by otaPreGain
    float _clipThresh = 1.f;
    // Resonance retention [0,1]: pulls the clip-threshold target back from
    // 1/√drive toward 1.0, so Drive stops squashing the resonant peak.
    // 0 = original behavior (full squash); 1 = clip threshold ignores Drive.
    // See docs/superpowers/specs/2026-07-18-mf20-resonance-retention-design.md.
    float _resRetention = 0.f;
    // Deterministic denormal-prevention dither: alternates sign each sample.
    // Replaces the RNG dither — cheaper, and bit-reproducible between the
    // VCV and MetaModule builds (relevant to headless comparison testing).
    float _dither = 1e-9f;

    MF20Filter::Mode _filterMode = MF20Filter::Mode::OTA;

    MF20FilterModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
                    "LP Cutoff", " Hz", 2.f);
        configParam(RES_PARAM, 0.f, 1.f, 0.25f, "LP Resonance", "%", 0.f, 100.f);
        configParam(HP_CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(120.f),
                    "HP Cutoff", " Hz", 2.f);
        configParam(HP_RES_PARAM, 0.f, 1.f, 0.25f, "HP Resonance", "%", 0.f, 100.f);
        configParam(DRIVE_PARAM, 1.f, 8.f, 1.f, "Drive", "x");
        configParam(LP_CUTOFF_CV_PARAM,    -1.f, 1.f, 1.f, "LP Cutoff CV", "x");
        configParam(HP_CUTOFF_CV_PARAM,    -1.f, 1.f, 1.f, "HP Cutoff CV", "x");
        configParam(TOTAL_CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "Total Cutoff CV (both filters)", "x");

        configInput(AUDIO_INPUT,   "Audio L");
        configInput(AUDIO_INPUT_R, "Audio R");
        configInput(LP_CUTOFF_INPUT,    "LP Cutoff CV");
        configInput(HP_CUTOFF_INPUT,    "HP Cutoff CV");
        configInput(TOTAL_CUTOFF_INPUT, "Total Cutoff CV (sweeps both filters)");

        configOutput(LP_OUTPUT,   "Audio L");
        configOutput(LP_OUTPUT_R, "Audio R");

        configBypass(AUDIO_INPUT,   LP_OUTPUT);
        configBypass(AUDIO_INPUT_R, LP_OUTPUT_R);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "_filterMode",
            json_integer(_filterMode == MF20Filter::Mode::K35 ? 1 : 0));
        json_object_set_new(root, "resRetention", json_real(_resRetention));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* m = json_object_get(root, "_filterMode");
        if (m)
            _filterMode = json_integer_value(m) == 1
                       ? MF20Filter::Mode::K35
                       : MF20Filter::Mode::OTA;
        json_t* rr = json_object_get(root, "resRetention");
        if (rr)
            _resRetention = clamp((float)json_real_value(rr), 0.f, 1.f);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        _sampleRate = e.sampleRate;
        _pool.setSampleRate(e.sampleRate);
        float alpha = smootherAlpha(e.sampleRate, 0.005f);
        for (auto& eng : _pool.engines) {
            eng.lpGSlew.setAlpha(alpha);
            eng.hpGSlew.setAlpha(alpha);
            eng.lpResSlew.setAlpha(alpha);
            eng.hpResSlew.setAlpha(alpha);
        }
        _driveSqrtSlew.setAlpha(alpha);
        _clipThreshSlew.setAlpha(alpha);
        _modulationSteps = static_cast<int>(e.sampleRate * 0.0025f);  // 2.5 ms
        _steps = _modulationSteps;
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        _pool.resetAll();
    }

    // Called at ~2.5 ms intervals. Reads params/CVs and updates smoother targets
    // for each active voice. Per-voice CV inputs use getPolyVoltage(c).
    void modulate() {
        float drive = params[DRIVE_PARAM].getValue();
        _drive = drive;
        _driveSqrtTarget  = std::sqrt(drive);
        // Retention lerps the clip threshold from 1/√drive (r=0, full squash)
        // toward 1.0 (r=1, threshold ignores Drive → resonance preserved).
        // Feeds OTA's feedback diode and K35's forward-clip pre-gain alike.
        float r = clamp(_resRetention, 0.f, 1.f);
        _clipThreshTarget = (1.f - r) * (1.f / _driveSqrtTarget) + r;

        float cutoffLog = params[CUTOFF_PARAM].getValue();
        float hpLog     = params[HP_CUTOFF_PARAM].getValue();
        float res       = clamp(params[RES_PARAM].getValue(), 0.f, 1.f);
        float hpResRaw  = clamp(params[HP_RES_PARAM].getValue(), 0.f, 1.f);
        float lpCvAtten    = params[LP_CUTOFF_CV_PARAM].getValue();
        float hpCvAtten    = params[HP_CUTOFF_CV_PARAM].getValue();
        float totalCvAtten = params[TOTAL_CUTOFF_CV_PARAM].getValue();

        bool lpCvConn    = inputs[LP_CUTOFF_INPUT].isConnected();
        bool hpCvConn    = inputs[HP_CUTOFF_INPUT].isConnected();
        bool totalCvConn = inputs[TOTAL_CUTOFF_INPUT].isConnected();

        for (int c = 0; c < _pool.activeVoices; c++) {
            VoiceEngine& eng = _pool.engines[c];
            eng.sanitize();

            // Total cutoff CV is an octave offset (1 V/oct) added to BOTH filters,
            // preserving the knob spread — the MS-20 "Total" cutoff-modulation bus.
            float totalOffset = 0.f;
            if (totalCvConn)
                totalOffset = totalCvAtten * inputs[TOTAL_CUTOFF_INPUT].getPolyVoltage(c);

            // LP cutoff target (log2 Hz): knob + Total + per-filter CV.
            float voiceCutoffLog = cutoffLog + totalOffset;
            if (lpCvConn)
                voiceCutoffLog += lpCvAtten * inputs[LP_CUTOFF_INPUT].getPolyVoltage(c);
            float lpHz = clamp(std::exp2(voiceCutoffLog), kCutoffFloorHz, _sampleRate * 0.498f);
            eng.lpGTarget = MF20Filter::cutoffToG(lpHz, _sampleRate);

            // HP cutoff target (log2 Hz): knob + Total + per-filter CV.
            float voiceHpLog = hpLog + totalOffset;
            if (hpCvConn)
                voiceHpLog += hpCvAtten * inputs[HP_CUTOFF_INPUT].getPolyVoltage(c);
            float hpHz = clamp(std::exp2(voiceHpLog), kCutoffFloorHz, _sampleRate * 0.498f);
            eng.hpGTarget = MF20Filter::cutoffToG(hpHz, _sampleRate);

            // Resonance targets — knob only (the MS-20 has no resonance modulation).
            eng.lpResTarget = res;
            eng.hpResTarget = hpResRaw;

            // Update filter mode and drive for this voice.
            eng.lpFilter.setMode(_filterMode);
            eng.hpFilter.setMode(_filterMode);
            eng.lpFilterR.setMode(_filterMode);
            eng.hpFilterR.setMode(_filterMode);
        }
    }

    // Process one voice (channel c). Advances its smoothers and runs the audio cascade.
    void processChannel(const ProcessArgs& args, int c) {
        VoiceEngine& eng = _pool.engines[c];

        eng.hpFilter.setDriveCharacterFromThreshold(_clipThresh);
        eng.lpFilter.setDriveCharacterFromThreshold(_clipThresh);

        // OTA mode: piecewise-linear pre-gain — amplifies by √drive (smoothed
        //   in process() into _driveSqrt), soft-clips at ±5 V.
        //   Unity gain at drive=1; continuous, monotonic for all drive values.
        //   NOTE: In OTA mode Drive is applied TWICE and intentionally so — here as
        //   input level, and inside the filter via setDriveCharacterFromThreshold()
        //   above, which lowers the diode clip threshold and steepens its slope.
        //   K35 mode does all of this in processK35()'s forward-path clip, so no
        //   pre-gain is applied.
        auto otaPreGain = [&](float x) {
            if (_filterMode != MF20Filter::Mode::OTA) return x;
            float d = x * _driveSqrt;
            return (d >  5.f) ?  5.f + 0.25f * (d - 5.f)
                 : (d < -5.f) ? -5.f + 0.25f * (d + 5.f)
                 : d;
        };

        float in = inputs[AUDIO_INPUT].getPolyVoltage(c);
        in += _dither;
        in = otaPreGain(in);

        // Advance slew smoothers one step toward their targets.
        float gLp      = eng.lpGSlew.process(eng.lpGTarget);
        float gHp      = eng.hpGSlew.process(eng.hpGTarget);
        float res      = eng.lpResSlew.process(eng.lpResTarget);
        float hpResRaw = eng.hpResSlew.process(eng.hpResTarget);

        res = resTaper(res);
        float hpRes = resTaper(hpResRaw);

        auto hpStage = eng.hpFilter.processVCVG(in,         gHp, hpRes);
        auto lpStage = eng.lpFilter.processVCVG(hpStage.hp, gLp, res);
        outputs[LP_OUTPUT].setVoltage(lpStage.lp, c);

        if (inputs[AUDIO_INPUT_R].isConnected()) {
            // True stereo: process R through its own filter pair.
            eng.hpFilterR.setDriveCharacterFromThreshold(_clipThresh);
            eng.lpFilterR.setDriveCharacterFromThreshold(_clipThresh);
            float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c);
            inR += _dither;
            inR = otaPreGain(inR);
            auto hpStageR = eng.hpFilterR.processVCVG(inR,          gHp, hpRes);
            auto lpStageR = eng.lpFilterR.processVCVG(hpStageR.hp,  gLp, res);
            outputs[LP_OUTPUT_R].setVoltage(lpStageR.lp, c);
        } else {
            // R input is normalled to L → an identical filter pass. Mirror L instead
            // of recomputing it (skips a full HP+LP solve per voice in mono patches).
            outputs[LP_OUTPUT_R].setVoltage(lpStage.lp, c);
        }
    }

    void process(const ProcessArgs& args) override {
        int voices = std::max({1, inputs[AUDIO_INPUT].getChannels(), inputs[AUDIO_INPUT_R].getChannels()});
        _pool.setVoices(voices);

        outputs[LP_OUTPUT].setChannels(voices);
        outputs[LP_OUTPUT_R].setChannels(voices);

        if (++_steps >= _modulationSteps) {
            _steps = 0;
            modulate();
        }

        _dither = -_dither;
        _driveSqrt  = _driveSqrtSlew.process(_driveSqrtTarget);
        _clipThresh = _clipThreshSlew.process(_clipThreshTarget);
        for (int c = 0; c < voices; c++)
            processChannel(args, c);
    }
};



// "Resonance retention" menu control (0-100 %). Stores 0..1 in the module;
// the Quantity presents it as a percentage. VCV desktop gets a ui::Slider;
// MetaModule (no ui::Slider) gets a discrete submenu — same pattern as
// Vespid's InputTrim/OutputLevel and Particules' manual-gain menus.
struct ResRetentionQuantity : Quantity {
    MF20FilterModule* module;
    ResRetentionQuantity(MF20FilterModule* m) : module(m) {}
    void setValue(float value) override {
        if (module)
            module->_resRetention = clamp(value, getMinValue(), getMaxValue()) / 100.f;
    }
    float getValue() override { return module ? module->_resRetention * 100.f : getDefaultValue(); }
    float getMinValue() override { return 0.f; }
    float getMaxValue() override { return 100.f; }
    float getDefaultValue() override { return 0.f; }
    std::string getLabel() override { return "Resonance retention"; }
    std::string getUnit() override { return " %"; }
    std::string getDisplayValueString() override { return string::f("%.0f", getValue()); }
};

#ifndef METAMODULE
struct ResRetentionSlider : ui::Slider {
    ResRetentionSlider(ResRetentionQuantity* q) {
        quantity = q;
        box.size.x = 200.f;
    }
    ~ResRetentionSlider() { delete quantity; }
};
#endif

struct MF20FilterWidget : ModuleWidget {
    MF20FilterWidget(MF20FilterModule* module) {

        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/MF20Filter.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Positions generated by helper.py from res/MF20Filter.svg (vcv-panel-gen).
        addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(11.80, 30.997)), module, MF20FilterModule::CUTOFF_PARAM));
        addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(39.00, 30.997)), module, MF20FilterModule::HP_CUTOFF_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(11.80, 46.997)), module, MF20FilterModule::LP_CUTOFF_CV_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.80, 59.347)), module, MF20FilterModule::LP_CUTOFF_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(25.40, 46.997)), module, MF20FilterModule::TOTAL_CUTOFF_CV_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.40, 59.347)), module, MF20FilterModule::TOTAL_CUTOFF_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(39.00, 46.997)), module, MF20FilterModule::HP_CUTOFF_CV_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.00, 59.347)), module, MF20FilterModule::HP_CUTOFF_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.80, 74.857)), module, MF20FilterModule::RES_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.00, 74.857)), module, MF20FilterModule::HP_RES_PARAM));
        addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 91.017)), module, MF20FilterModule::DRIVE_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.35, 113.69)), module, MF20FilterModule::AUDIO_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.05, 113.69)), module, MF20FilterModule::AUDIO_INPUT_R));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.75, 113.69)), module, MF20FilterModule::LP_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.45, 113.69)), module, MF20FilterModule::LP_OUTPUT_R));
    }

    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<MF20FilterModule*>(module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Filter revision"));
        menu->addChild(createMenuItem("OTA (revised MS-20)",
            m->_filterMode == MF20Filter::Mode::OTA ? "✓" : "",
            [m]() { m->_filterMode = MF20Filter::Mode::OTA; }));
        menu->addChild(createMenuItem("Korg35 (original MS-20)",
            m->_filterMode == MF20Filter::Mode::K35 ? "✓" : "",
            [m]() { m->_filterMode = MF20Filter::Mode::K35; }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Drive resonance retention"));
#ifdef METAMODULE
        // MetaModule has no ui::Slider; offer a discrete list snapping to the
        // nearest preset (same precedent as Vespid's #ifdef METAMODULE menus).
        menu->addChild(createIndexSubmenuItem("Resonance retention",
            {"0 %", "25 %", "50 %", "75 %", "100 %"},
            [m]() -> size_t {
                static const float kValues[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
                size_t best = 0;
                float bestDiff = std::fabs(kValues[0] - m->_resRetention);
                for (size_t i = 0; i < 5; i++) {
                    float diff = std::fabs(kValues[i] - m->_resRetention);
                    if (diff < bestDiff) { bestDiff = diff; best = i; }
                }
                return best;
            },
            [m](size_t i) {
                static const float kValues[5] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
                m->_resRetention = kValues[i];
            }));
#else
        menu->addChild(new ResRetentionSlider(new ResRetentionQuantity(m)));
#endif
    }
};

Model* modelMF20Filter = createModel<MF20FilterModule, MF20FilterWidget>("MF20Filter");
