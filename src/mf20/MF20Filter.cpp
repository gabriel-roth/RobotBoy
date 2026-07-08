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

    // Rate divider — modulate() runs every ~2.5 ms instead of every sample.
    int _modulationSteps = 100;
    int _steps = -1;

    // Shared modulation target (same for all voices; per-voice CV added in modulate()).
    float _drive = 1.f;

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
        configInput(AUDIO_INPUT_R, "Audio R (normalled to L)");
        configInput(LP_CUTOFF_INPUT,    "LP Cutoff CV");
        configInput(HP_CUTOFF_INPUT,    "HP Cutoff CV");
        configInput(TOTAL_CUTOFF_INPUT, "Total Cutoff CV (sweeps both filters)");

        configOutput(LP_OUTPUT,   "Lowpass L");
        configOutput(LP_OUTPUT_R, "Lowpass R");

        configBypass(AUDIO_INPUT,   LP_OUTPUT);
        configBypass(AUDIO_INPUT_R, LP_OUTPUT_R);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "_filterMode",
            json_integer(_filterMode == MF20Filter::Mode::K35 ? 1 : 0));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* m = json_object_get(root, "_filterMode");
        if (m)
            _filterMode = json_integer_value(m) == 1
                       ? MF20Filter::Mode::K35
                       : MF20Filter::Mode::OTA;
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        _pool.setSampleRate(e.sampleRate);
        float alpha = smootherAlpha(e.sampleRate, 0.005f);
        for (int i = 0; i < 16; i++) {
            if (!_pool.engines[i]) continue;
            VoiceEngine* eng = _pool.engines[i];
            eng->lpCutoffSlew.setAlpha(alpha);
            eng->hpCutoffSlew.setAlpha(alpha);
            eng->lpResSlew.setAlpha(alpha);
            eng->hpResSlew.setAlpha(alpha);
        }
        _modulationSteps = static_cast<int>(e.sampleRate * 0.0025f);  // 2.5 ms
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for (int i = 0; i < 16; i++) {
            if (!_pool.engines[i]) continue;
            _pool.engines[i]->reset();
        }
    }

    // Called at ~2.5 ms intervals. Reads params/CVs and updates smoother targets
    // for each active voice. Per-voice CV inputs use getPolyVoltage(c).
    void modulate() {
        float drive = params[DRIVE_PARAM].getValue();
        _drive = drive;

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
            VoiceEngine* eng = _pool.engines[c];
            if (!eng) continue;
            eng->sanitize();

            // Total cutoff CV is an octave offset (1 V/oct) added to BOTH filters,
            // preserving the knob spread — the MS-20 "Total" cutoff-modulation bus.
            float totalOffset = 0.f;
            if (totalCvConn)
                totalOffset = totalCvAtten * inputs[TOTAL_CUTOFF_INPUT].getPolyVoltage(c);

            // LP cutoff target (log2 Hz): knob + Total + per-filter CV.
            float voiceCutoffLog = cutoffLog + totalOffset;
            if (lpCvConn)
                voiceCutoffLog += lpCvAtten * inputs[LP_CUTOFF_INPUT].getPolyVoltage(c);
            eng->lpCutoffTarget = voiceCutoffLog;

            // HP cutoff target (log2 Hz): knob + Total + per-filter CV.
            float voiceHpLog = hpLog + totalOffset;
            if (hpCvConn)
                voiceHpLog += hpCvAtten * inputs[HP_CUTOFF_INPUT].getPolyVoltage(c);
            eng->hpCutoffTarget = voiceHpLog;

            // Resonance targets — knob only (the MS-20 has no resonance modulation).
            eng->lpResTarget = res;
            eng->hpResTarget = hpResRaw;

            // Update filter mode and drive for this voice.
            eng->lpFilter.setMode(_filterMode);
            eng->hpFilter.setMode(_filterMode);
            eng->lpFilterR.setMode(_filterMode);
            eng->hpFilterR.setMode(_filterMode);
            eng->lpFilter.setDriveCharacter(drive);
            eng->lpFilterR.setDriveCharacter(drive);
            eng->hpFilter.setDriveCharacter(drive);
            eng->hpFilterR.setDriveCharacter(drive);
        }
    }

    // Process one voice (channel c). Advances its smoothers and runs the audio cascade.
    void processChannel(const ProcessArgs& args, int c) {
        VoiceEngine* eng = _pool.engines[c];
        if (!eng) return;

        // OTA mode: piecewise-linear pre-gain — amplifies by √drive, soft-clips at ±5 V.
        //   Unity gain at drive=1; continuous, monotonic for all drive values.
        //   NOTE: In OTA mode Drive is applied TWICE and intentionally so — here as
        //   input level, and inside the filter via setDriveCharacter() (see modulate()),
        //   which lowers the diode clip threshold and steepens its slope. K35 mode does
        //   all of this in processK35()'s forward-path clip, so no pre-gain is applied.
        auto otaPreGain = [&](float x) {
            if (_filterMode != MF20Filter::Mode::OTA) return x;
            float d = x * std::sqrt(_drive);
            return (d >  5.f) ?  5.f + 0.25f * (d - 5.f)
                 : (d < -5.f) ? -5.f + 0.25f * (d + 5.f)
                 : d;
        };

        float in = inputs[AUDIO_INPUT].getPolyVoltage(c);
        in += 1e-6f * (2.f * random::uniform() - 1.f);
        in = otaPreGain(in);

        // Advance slew smoothers one step toward their targets.
        float cutoffLog = eng->lpCutoffSlew.process(eng->lpCutoffTarget);
        float hpLog     = eng->hpCutoffSlew.process(eng->hpCutoffTarget);
        float res       = eng->lpResSlew.process(eng->lpResTarget);
        float hpResRaw  = eng->hpResSlew.process(eng->hpResTarget);

        float cutoffHz   = clamp(std::pow(2.f, cutoffLog), 20.f, args.sampleRate * 0.498f);
        float hpCutoffHz = clamp(std::pow(2.f, hpLog),     20.f, args.sampleRate * 0.498f);

        res = resTaper(res);
        float hpRes = resTaper(hpResRaw);

        auto hpStage = eng->hpFilter.processVCV(in,         hpCutoffHz, hpRes);
        auto lpStage = eng->lpFilter.processVCV(hpStage.hp, cutoffHz,   res);
        outputs[LP_OUTPUT].setVoltage(lpStage.lp, c);

        if (inputs[AUDIO_INPUT_R].isConnected()) {
            // True stereo: process R through its own filter pair.
            float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c);
            inR += 1e-6f * (2.f * random::uniform() - 1.f);
            inR = otaPreGain(inR);
            auto hpStageR = eng->hpFilterR.processVCV(inR,          hpCutoffHz, hpRes);
            auto lpStageR = eng->lpFilterR.processVCV(hpStageR.hp,  cutoffHz,   res);
            outputs[LP_OUTPUT_R].setVoltage(lpStageR.lp, c);
        } else {
            // R input is normalled to L → an identical filter pass. Mirror L instead
            // of recomputing it (skips a full HP+LP solve per voice in mono patches).
            outputs[LP_OUTPUT_R].setVoltage(lpStage.lp, c);
        }
    }

    void process(const ProcessArgs& args) override {
        int voices = std::max(1, inputs[AUDIO_INPUT].getChannels());
        _pool.setVoices(voices);

        outputs[LP_OUTPUT].setChannels(voices);
        outputs[LP_OUTPUT_R].setChannels(voices);

        if (++_steps >= _modulationSteps) {
            _steps = 0;
            modulate();
        }

        for (int c = 0; c < voices; c++)
            processChannel(args, c);
    }
};



struct MF20FilterWidget : ModuleWidget {
    MF20FilterWidget(MF20FilterModule* module) {

        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/MF20Filter.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Positions generated by helper.py from res/MF20Filter.svg (vcv-panel-gen).
        addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(11.80, 29.16)), module, MF20FilterModule::CUTOFF_PARAM));
        addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(39.00, 29.16)), module, MF20FilterModule::HP_CUTOFF_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(11.80, 45.16)), module, MF20FilterModule::LP_CUTOFF_CV_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.80, 57.51)), module, MF20FilterModule::LP_CUTOFF_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(25.40, 45.16)), module, MF20FilterModule::TOTAL_CUTOFF_CV_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.40, 57.51)), module, MF20FilterModule::TOTAL_CUTOFF_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(39.00, 45.16)), module, MF20FilterModule::HP_CUTOFF_CV_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.00, 57.51)), module, MF20FilterModule::HP_CUTOFF_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.80, 73.02)), module, MF20FilterModule::RES_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.00, 73.02)), module, MF20FilterModule::HP_RES_PARAM));
        addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 89.18)), module, MF20FilterModule::DRIVE_PARAM));
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
    }
};

Model* modelMF20Filter = createModel<MF20FilterModule, MF20FilterWidget>("MF20Filter");
