#include "plugin.hpp"
#include "onbetap/OnbetapFilter.hpp"
#include "onbetap/engine.hpp"
#include "onbetap/drive.hpp"

// Onbetap: Polivoks-style multimode filter.
//
// Nonlinear TPT two-integrator core (OnbetapFilter), oversampled 2x or 4x,
// one core per stereo side per voice (OnbetapPool, up to 16 voices).
// Cutoff, resonance and drive targets are computed at ~2.5 ms intervals
// (modulate()); the audio path only slews them and runs the oversampled
// solve. Mode switching crossfades over 5 ms, except in Vintage character,
// which switches hard like the factory panel switch. See Filters.md
// (Onbetap: Sources) for the circuit references behind the model.
//
// Drive: onbetap/drive.hpp maps the knob to the core's input gain, a constant
// output makeup and a push into the output VCA, which saturates at
// 9·tanhish(v/9).

static constexpr float kCLag = 0.25f;              // phase-lag correction: k -= kCLag·cutoffLagCorr(fc)
static constexpr float kOnsetTrim = 0.045f;        // moves the self-oscillation onset from res ~0.72 to ~0.84
static constexpr float kVintageDriftScale = 0.12f; // DriftWalker scale (octaves)
static constexpr float kVintageOffset = 0.03f;     // node offset at 750 Hz, scales with log2 fc
static constexpr float kDcBlockHz = 1.6f;

struct Onbetap : Module {
	enum ParamId {
		CUTOFF_PARAM,
		RES_PARAM,         // Resonance (labelled "Q" on the panel)
		DRIVE_PARAM,
		MODE_PARAM,        // 0=LP 1=BP 2=HP 3=Notch 4=Peak
		CUTOFF_CV_PARAM,   // Cutoff CV attenuverter
		RES_CV_PARAM,      // Resonance CV attenuverter
		DRIVE_CV_PARAM,    // Drive CV attenuverter
		PARAMS_LEN
	};
	enum InputId {
		AUDIO_L_INPUT,
		AUDIO_R_INPUT,
		CUTOFF_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
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

	// Character: false = Tamed (stable, playable), true = Vintage (drifty,
	// untuned self-oscillation).
	bool vintageDrift = false;

	onbetap::OnbetapPool pool;
	onbetap::DriftWalker driftL{0x0B617A01u};
	onbetap::DriftWalker driftR{0x0B617A02u};

	// Oversampling, 2x ("CPU efficient") or 4x ("High quality"). The menu
	// sets oversample; process() applies it, since the filter and FIR
	// histories have to be reset on the audio thread.
	static constexpr int kDefaultOversample = 2;
	int oversample = kDefaultOversample;
	int activeOversample = kDefaultOversample;

	int modulationSteps = 100, steps = 100;
	float sampleRate = 44100.f;
	float dither = 1e-9f;

	// Mode crossfade (5 ms): current/target mode + ramp position
	int modeCurrent = 0, modeTarget = 0;
	float modeXf = 1.f, modeXfStep = 1.f;

	Onbetap() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "Cutoff", " Hz", 2.f);
		configParam(CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "Cutoff CV", "x");
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(RES_CV_PARAM, -1.f, 1.f, 1.f, "Resonance CV", "x");
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 1.f, "Drive CV", "x");
		configSwitch(MODE_PARAM, 0.f, 4.f, 0.f, "Mode",
		             {"Lowpass", "Bandpass", "Highpass", "Notch", "Peak"});

		configInput(AUDIO_L_INPUT, "Audio L");
		configInput(AUDIO_R_INPUT, "Audio R");
		configInput(CUTOFF_INPUT, "Cutoff CV");
		configInput(RES_INPUT, "Resonance CV");
		configInput(DRIVE_INPUT, "Drive CV");

		configOutput(AUDIO_L_OUTPUT, "Audio L");
		configOutput(AUDIO_R_OUTPUT, "Audio R");

		configBypass(AUDIO_L_INPUT, AUDIO_L_OUTPUT);
		configBypass(AUDIO_R_INPUT, AUDIO_R_OUTPUT);

		// Rack sends a sample-rate change after construction; this covers
		// hosts that don't.
		configureRates(44100.f);
	}

	void configureRates(float fs) {
		sampleRate = fs;
		float alpha = robotboy::smootherAlpha(fs, 0.005f);
		for (auto& v : pool.voices)
			v.setRates(alpha, kDcBlockHz, fs);
		modulationSteps = (int)(fs * 0.0025f);
		steps = modulationSteps;   // modulate on the next process()
		modeXfStep = 1.f / (0.005f * fs);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		configureRates(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		pool.resetAll();
		driftL.reset();
		driftR.reset();
		steps = modulationSteps;
	}

	void modulate() {
		float fsOs = sampleRate * activeOversample;

		// Drift (Vintage only), per side. Both walkers advance every block
		// so toggling Character mid-patch stays deterministic.
		float dL = driftL.step(0.0025f, kVintageDriftScale);
		float dR = driftR.step(0.0025f, kVintageDriftScale);
		if (!vintageDrift)
			dL = dR = 0.f;

		float cutoffLog = params[CUTOFF_PARAM].getValue();
		float resKnob = params[RES_PARAM].getValue();
		float driveKnob = params[DRIVE_PARAM].getValue();
		float cutoffAtten = params[CUTOFF_CV_PARAM].getValue();
		float resAtten = params[RES_CV_PARAM].getValue();
		float driveAtten = params[DRIVE_CV_PARAM].getValue();
		bool cutoffCvConnected = inputs[CUTOFF_INPUT].isConnected();
		bool resCvConnected = inputs[RES_INPUT].isConnected();
		bool driveCvConnected = inputs[DRIVE_INPUT].isConnected();

		// Mode target (snap knob); start a crossfade on change.
		int m = (int)std::round(params[MODE_PARAM].getValue());
		if (m != modeTarget) {
			modeCurrent = modeTarget;
			modeTarget = m;
			modeXf = 0.f;
		}

		for (int c = 0; c < pool.activeVoices; c++) {
			onbetap::OnbetapVoice& v = pool.voices[c];
			v.sanitize();

			// The g target carries the L drift; R applies its own as a ratio
			// (rGRatio), so poly voices share one target.
			float voiceLog = cutoffLog;
			if (cutoffCvConnected)
				voiceLog += cutoffAtten * inputs[CUTOFF_INPUT].getPolyVoltage(c);
			float fc = std::exp2(voiceLog + dL);
			v.gTarget = onbetap::OnbetapFilter::cutoffToG(fc, fsOs);

			float res = resKnob;
			if (resCvConnected)
				res += resAtten * inputs[RES_INPUT].getPolyVoltage(c) * 0.2f;
			res = clamp(res, 0.f, 1.f);
			// Reverse-log damping map plus the onset trim; minimum resonance
			// is Q≈1.
			float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f) + kOnsetTrim;
			// Phase-lag correction, evaluated at the calibration reference
			// rate so it doesn't depend on oversampling or host rate.
			v.kTarget = k - kCLag * onbetap::cutoffLagCorr(fc);

			float drive = driveKnob;
			if (driveCvConnected)
				drive += driveAtten * inputs[DRIVE_INPUT].getPolyVoltage(c) * 0.2f;
			drive = clamp(drive, 0.f, 1.f);
			onbetap::DriveGains gains = onbetap::driveGains(drive);
			v.driveTarget = gains.driveScale;
			v.makeupTarget = gains.makeup;
			v.pushTarget = gains.vcaPush;

			// Character: integrator mismatch and a cutoff-scaled node offset.
			if (vintageDrift) {
				v.l.filter.setMismatch(onbetap::kMismatchL1, onbetap::kMismatchL2);
				v.r.filter.setMismatch(onbetap::kMismatchR1, onbetap::kMismatchR2);
				float offset = kVintageOffset * (voiceLog - std::log2(750.f)) * 0.25f;
				v.l.filter.setOffset(offset);
				v.r.filter.setOffset(-0.8f * offset);
				v.rGRatio = std::exp2(dR - dL);
			}
			else {
				v.l.filter.setMismatch(0.f, 0.f);
				v.r.filter.setMismatch(0.f, 0.f);
				v.l.filter.setOffset(0.f);
				v.r.filter.setOffset(0.f);
				v.rGRatio = 1.f;
			}
			float boost = clamp(onbetap::OnbetapFilter::kLeakCornerHz / fc,
			                    1.f, onbetap::OnbetapFilter::kLeakBoostMax);
			float leak = 2.f * float(M_PI) * onbetap::OnbetapFilter::kLeakPoleHz / fsOs * boost;
			v.l.filter.setLeak(leak);
			v.r.filter.setLeak(leak);
		}
	}

	// One stereo side through the oversampled core. Returns output volts.
	float processSide(onbetap::Side& side, float inVolts, float g, float kEff,
	                  float driveScale, float makeup, float push) {
		float lp = 0.f, bp = 0.f, hp = 0.f;
		float x1 = inVolts * driveScale;
		if (activeOversample == 4) {
			// Two-stage decimation: fir4* (192k→96k) feeds the same
			// DecimFir13 stage the 2x path uses (96k→48k). Stage A's output
			// is only consumed on even substeps, so odd substeps just
			// advance its history.
			for (int i = 1; i <= 4; i++) {
				float x = side.xPrev + (x1 - side.xPrev) * (float)i * 0.25f;   // linear-interpolation upsample
				auto o = side.filter.processG(x, g, kEff);
				if ((i & 1) == 0) {
					float fl = side.firLp.push(side.fir4Lp.push(o.lp));
					float fb = side.firBp.push(side.fir4Bp.push(o.bp));
					float fh = side.firHp.push(side.fir4Hp.push(o.hp));
					if (i == 4) { lp = fl; bp = fb; hp = fh; }
				}
				else {
					side.fir4Lp.pushHistory(o.lp);
					side.fir4Bp.pushHistory(o.bp);
					side.fir4Hp.pushHistory(o.hp);
				}
			}
		}
		else {
			for (int i = 1; i <= 2; i++) {
				float x = side.xPrev + (x1 - side.xPrev) * (float)i * 0.5f;    // linear-interpolation upsample
				auto o = side.filter.processG(x, g, kEff);
				float fl = side.firLp.push(o.lp);
				float fb = side.firBp.push(o.bp);
				float fh = side.firHp.push(o.hp);
				if (i == 2) { lp = fl; bp = fb; hp = fh; }   // decimate: keep 1 of 2
			}
		}
		side.xPrev = x1;

		auto tap = [&](int mode) {
			switch (mode) {
				case 0: return lp;
				case 1: return bp;
				case 2: return hp;
				case 3: return lp + hp;      // notch
				default: return lp - hp;     // peak
			}
		};
		float y = (modeXf >= 1.f || vintageDrift)
			? tap(modeTarget)
			: tap(modeCurrent) + (tap(modeTarget) - tap(modeCurrent)) * modeXf;

		float v = side.dc.process(-y * makeup);   // AC-couple (rectification DC)
		return 9.f * onbetap::OnbetapFilter::tanhish(push * v * (1.f / 9.f));   // overdriven output VCA
	}

	void process(const ProcessArgs& args) override {
		if (oversample != activeOversample) {
			activeOversample = oversample;
			pool.resetAll();
			steps = modulationSteps;
		}

		int voices = std::max({1, inputs[AUDIO_L_INPUT].getChannels(),
		                       inputs[AUDIO_R_INPUT].getChannels()});
		pool.setVoices(voices);
		outputs[AUDIO_L_OUTPUT].setChannels(voices);
		outputs[AUDIO_R_OUTPUT].setChannels(voices);

		if (++steps >= modulationSteps) {
			steps = 0;
			modulate();
		}
		if (modeXf < 1.f)
			modeXf = std::min(1.f, modeXf + modeXfStep);
		dither = -dither;

		bool rConnected = inputs[AUDIO_R_INPUT].isConnected();

		for (int c = 0; c < voices; c++) {
			onbetap::OnbetapVoice& v = pool.voices[c];
			float g = v.gSlew.process(v.gTarget);
			float kBase = v.kSlew.process(v.kTarget);
			float drive = v.driveSlew.process(v.driveTarget);
			float makeup = v.makeupSlew.process(v.makeupTarget);
			float push = v.pushSlew.process(v.pushTarget);
			float kEff = std::max(kBase, -0.31f);   // denominator guard floor

			float inL = inputs[AUDIO_L_INPUT].getPolyVoltage(c) + dither;
			float outL = processSide(v.l, inL, g, kEff, drive, makeup, push);
			outputs[AUDIO_L_OUTPUT].setVoltage(outL, c);

			// An unpatched R input is normalled to L: mirror L instead of
			// running the core again (in Vintage a mono patch loses the L/R
			// decorrelation, which it doesn't need).
			float outR = outL;
			if (rConnected) {
				float inR = inputs[AUDIO_R_INPUT].getPolyVoltage(c) + dither;
				outR = processSide(v.r, inR, g * v.rGRatio, kEff, drive, makeup, push);
			}
			outputs[AUDIO_R_OUTPUT].setVoltage(outR, c);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "vintageDrift", json_boolean(vintageDrift));
		json_object_set_new(rootJ, "oversample", json_integer(oversample));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* vintageJ = json_object_get(rootJ, "vintageDrift");
		if (vintageJ)
			vintageDrift = json_boolean_value(vintageJ);
		// Older patches may say 1x, which is no longer offered; they get 2x.
		json_t* oversampleJ = json_object_get(rootJ, "oversample");
		if (oversampleJ) {
			int os = (int)json_integer_value(oversampleJ);
			oversample = (os == 2 || os == 4) ? os : kDefaultOversample;
		}
	}
};

struct OnbetapWidget : ModuleWidget {
	OnbetapWidget(Onbetap* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Onbetap.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions mirror res/Onbetap.svg (panel-specs/onbetap.yaml).
		// Knobs: Q (left) / Cutoff hero (centre) / Drive (right).
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.80, 40.500)), module, Onbetap::RES_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 33.500)), module, Onbetap::CUTOFF_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.00, 40.500)), module, Onbetap::DRIVE_PARAM));

		// Attenuverters, one per column.
		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.80, 52.500)), module, Onbetap::RES_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(25.40, 52.500)), module, Onbetap::CUTOFF_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(39.00, 52.500)), module, Onbetap::DRIVE_CV_PARAM));

		// CV jacks, one per column.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.80, 64.500)), module, Onbetap::RES_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.40, 64.500)), module, Onbetap::CUTOFF_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.00, 64.500)), module, Onbetap::DRIVE_INPUT));

		// Mode hero (snapping, via configSwitch).
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 86.333)), module, Onbetap::MODE_PARAM));

		// Stereo audio I/O.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.35, 114.017)), module, Onbetap::AUDIO_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.05, 114.017)), module, Onbetap::AUDIO_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.75, 114.017)), module, Onbetap::AUDIO_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.45, 114.017)), module, Onbetap::AUDIO_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Onbetap* module = getModule<Onbetap>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Character",
			{"Tamed (stable)", "Vintage (drift)"},
			[=]() { return module->vintageDrift ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "change character", [=]() { module->vintageDrift = (i == 1); });
			}));
		menu->addChild(createIndexSubmenuItem("Oversampling",
			{"CPU efficient (2x)", "High quality (4x)"},
			[=]() { return module->oversample == 4 ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set oversampling", [=]() { module->oversample = (i == 1) ? 4 : 2; });
			}));
	}
};

Model* modelOnbetap = createModel<Onbetap, OnbetapWidget>("Onbetap");
