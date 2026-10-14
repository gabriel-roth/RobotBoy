#include "plugin.hpp"
#include "engine.hpp"

// MF-20: Korg MS-20-style filter, an HP filter into an LP filter per voice,
// stereo and polyphonic (src/mf20/MF20Filter.hpp has the circuit model).
// Params and CVs are read every ~2.5 ms in modulate(); per-sample smoothers
// slew the resulting targets.
struct MF20Filter : Module {
	enum ParamId {
		CUTOFF_PARAM,            // LP cutoff (log2 Hz)
		RES_PARAM,               // LP resonance
		HP_CUTOFF_PARAM,         // HP cutoff (log2 Hz)
		HP_RES_PARAM,            // HP resonance
		DRIVE_PARAM,
		LP_CUTOFF_CV_PARAM,      // attenuverter for LP cutoff CV
		HP_CUTOFF_CV_PARAM,      // attenuverter for HP cutoff CV
		TOTAL_CUTOFF_CV_PARAM,   // attenuverter for Total cutoff CV (both filters)
		PARAMS_LEN
	};
	enum InputId {
		AUDIO_L_INPUT,
		AUDIO_R_INPUT,
		LP_CUTOFF_INPUT,
		HP_CUTOFF_INPUT,
		TOTAL_CUTOFF_INPUT,      // sweeps both LP and HP cutoff (MS-20 "Total")
		INPUTS_LEN
	};
	enum OutputId {
		AUDIO_L_OUTPUT,
		AUDIO_R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	mf20::EnginePool pool;

	// Cutoff clamp floor (Hz). The knob floors at 20 Hz, but CV can take the
	// cutoff down to the filter core's low limit, so deep negative CV nearly
	// closes the filter like an MS-20's "Total" sweep.
	static constexpr float kCutoffFloorHz = 1.f;

	// modulate() runs every ~2.5 ms instead of every sample. steps starts at
	// modulationSteps so the first process() modulates immediately.
	int modulationSteps = 100;
	int steps = 100;

	float sampleRate = 44100.f;

	// Drive smoothing: targets computed once per modulate block, slewed
	// per sample (5 ms) so Drive sweeps don't zipper. √drive feeds the OTA
	// pre-gain and K35 forward clip, 1/√drive the clip threshold; slewing
	// both keeps sqrt and divide out of the audio path.
	robotboy::OnePoleSmoother driveSqrtSlew{1.f};
	robotboy::OnePoleSmoother clipThreshSlew{1.f};
	robotboy::OnePoleSmoother fbThreshSlew{1.f};
	float driveSqrtTarget = 1.f;
	float clipThreshTarget = 1.f;
	float fbThreshTarget = 1.f;
	float driveSqrt = 1.f;    // current smoothed values
	float clipThresh = 1.f;
	float fbThresh = 1.f;     // K35 feedback-loop clip threshold
	// Resonance retention [0,1]: how much Drive is stopped from squashing the
	// resonant peak. The path differs by mode (see modulate()): OTA eases its
	// feedback diode toward the Drive-1 threshold while keeping full input
	// pre-gain; K35 keeps its forward clip at full drive (grit) and instead
	// opens the resonance-loop clip.
	static constexpr float kResRetention = 0.75f;
	// Max K35 feedback-loop clip threshold at full retention (threshold 1.0 =
	// stock). Bounded so the loop stays stable (saturated-region slope is
	// unchanged).
	static constexpr float kK35FbRetentionMax = 3.f;
	// Denormal-prevention dither that alternates sign each sample: cheaper
	// than an RNG, and bit-identical between the VCV and MetaModule builds.
	float dither = 1e-9f;

	mf20::MF20Filter::Mode filterMode = mf20::MF20Filter::Mode::OTA;

	MF20Filter() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "LP cutoff", " Hz", 2.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.25f, "LP resonance", "%", 0.f, 100.f);
		configParam(HP_CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(120.f),
		            "HP cutoff", " Hz", 2.f);
		configParam(HP_RES_PARAM, 0.f, 1.f, 0.25f, "HP resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 1.f, 8.f, 1.f, "Drive", "x");
		configParam(LP_CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "LP cutoff CV", "x");
		configParam(HP_CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "HP cutoff CV", "x");
		configParam(TOTAL_CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "Total cutoff CV (both filters)", "x");

		configInput(AUDIO_L_INPUT, "Audio L");
		configInput(AUDIO_R_INPUT, "Audio R");
		configInput(LP_CUTOFF_INPUT, "LP cutoff CV");
		configInput(HP_CUTOFF_INPUT, "HP cutoff CV");
		configInput(TOTAL_CUTOFF_INPUT, "Total cutoff CV (sweeps both filters)");

		configOutput(AUDIO_L_OUTPUT, "Audio L");
		configOutput(AUDIO_R_OUTPUT, "Audio R");

		configBypass(AUDIO_L_INPUT, AUDIO_L_OUTPUT);
		configBypass(AUDIO_R_INPUT, AUDIO_R_OUTPUT);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "_filterMode",
			json_integer(filterMode == mf20::MF20Filter::Mode::K35 ? 1 : 0));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* modeJ = json_object_get(rootJ, "_filterMode");
		if (modeJ)
			filterMode = json_integer_value(modeJ) == 1
				? mf20::MF20Filter::Mode::K35
				: mf20::MF20Filter::Mode::OTA;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		float alpha = robotboy::smootherAlpha(e.sampleRate, 0.005f);
		pool.setAlpha(alpha);
		driveSqrtSlew.setAlpha(alpha);
		clipThreshSlew.setAlpha(alpha);
		fbThreshSlew.setAlpha(alpha);
		modulationSteps = (int)(e.sampleRate * 0.0025f);
		steps = modulationSteps;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		pool.resetAll();
	}

	// Reads params and CVs and updates the smoother targets for each active
	// voice. Per-voice CV inputs use getPolyVoltage(c).
	void modulate() {
		float drive = params[DRIVE_PARAM].getValue();
		driveSqrtTarget = std::sqrt(drive);
		// Resonance retention takes a mode-appropriate path so each mode keeps
		// its Drive character while regaining resonance:
		//   OTA: ease the feedback-diode threshold from 1/√drive toward 1.0
		//        (its Drive-1 value). The input pre-gain is untouched, so Drive
		//        keeps its loudness and edge; only the diode relaxes.
		//   K35: keep the forward clip at full drive (1/√drive, grit intact)
		//        and open the resonance-loop clip threshold from 1.0 toward
		//        kK35FbRetentionMax, so the peak rings back up above the
		//        saturation.
		constexpr float r = kResRetention;
		if (filterMode == mf20::MF20Filter::Mode::K35) {
			clipThreshTarget = 1.f / driveSqrtTarget;
			fbThreshTarget = 1.f + r * (kK35FbRetentionMax - 1.f);
		}
		else {
			clipThreshTarget = (1.f - r) * (1.f / driveSqrtTarget) + r;
			fbThreshTarget = 1.f;   // OTA ignores the feedback-loop threshold
		}

		float lpLog = params[CUTOFF_PARAM].getValue();
		float hpLog = params[HP_CUTOFF_PARAM].getValue();
		float lpRes = clamp(params[RES_PARAM].getValue(), 0.f, 1.f);
		float hpRes = clamp(params[HP_RES_PARAM].getValue(), 0.f, 1.f);
		float lpCvAtten = params[LP_CUTOFF_CV_PARAM].getValue();
		float hpCvAtten = params[HP_CUTOFF_CV_PARAM].getValue();
		float totalCvAtten = params[TOTAL_CUTOFF_CV_PARAM].getValue();

		bool lpCvConnected = inputs[LP_CUTOFF_INPUT].isConnected();
		bool hpCvConnected = inputs[HP_CUTOFF_INPUT].isConnected();
		bool totalCvConnected = inputs[TOTAL_CUTOFF_INPUT].isConnected();

		for (int c = 0; c < pool.activeVoices; c++) {
			mf20::VoiceEngine& eng = pool.engines[c];
			eng.sanitize();

			// Total cutoff CV is an octave offset (1 V/oct) added to both
			// filters, preserving the knob spread: the MS-20 "Total" bus.
			float totalOffset = 0.f;
			if (totalCvConnected)
				totalOffset = totalCvAtten * inputs[TOTAL_CUTOFF_INPUT].getPolyVoltage(c);

			float voiceLpLog = lpLog + totalOffset;
			if (lpCvConnected)
				voiceLpLog += lpCvAtten * inputs[LP_CUTOFF_INPUT].getPolyVoltage(c);
			float lpHz = clamp(std::exp2(voiceLpLog), kCutoffFloorHz, sampleRate * 0.498f);
			eng.lpGTarget = mf20::MF20Filter::cutoffToG(lpHz, sampleRate);

			float voiceHpLog = hpLog + totalOffset;
			if (hpCvConnected)
				voiceHpLog += hpCvAtten * inputs[HP_CUTOFF_INPUT].getPolyVoltage(c);
			float hpHz = clamp(std::exp2(voiceHpLog), kCutoffFloorHz, sampleRate * 0.498f);
			eng.hpGTarget = mf20::MF20Filter::cutoffToG(hpHz, sampleRate);

			// Resonance: knob only (the MS-20 has no resonance modulation).
			eng.lpResTarget = lpRes;
			eng.hpResTarget = hpRes;

			eng.lpFilter.setMode(filterMode);
			eng.hpFilter.setMode(filterMode);
			eng.lpFilterR.setMode(filterMode);
			eng.hpFilterR.setMode(filterMode);
		}
	}

	// OTA mode's input pre-gain: amplifies by √drive and soft-clips at ±5 V
	// (unity at drive 1, continuous and monotonic). Drive is applied twice
	// in OTA mode, deliberately: here as input level, and inside the filter
	// as a lower, steeper diode clip. K35 mode does both in its forward-path
	// clip, so it gets no pre-gain.
	float otaPreGain(float x) const {
		if (filterMode != mf20::MF20Filter::Mode::OTA)
			return x;
		float d = x * driveSqrt;
		return (d > 5.f) ? 5.f + 0.25f * (d - 5.f)
		     : (d < -5.f) ? -5.f + 0.25f * (d + 5.f)
		     : d;
	}

	// One HP→LP pass through a side's filter pair; returns the LP output.
	float processSide(mf20::MF20Filter& hp, mf20::MF20Filter& lp, float in,
	                  float gHp, float gLp, float hpRes, float lpRes) {
		hp.setDriveCharacter(clipThresh, driveSqrt);
		lp.setDriveCharacter(clipThresh, driveSqrt);
		hp.setFbThreshold(fbThresh);
		lp.setFbThreshold(fbThresh);
		float x = otaPreGain(in + dither);
		auto hpStage = hp.processVCVG(x, gHp, hpRes);
		return lp.processVCVG(hpStage.hp, gLp, lpRes).lp;
	}

	void process(const ProcessArgs& args) override {
		int voices = std::max({1, inputs[AUDIO_L_INPUT].getChannels(), inputs[AUDIO_R_INPUT].getChannels()});
		pool.setVoices(voices);
		outputs[AUDIO_L_OUTPUT].setChannels(voices);
		outputs[AUDIO_R_OUTPUT].setChannels(voices);

		if (++steps >= modulationSteps) {
			steps = 0;
			modulate();
		}

		dither = -dither;
		driveSqrt = driveSqrtSlew.process(driveSqrtTarget);
		clipThresh = clipThreshSlew.process(clipThreshTarget);
		fbThresh = fbThreshSlew.process(fbThreshTarget);
		bool rConnected = inputs[AUDIO_R_INPUT].isConnected();

		for (int c = 0; c < voices; c++) {
			mf20::VoiceEngine& eng = pool.engines[c];
			float gLp = eng.lpGSlew.process(eng.lpGTarget);
			float gHp = eng.hpGSlew.process(eng.hpGTarget);
			float lpRes = mf20::resTaper(eng.lpResSlew.process(eng.lpResTarget));
			float hpRes = mf20::resTaper(eng.hpResSlew.process(eng.hpResTarget));

			float outL = processSide(eng.hpFilter, eng.lpFilter,
				inputs[AUDIO_L_INPUT].getPolyVoltage(c), gHp, gLp, hpRes, lpRes);
			outputs[AUDIO_L_OUTPUT].setVoltage(outL, c);

			// An unpatched R input is normalled to L: mirror L instead of
			// running the identical pass again.
			float outR = rConnected
				? processSide(eng.hpFilterR, eng.lpFilterR,
					inputs[AUDIO_R_INPUT].getPolyVoltage(c), gHp, gLp, hpRes, lpRes)
				: outL;
			outputs[AUDIO_R_OUTPUT].setVoltage(outR, c);
		}
	}
};

struct MF20FilterWidget : ModuleWidget {
	MF20FilterWidget(MF20Filter* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MF20Filter.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions mirror res/MF20Filter.svg (vcv-panel-gen).
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(11.80, 30.997)), module, MF20Filter::CUTOFF_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(39.00, 30.997)), module, MF20Filter::HP_CUTOFF_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.80, 46.997)), module, MF20Filter::LP_CUTOFF_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.80, 59.347)), module, MF20Filter::LP_CUTOFF_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(25.40, 46.997)), module, MF20Filter::TOTAL_CUTOFF_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.40, 59.347)), module, MF20Filter::TOTAL_CUTOFF_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(39.00, 46.997)), module, MF20Filter::HP_CUTOFF_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.00, 59.347)), module, MF20Filter::HP_CUTOFF_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.80, 74.857)), module, MF20Filter::RES_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.00, 74.857)), module, MF20Filter::HP_RES_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 91.017)), module, MF20Filter::DRIVE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.35, 113.69)), module, MF20Filter::AUDIO_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.05, 113.69)), module, MF20Filter::AUDIO_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.75, 113.69)), module, MF20Filter::AUDIO_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.45, 113.69)), module, MF20Filter::AUDIO_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		MF20Filter* module = getModule<MF20Filter>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Filter revision",
			{"OTA (revised MS-20)", "Korg35 (original MS-20)"},
			[=]() { return module->filterMode == mf20::MF20Filter::Mode::K35 ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "change filter revision", [=]() {
					module->filterMode = i ? mf20::MF20Filter::Mode::K35 : mf20::MF20Filter::Mode::OTA;
				});
			}));
	}
};

Model* modelMF20Filter = createModel<MF20Filter, MF20FilterWidget>("MF20Filter");
