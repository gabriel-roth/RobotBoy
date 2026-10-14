#include "plugin.hpp"
#include "vespid/engine.hpp"

// Vespid: EDP Wasp-style CMOS state-variable filter, stereo and polyphonic.
//
// DSP: wasp::WaspFilter (src/vespid/WaspFilter.hpp) at 4x through
// wasp::EnginePool (src/vespid/engine.hpp). Params and CVs are read every
// ~2.5 ms in modulate(); per-sample smoothers slew the resulting targets.

struct Vespid : Module {
	enum ParamId {
		FREQ_PARAM,
		RES_PARAM,
		DRIVE_PARAM,
		BLEND_PARAM,       // LP <-> notch <-> HP crossfade on the Mix output
		FREQ_CV_PARAM,     // Frequency CV attenuverter
		RES_CV_PARAM,      // Resonance CV attenuverter
		DRIVE_CV_PARAM,    // Drive CV attenuverter
		PARAMS_LEN
	};
	enum InputId {
		AUDIO_L_INPUT,
		AUDIO_R_INPUT,
		FREQ_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
		BLEND_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MIX_L_OUTPUT, MIX_R_OUTPUT,   // Blend of LP/HP (notch at centre)
		HP_L_OUTPUT,  HP_R_OUTPUT,
		BP_L_OUTPUT,  BP_R_OUTPUT,
		LP_L_OUTPUT,  LP_R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// Character: false = British (the original EDP Wasp: overload limiter on,
	// no self-oscillation); true = German (the Doepfer A-124 mod: limiter off,
	// self-oscillates).
	bool german = false;

	// Panel theme: 0 = charcoal (default), 1 = gold. Persisted; the widget reads
	// it to pick which faceplate SVG to draw. MetaModule is locked to charcoal:
	// no Panel menu there, and dataFromJson ignores the key.
	int panelTheme = 0;

	// Per-voice DSP state (up to 16 voices), stereo, 4x oversampled
	// (1x and 2x leave audible aliasing from the nonlinear feedback loop).
	wasp::EnginePool pool;

	// modulate() runs every ~2.5 ms instead of every sample. steps starts at
	// modulationSteps so the first process() modulates immediately.
	int modulationSteps = 100;
	int steps = 100;

	float sampleRate = 44100.f;   // host rate

	// Inverter bandwidth (Hz) feeding the kC2eff self-oscillation term.
	// German 50 kHz: eager self-oscillation, onset well under a second at
	// mid/high cutoffs, reaching down to ~300-400 Hz cutoffs. British 60 kHz:
	// its free-run threshold sits at ~55-60 kHz (fitted_constants.md), so 60
	// keeps the no-self-oscillation promise.
	static constexpr float kFPoleGerman = 50000.f;
	static constexpr float kFPoleBritish = 60000.f;

	// Self-oscillation pitch tracking: false = hardware-accurate (drifts flat
	// at high resonance in German mode), true = corrected to track the knob.
	bool oscPitchCorrected = false;

	// Blend (LP<->HP crossfade) is shared across voices, so it has a single
	// module-level smoother.
	robotboy::OnePoleSmoother blendSlew{0.5f};
	float blendTarget = 0.5f;

	// Noise floor that alternates sign each sample (cheaper than an RNG and
	// bit-identical between VCV and MetaModule). It prevents denormals and
	// seeds self-oscillation: 1e-4 V (~-94 dBV, at Nyquist, inaudible) is
	// close to a real circuit's noise floor, and a much smaller seed makes
	// German mode's onset noticeably slower.
	float dither = 1e-4f;

	Vespid() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FREQ_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "Frequency", " Hz", 2.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(BLEND_PARAM, 0.f, 1.f, 0.5f, "Blend: LP/notch/HP");
		configParam(FREQ_CV_PARAM,  -1.f, 1.f, 1.f, "Frequency CV", "x");
		configParam(RES_CV_PARAM,   -1.f, 1.f, 1.f, "Resonance CV", "x");
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 1.f, "Drive CV", "x");

		configInput(AUDIO_L_INPUT, "Audio L");
		configInput(AUDIO_R_INPUT, "Audio R");
		configInput(FREQ_INPUT, "Frequency CV");
		configInput(RES_INPUT, "Resonance CV");
		configInput(DRIVE_INPUT, "Drive CV");
		configInput(BLEND_INPUT, "Blend CV");

		configOutput(LP_L_OUTPUT, "Lowpass L");
		configOutput(LP_R_OUTPUT, "Lowpass R");
		configOutput(BP_L_OUTPUT, "Bandpass L");
		configOutput(BP_R_OUTPUT, "Bandpass R");
		configOutput(HP_L_OUTPUT, "Highpass L");
		configOutput(HP_R_OUTPUT, "Highpass R");
		configOutput(MIX_L_OUTPUT, "Mix L");
		configOutput(MIX_R_OUTPUT, "Mix R");

		// Bypass routes the dry audio input to every output.
		for (int out : {LP_L_OUTPUT, BP_L_OUTPUT, HP_L_OUTPUT, MIX_L_OUTPUT})
			configBypass(AUDIO_L_INPUT, out);
		for (int out : {LP_R_OUTPUT, BP_R_OUTPUT, HP_R_OUTPUT, MIX_R_OUTPUT})
			configBypass(AUDIO_R_INPUT, out);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		float alpha = robotboy::smootherAlpha(sampleRate, 0.005f);
		pool.setSampleRate(sampleRate, alpha);
		blendSlew.setAlpha(alpha);
		// The g/kC2/H1 targets depend on the rate, so modulate on the next
		// process() rather than run on stale coefficients for ~2.5 ms.
		modulationSteps = std::max(1, (int)(sampleRate * 0.0025f));
		steps = modulationSteps;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		pool.resetAll();
	}

	// Called at ~2.5 ms intervals: reads params/CVs, updates per-voice
	// smoother targets. Division-heavy math (tan, computeH1) lives here, not
	// in the audio path.
	void modulate() {
		const wasp::ModeConfig& mode = german ? wasp::kGerman : wasp::kBritish;
		float fsInt = sampleRate * wasp::kOversample;

		float fPole = german ? kFPoleGerman : kFPoleBritish;

		float freqLog     = params[FREQ_PARAM].getValue();
		float freqCvAtten = params[FREQ_CV_PARAM].getValue();
		float res         = clamp(params[RES_PARAM].getValue(), 0.f, 1.f);
		float resCvAtten  = params[RES_CV_PARAM].getValue();
		float driveKnob   = clamp(params[DRIVE_PARAM].getValue(), 0.f, 1.f);
		float driveCvAtten = params[DRIVE_CV_PARAM].getValue();

		bool freqCvConnected = inputs[FREQ_INPUT].isConnected();
		bool resCvConnected = inputs[RES_INPUT].isConnected();
		bool driveCvConnected = inputs[DRIVE_INPUT].isConnected();

		for (int c = 0; c < pool.activeVoices; c++) {
			wasp::VoiceEngine& eng = pool.engines[c];
			eng.sanitize();

			eng.l.filt.setMode(mode);
			eng.r.filt.setMode(mode);

			// Frequency: 1 V/oct CV on the log2 knob value, then to Hz,
			// clamped [1 Hz, 0.45*fsInt] before the wcComp/internal clamps.
			float fcLog = freqLog;
			if (freqCvConnected)
				fcLog += freqCvAtten * inputs[FREQ_INPUT].getPolyVoltage(c);
			float fc = clamp(std::exp2(fcLog), 1.f, 0.45f * fsInt);

			float fcInt = fc * mode.wcComp;

			// Self-oscillation pitch correction (German mode only): the
			// rail-bounded limit cycle runs ~0.72x the knob's small-signal
			// placement at rho=1; blend toward a corrected scale as rho
			// approaches 1 so the perceived pitch tracks the knob there.
			if (oscPitchCorrected && german) {
				float rhoNow = eng.rhoSlew.value;
				float s = clamp((rhoNow - 0.85f) / 0.15f, 0.f, 1.f);
				fcInt *= 1.f + (1.f / 0.72f - 1.f) * s;
			}

			fcInt = clamp(fcInt, 0.25f, 0.45f * fsInt);
			eng.gTarget = std::tan(float(M_PI) * fcInt / fsInt);

			// kC2eff may legitimately go negative (the self-oscillation
			// mechanism) — never clamp it.
			float wcInt = 2.f * float(M_PI) * fcInt;
			eng.kC2Target = 27e3f * 100e-12f * wcInt
			               - mode.kR2 * wcInt / (2.f * float(M_PI) * fPole);

			// Resonance.
			float rho = res;
			if (resCvConnected)
				rho += resCvAtten * inputs[RES_INPUT].getPolyVoltage(c) / 10.f;
			rho = clamp(rho, 0.f, 1.f);
			eng.rhoTarget = rho;

			// Drive: knob 0..1 -> 2x..64x (2x fixed pre-gain, 30 dB span),
			// times the per-mode hardware level staging (mode.inGain). At
			// drive 0 a 5 V signal lands at German mode's Euro-hot staging
			// (10 V eq., clean, onset ~6% up the knob) and British mode's
			// EDP-nominal 2.5 V (the original's light rasp, ~12% THD).
			float drive01 = driveKnob;
			if (driveCvConnected)
				drive01 += driveCvAtten * inputs[DRIVE_INPUT].getPolyVoltage(c) / 10.f;
			drive01 = clamp(drive01, 0.f, 1.f);
			eng.driveTarget = 2.f * std::exp2(5.f * drive01) * mode.inGain;

			// H1 (resonance network) coefficients: computeH1 is division-heavy,
			// so it's evaluated here (modulate rate) and its outputs are what
			// gets slewed per-sample, not rho itself.
			wasp::H1Coeffs h1t = wasp::computeH1(rho, fsInt);
			eng.beta0Target  = h1t.beta0;
			eng.beta1Target  = h1t.beta1;
			eng.alpha1Target = h1t.alpha1;
		}

		// Blend (LP<->HP crossfade): shared across voices, no attenuverter.
		float blend = params[BLEND_PARAM].getValue();
		if (inputs[BLEND_INPUT].isConnected())
			blend += inputs[BLEND_INPUT].getVoltage() / 10.f;
		blendTarget = clamp(blend, 0.f, 1.f);
	}

	// One voice's audio for L, and for R unless it's normalled to L. The
	// Blend smoother is advanced once per sample in process(), not here, so
	// every voice sees the same value. maskL/maskR select which signals each
	// channel decimates.
	void processVoice(int c, float m, bool rConnected, int maskL, int maskR, float makeup) {
		wasp::VoiceEngine& eng = pool.engines[c];

		float g = eng.gSlew.process(eng.gTarget);
		float kC2 = eng.kC2Slew.process(eng.kC2Target);
		float drive = eng.driveSlew.process(eng.driveTarget);
		// rhoSlew doesn't feed the audio path (the H1 coefficient smoothers
		// do) but stays current so modulate() can read back a smoothed
		// resonance for the pitch correction.
		eng.rhoSlew.process(eng.rhoTarget);
		wasp::H1Coeffs h1{
			eng.beta0Slew.process(eng.beta0Target),
			eng.beta1Slew.process(eng.beta1Target),
			eng.alpha1Slew.process(eng.alpha1Target)
		};

		float inL = inputs[AUDIO_L_INPUT].getPolyVoltage(c);
		wasp::Channel::Out oL = eng.l.process(inL * drive + dither, g, h1, kC2, maskL, m, makeup);
		// An unpatched R input is normalled to L: mirror L's outputs (maskL
		// already includes the R jacks' needs in that case).
		wasp::Channel::Out oR = oL;
		if (rConnected) {
			float inR = inputs[AUDIO_R_INPUT].getPolyVoltage(c);
			oR = eng.r.process(inR * drive + dither, g, h1, kC2, maskR, m, makeup);
		}

		outputs[LP_L_OUTPUT].setVoltage(oL.lp, c);
		outputs[BP_L_OUTPUT].setVoltage(oL.bp, c);
		outputs[HP_L_OUTPUT].setVoltage(oL.hp, c);
		outputs[MIX_L_OUTPUT].setVoltage(oL.mix, c);
		outputs[LP_R_OUTPUT].setVoltage(oR.lp, c);
		outputs[BP_R_OUTPUT].setVoltage(oR.bp, c);
		outputs[HP_R_OUTPUT].setVoltage(oR.hp, c);
		outputs[MIX_R_OUTPUT].setVoltage(oR.mix, c);
	}

	// Which signals a side's connected jacks need.
	int outputMask(int lp, int bp, int hp, int mix) {
		return (outputs[lp].isConnected() ? wasp::kOutLp : 0)
		     | (outputs[bp].isConnected() ? wasp::kOutBp : 0)
		     | (outputs[hp].isConnected() ? wasp::kOutHp : 0)
		     | (outputs[mix].isConnected() ? wasp::kOutMix : 0);
	}

	void process(const ProcessArgs& args) override {
		int channels = std::max({1, inputs[AUDIO_L_INPUT].getChannels(),
		                         inputs[AUDIO_R_INPUT].getChannels()});
		pool.setVoices(channels);
		if (outputs[LP_L_OUTPUT].getChannels() != channels)
			for (int out = 0; out < OUTPUTS_LEN; out++)
				outputs[out].setChannels(channels);

		if (++steps >= modulationSteps) {
			steps = 0;
			modulate();
		}

		dither = -dither;
		float m = blendSlew.process(blendTarget);
		bool rConnected = inputs[AUDIO_R_INPUT].isConnected();
		// Only connected outputs pay for decimation and DC blocking. When R
		// is normalled to L, the R jacks are fed from L, so their needs fold
		// into maskL.
		int maskL = outputMask(LP_L_OUTPUT, BP_L_OUTPUT, HP_L_OUTPUT, MIX_L_OUTPUT);
		int maskR = outputMask(LP_R_OUTPUT, BP_R_OUTPUT, HP_R_OUTPUT, MIX_R_OUTPUT);
		if (!rConnected) {
			maskL |= maskR;
			maskR = 0;
		}
		float makeup = (german ? wasp::kGerman : wasp::kBritish).makeup;
		for (int c = 0; c < channels; c++)
			processVoice(c, m, rConnected, maskL, maskR, makeup);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "german", json_boolean(german));
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));
		json_object_set_new(rootJ, "oscPitchCorrected", json_boolean(oscPitchCorrected));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// "doepfer" and "screaming" are this setting's names in older patches.
		json_t* germanJ = json_object_get(rootJ, "german");
		if (!germanJ)
			germanJ = json_object_get(rootJ, "doepfer");
		if (!germanJ)
			germanJ = json_object_get(rootJ, "screaming");
		if (germanJ)
			german = json_boolean_value(germanJ);
#if !defined(METAMODULE)
		// MetaModule is locked to charcoal, so it ignores the key.
		json_t* themeJ = json_object_get(rootJ, "panelTheme");
		if (themeJ)
			panelTheme = json_integer_value(themeJ);
#endif
		json_t* pitchJ = json_object_get(rootJ, "oscPitchCorrected");
		if (pitchJ)
			oscPitchCorrected = json_boolean_value(pitchJ);
	}
};

struct VespidWidget : ModuleWidget {
	app::SvgPanel* panel = nullptr;

#if !defined(METAMODULE)
	// The faceplate follows the module's panelTheme (0 = charcoal, 1 = gold),
	// however it changed: menu, preset load or undo. Screws stay dark on both
	// themes. Desktop only: MetaModule is locked to charcoal.
	int shownTheme = 0;

	void step() override {
		Vespid* module = getModule<Vespid>();
		if (module && module->panelTheme != shownTheme) {
			shownTheme = module->panelTheme;
			std::string f = (shownTheme == 1) ? "res/Vespid-gold.svg" : "res/Vespid.svg";
			panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, f)));
		}
		ModuleWidget::step();
	}
#endif

	VespidWidget(Vespid* module) {
		setModule(module);
		panel = createPanel(asset::plugin(pluginInstance, "res/Vespid.svg"));
		setPanel(panel);

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions mirror res/Vespid.svg (vcv-panel-gen), 10 HP.
		// Top cluster: Res / Freq hero / Drive, each over its attenuverter + CV jack.
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.467, 32.000)), module, Vespid::RES_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.400, 29.200)), module, Vespid::FREQ_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(40.333, 32.000)), module, Vespid::DRIVE_PARAM));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(10.467, 45.000)), module, Vespid::RES_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(25.400, 45.000)), module, Vespid::FREQ_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(40.333, 45.000)), module, Vespid::DRIVE_CV_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.467, 56.250)), module, Vespid::RES_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.400, 56.250)), module, Vespid::FREQ_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.333, 56.250)), module, Vespid::DRIVE_INPUT));

		// Audio input, centered in the gap between the two zones.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.550, 69.550)), module, Vespid::AUDIO_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.250, 69.550)), module, Vespid::AUDIO_R_INPUT));

		// Lower zone. Left column: Blend CV / Blend knob / Mix. Right column: HP / BP / LP.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.480, 86.750)), module, Vespid::BLEND_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.450, 86.750)), module, Vespid::HP_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.150, 86.750)), module, Vespid::HP_R_OUTPUT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(13.497, 101.950)), module, Vespid::BLEND_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.447, 101.950)), module, Vespid::BP_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.147, 101.950)), module, Vespid::BP_R_OUTPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.650, 117.150)), module, Vespid::MIX_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(18.350, 117.150)), module, Vespid::MIX_R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.450, 117.150)), module, Vespid::LP_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.150, 117.150)), module, Vespid::LP_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Vespid* module = getModule<Vespid>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Character",
			{"British (limited)", "German (self-oscillates)"},
			[=]() { return module->german ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "change character", [=]() { module->german = (i == 1); });
			}));
		menu->addChild(createIndexSubmenuItem("Self-oscillation pitch (German)",
			{"Hardware (drifts flat)", "Corrected (tracks knob)"},
			[=]() { return module->oscPitchCorrected ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "change self-oscillation pitch", [=]() { module->oscPitchCorrected = (i == 1); });
			}));
#if !defined(METAMODULE)
		// Desktop only: MetaModule is locked to the charcoal faceplate.
		menu->addChild(createIndexSubmenuItem("Panel",
			{"Charcoal", "Gold"},
			[=]() { return module->panelTheme; },
			[=](int i) {
				withMenuUndo(module, "change panel", [=]() { module->panelTheme = i; });
			}));
#endif
	}
};

Model* modelVespid = createModel<Vespid, VespidWidget>("Vespid");
