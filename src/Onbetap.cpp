#include "plugin.hpp"
#include "onbetap/OnbetapFilter.hpp"
#include "onbetap/engine.hpp"
#include "onbetap/drive.hpp"
#include "mf20/dsp_utils.hpp"

// Onbetap — Polivoks-style multimode filter.
//
// Nonlinear TPT two-integrator core (OnbetapFilter), oversampled 2x by
// default, one core per stereo side per voice (OnbetapPool, up to 16 voices).
// Cutoff/resonance/drive are smoothed in the g/k/drive domain at ~2.5 ms
// intervals (modulate()); the audio path (process()/processSide()) only
// slews and runs the oversampled solve. Mode switching crossfades over 5 ms
// except in Vintage character, which switches hard (matches the factory
// panel switch). See docs/superpowers/specs/2026-07-15-onbetap-dsp-spec.md
// and docs/research/polivoks-*.md for the circuit derivation. Panel
// positions mirror res/Onbetap.svg (see panel-specs/onbetap.yaml).

// Drive-knob → gain mapping (input drive + output makeup) lives in
// onbetap/drive.hpp so the makeup formula is unit-tested directly. The output
// makeup is a CONSTANT buffer gain (Drive-independent): the core's rail clamping
// already compresses level, so a Drive-dependent makeup double-compensates —
// dropping level and stripping grit as Drive rises. drive knob [0,1] → input
// gain 0.25×…16× (−12…+24 dB, span baked at onbetap::kDriveSpanDb); output:
// volts = core × makeup, then VCA sat
// 9·tanhish(v/9). See docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md.
// A Drive-following push into the output VCA (drive.hpp vcaPush, quadratic in
// drive, bounded by the 9 V ceiling) keeps the top of the knob gaining grit
// while the authentic resonance choke removes the resonance-derived grit.
static constexpr float kCLag     = 0.25f;     // phase-lag: kEff -= cLag·g²/(1+g²)
static constexpr float kOnsetTrim = 0.045f;   // baked self-osc onset trim (by ear,
                                              // 2026-07-18): onset res ~0.72 → ~0.84
static constexpr float kVintageDriftOct = 0.12f;  // OU stationary std, calibrated Task 5
static constexpr float kVintageOffset   = 0.03f;  // node offset at 750 Hz, scales with log2 fc
static constexpr float kTwoPi = 6.28318530717959f;

struct Onbetap : Module {
	enum ParamId {
		CUTOFF_PARAM,
		CUTOFF_CV_PARAM,   // Cutoff CV attenuverter
		RES_PARAM,         // Resonance (labelled "Q" on the panel)
		RES_CV_PARAM,      // Resonance CV attenuverter
		DRIVE_PARAM,
		DRIVE_CV_PARAM,    // Drive CV attenuverter
		MODE_PARAM,        // 0=LP 1=BP 2=HP 3=Notch 4=Peak
		NUM_PARAMS
	};
	enum InputId {
		AUDIO_INPUT,
		AUDIO_INPUT_R,
		CUTOFF_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
		NUM_INPUTS
	};
	enum OutputId {
		AUDIO_OUTPUT,
		AUDIO_OUTPUT_R,
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	// Character: false = tamed (stable, playable), true = vintage (drifty,
	// untuned self-oscillation). Persisted now; acted on once the DSP lands.
	bool vintageDrift = false;

	OnbetapPool pool;
	onbetap::DriftWalker driftL { 0x0B617A01u };
	onbetap::DriftWalker driftR { 0x0B617A02u };

	// Soft (diode-clamp) limiting is the shipped default; Hard stays a menu
	// option. All tuning constants are baked — see onbetap/drive.hpp and
	// kOnsetTrim above.
	OnbetapFilter::Limit limitMode = OnbetapFilter::Limit::Soft;
	int oversample = 2;                             // 1 / 2 / 4

	int modulationSteps = 100, steps = 100;
	float sampleRate = 44100.f;
	float dither = 1e-9f;

	// Mode crossfade (5 ms): current/target mode + ramp position
	int modeCurrent = 0, modeTarget = 0;
	float modeXf = 1.f, modeXfStep = 1.f;

	// DC blocker coefficient (1.6 Hz highpass), computed from the host
	// sample rate (not the oversampled rate — the blocker runs once per
	// host sample, after decimation).
	float dcCoef = 1.f;

	Onbetap() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "Cutoff", " Hz", 2.f);
		configParam(CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "Cutoff CV", "x");
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(RES_CV_PARAM, -1.f, 1.f, 1.f, "Resonance CV", "x");
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 1.f, "Drive CV", "x");
		configSwitch(MODE_PARAM, 0.f, 4.f, 0.f, "Mode",
		             {"Lowpass", "Bandpass", "Highpass", "Notch", "Peak"});

		configInput(AUDIO_INPUT,   "Audio L");
		configInput(AUDIO_INPUT_R, "Audio R");
		configInput(CUTOFF_INPUT,  "Cutoff CV");
		configInput(RES_INPUT,     "Resonance CV");
		configInput(DRIVE_INPUT,   "Drive CV");

		configOutput(AUDIO_OUTPUT,   "Audio L");
		configOutput(AUDIO_OUTPUT_R, "Audio R");

		configBypass(AUDIO_INPUT,   AUDIO_OUTPUT);
		configBypass(AUDIO_INPUT_R, AUDIO_OUTPUT_R);

		// onSampleRateChange is triggered by Rack after construction; still
		// call configureRates here for host-free safety (tests, headless).
		configureRates(44100.f);
	}

	void configureRates(float fs) {
		sampleRate = fs;
		float alpha = smootherAlpha(fs, 0.005f);
		for (auto& v : pool.voices) v.setAlpha(alpha);
		modulationSteps = (int)(fs * 0.0025f);
		steps = modulationSteps;                     // modulate on first process()
		modeXfStep = 1.f / (0.005f * fs);
		dcCoef = 1.f - kTwoPi * 1.6f / fs;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		configureRates(e.sampleRate);
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		pool.resetAll();
		driftL.reset(); driftR.reset();
		steps = modulationSteps;                     // modulate on first process() after Reset
	}

	void modulate() {
		float fsOs = sampleRate * oversample;

		// Drift (Vintage only) — module-level, per side; both walkers advance
		// every block so toggling Character mid-patch is deterministic.
		float dL = driftL.step(0.0025f, kVintageDriftOct);
		float dR = driftR.step(0.0025f, kVintageDriftOct);
		if (!vintageDrift) dL = dR = 0.f;

		float cutoffLog = params[CUTOFF_PARAM].getValue();
		float resKnob   = params[RES_PARAM].getValue();
		float driveKnob = params[DRIVE_PARAM].getValue();
		float cutAtt = params[CUTOFF_CV_PARAM].getValue();
		float resAtt = params[RES_CV_PARAM].getValue();
		float drvAtt = params[DRIVE_CV_PARAM].getValue();
		bool cutCv = inputs[CUTOFF_INPUT].isConnected();
		bool resCv = inputs[RES_INPUT].isConnected();
		bool drvCv = inputs[DRIVE_INPUT].isConnected();

		// Mode target (snap knob); start crossfade on change
		int m = (int)std::round(params[MODE_PARAM].getValue());
		if (m != modeTarget) { modeCurrent = modeTarget; modeTarget = m; modeXf = 0.f; }

		for (int c = 0; c < pool.activeVoices; c++) {
			OnbetapVoice& v = pool.voices[c];
			v.sanitize();

			float voiceLog = cutoffLog;
			if (cutCv) voiceLog += cutAtt * inputs[CUTOFF_INPUT].getPolyVoltage(c);
			// per-side drift handled at the filter level via g scale below;
			// g target uses the L drift (R applies the delta as a ratio)
			float fc = std::exp2(voiceLog + dL);
			v.gTarget = OnbetapFilter::cutoffToG(fc, fsOs);

			float res = resKnob;
			if (resCv) res += resAtt * inputs[RES_INPUT].getPolyVoltage(c) * 0.2f;
			res = clamp(res, 0.f, 1.f);
			// rev-log damping map (+ baked onset trim → onset ~res 0.84);
			// min resonance Q≈1
			float k = -0.06f + 1.08f * std::pow(1.f - res, 2.3f) + kOnsetTrim;
			v.kTarget = k;   // phase-lag term applied per sample from slewed g

			float drive = driveKnob;
			if (drvCv) drive += drvAtt * inputs[DRIVE_INPUT].getPolyVoltage(c) * 0.2f;
			drive = clamp(drive, 0.f, 1.f);
			auto gains = onbetap::driveGains(drive, onbetap::kDriveSpanDb, 1.f,
			                                 0.f, onbetap::kDefaultGritDb);
			v.driveTarget  = gains.driveScale;
			v.makeupTarget = gains.makeup;
			v.pushTarget   = gains.vcaPush;

			// Character: mismatch + cutoff-scaled offset (vintage), R drift as
			// a relative g ratio so poly voices share one target.
			if (vintageDrift) {
				v.fL.setMismatch(onbetap::kMismatchL1, onbetap::kMismatchL2);
				v.fR.setMismatch(onbetap::kMismatchR1, onbetap::kMismatchR2);
				float offs = kVintageOffset * (voiceLog - std::log2(750.f)) * 0.25f;
				v.fL.setOffset(offs);
				v.fR.setOffset(-0.8f * offs);
				v.fRgRatio = std::exp2(dR - dL);
			} else {
				v.fL.setMismatch(0.f, 0.f);
				v.fR.setMismatch(0.f, 0.f);
				v.fL.setOffset(0.f);
				v.fR.setOffset(0.f);
				v.fRgRatio = 1.f;
			}
			v.fL.setLimit(limitMode);
			v.fR.setLimit(limitMode);
			float boost = clamp(OnbetapFilter::kLeakCornerHz / fc,
			                    1.f, OnbetapFilter::kLeakBoostMax);
			float leak = kTwoPi * OnbetapFilter::kLeakPoleHz / fsOs * boost;
			v.fL.setLeak(leak);
			v.fR.setLeak(leak);
		}
	}

	// One stereo side through the oversampled core. Returns output volts.
	// fir{Lp,Bp,Hp} are only touched (and only meaningful) on the 2x path;
	// the 1x path bypasses them entirely; on the 4x path they serve as
	// stage B behind the fir4* stage-A decimators.
	float processSide(OnbetapFilter& flt, float& xPrev, DCBlock& dc, float inVolts,
	                  float g, float kEff, float driveScale, float makeup, float push,
	                  DecimFir13& firLp, DecimFir13& firBp, DecimFir13& firHp,
	                  DecimFir9& fir4Lp, DecimFir9& fir4Bp, DecimFir9& fir4Hp) {
		float lp = 0, bp = 0, hp = 0;
		float x1 = inVolts * driveScale;
		if (oversample == 4) {
			// 4x: two-stage decimation — fir4* (DecimFir9, 192k→96k) feeds
			// the same DecimFir13 stage the 2x path uses (96k→48k), so the
			// 4x passband matches 2x by construction. See engine.hpp for
			// the folding-band math and design provenance.
			for (int i = 1; i <= 4; i++) {
				float t = (float)i / 4.f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				float al = fir4Lp.push(o.lp);
				float ab = fir4Bp.push(o.bp);
				float ah = fir4Hp.push(o.hp);
				if ((i & 1) == 0) {                  // 96k instants: substeps 2, 4
					float fl = firLp.push(al);
					float fb = firBp.push(ab);
					float fh = firHp.push(ah);
					if (i == 4) { lp = fl; bp = fb; hp = fh; }
				}
			}
		} else if (oversample == 2) {
			// 2x: 13-tap decimation FIR (see engine.hpp DecimFir13) replaces
			// the crude 2-tap boxcar average, which under-attenuates the
			// alias band and both droops the top octave and lets content
			// above the new Nyquist fold back down (measured, Task 5).
			for (int i = 1; i <= 2; i++) {
				float t = (float)i / 2.f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				float fl = firLp.push(o.lp);
				float fb = firBp.push(o.bp);
				float fh = firHp.push(o.hp);
				if (i == 2) { lp = fl; bp = fb; hp = fh; }  // decimate: keep 1 of 2
			}
		} else {
			for (int i = 1; i <= oversample; i++) {
				float t = (float)i / oversample;
				float x = xPrev + (x1 - xPrev) * t;      // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				lp += o.lp; bp += o.bp; hp += o.hp;      // average = crude decimator
			}
			float inv = 1.f / oversample;
			lp *= inv; bp *= inv; hp *= inv;
		}
		xPrev = x1;

		// taps + 5 ms crossfade on mode change (Vintage: hard switch, DC step
		// and all, like the factory panel switch)
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

		float v = -y * makeup;
		v = dc.process(v, dcCoef);                   // AC-couple (rectification DC)
		return 9.f * OnbetapFilter::tanhish(push * v / 9.f); // "overdriven VCA" stage
	}

	void process(const ProcessArgs& args) override {
		int voices = std::max({1, inputs[AUDIO_INPUT].getChannels(),
		                          inputs[AUDIO_INPUT_R].getChannels()});
		pool.setVoices(voices);
		outputs[AUDIO_OUTPUT].setChannels(voices);
		outputs[AUDIO_OUTPUT_R].setChannels(voices);

		if (++steps >= modulationSteps) { steps = 0; modulate(); }
		if (modeXf < 1.f) modeXf = std::min(1.f, modeXf + modeXfStep);
		dither = -dither;

		bool rConnected = inputs[AUDIO_INPUT_R].isConnected();

		for (int c = 0; c < voices; c++) {
			OnbetapVoice& v = pool.voices[c];
			float g      = v.gSlew.process(v.gTarget);
			float kBase  = v.kSlew.process(v.kTarget);
			float drive  = v.driveSlew.process(v.driveTarget);
			float makeup = v.makeupSlew.process(v.makeupTarget);
			float push   = v.pushSlew.process(v.pushTarget);
			float kEff   = kBase - kCLag * g * g / (1.f + g * g);
			kEff = std::max(kEff, -0.31f);           // denominator guard floor

			float inL = inputs[AUDIO_INPUT].getPolyVoltage(c) + dither;
			float outL = processSide(v.fL, v.xPrevL, v.dcL, inL, g, kEff, drive, makeup,
			                         push, v.firLpL, v.firBpL, v.firHpL,
			                         v.fir4LpL, v.fir4BpL, v.fir4HpL);
			outputs[AUDIO_OUTPUT].setVoltage(outL, c);

			if (rConnected) {
				float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c) + dither;
				float outR = processSide(v.fR, v.xPrevR, v.dcR, inR,
				                         g * v.fRgRatio, kEff, drive, makeup, push,
				                         v.firLpR, v.firBpR, v.firHpR,
				                         v.fir4LpR, v.fir4BpR, v.fir4HpR);
				outputs[AUDIO_OUTPUT_R].setVoltage(outR, c);
			} else {
				// R normalled to L → mirror L (skips a full core solve; in
				// Vintage this loses the L/R decorrelation, which is fine for
				// a mono patch)
				outputs[AUDIO_OUTPUT_R].setVoltage(outL, c);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "vintageDrift", json_boolean(vintageDrift));
		json_object_set_new(root, "limitMode", json_integer((int)limitMode));
		json_object_set_new(root, "oversample", json_integer(oversample));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* v = json_object_get(root, "vintageDrift");
		if (v)
			vintageDrift = json_boolean_value(v);
		json_t* lm = json_object_get(root, "limitMode");
		if (lm)
			limitMode = (json_integer_value(lm) == (int)OnbetapFilter::Limit::Soft)
			          ? OnbetapFilter::Limit::Soft : OnbetapFilter::Limit::Hard;
		json_t* os = json_object_get(root, "oversample");
		if (os) {
			int v = (int)json_integer_value(os);
			oversample = (v == 1 || v == 2 || v == 4) ? v : 2;
		}
		// tune* keys from the removed Tuning menu (including tuneOnset) are
		// deliberately ignored — the voicing is baked now.
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

		// Knobs: Q (left) / Cutoff hero (centre) / Drive (right).
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.80, 37.000)), module, Onbetap::RES_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 30.000)), module, Onbetap::CUTOFF_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.00, 37.000)), module, Onbetap::DRIVE_PARAM));

		// Attenuverters, one per column.
		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.80, 49.000)), module, Onbetap::RES_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(25.40, 49.000)), module, Onbetap::CUTOFF_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(39.00, 49.000)), module, Onbetap::DRIVE_CV_PARAM));

		// CV jacks, one per column.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.80, 67.350)), module, Onbetap::RES_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.40, 67.350)), module, Onbetap::CUTOFF_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.00, 67.350)), module, Onbetap::DRIVE_INPUT));

		// Mode hero (snap enabled via configSwitch).
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 89.833)), module, Onbetap::MODE_PARAM));

		// Stereo audio I/O.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.35, 114.017)), module, Onbetap::AUDIO_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.05, 114.017)), module, Onbetap::AUDIO_INPUT_R));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.75, 114.017)), module, Onbetap::AUDIO_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.45, 114.017)), module, Onbetap::AUDIO_OUTPUT_R));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Onbetap*>(module);
		if (!m) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Character"));
		menu->addChild(createMenuItem("Tamed (stable)",
			!m->vintageDrift ? "✓" : "",
			[m]() { m->vintageDrift = false; }));
		menu->addChild(createMenuItem("Vintage (drift)",
			m->vintageDrift ? "✓" : "",
			[m]() { m->vintageDrift = true; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Resonance limiting"));
		menu->addChild(createMenuItem("Soft (diode clamp)",
			m->limitMode == OnbetapFilter::Limit::Soft ? "✓" : "",
			[m]() { m->limitMode = OnbetapFilter::Limit::Soft; }));
		menu->addChild(createMenuItem("Hard (factory rails)",
			m->limitMode == OnbetapFilter::Limit::Hard ? "✓" : "",
			[m]() { m->limitMode = OnbetapFilter::Limit::Hard; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Oversampling",
			{"1x", "2x", "4x"},
			[m]() { return m->oversample == 1 ? 0 : m->oversample == 2 ? 1 : 2; },
			[m](int i) {
				m->oversample = (i == 0) ? 1 : (i == 1) ? 2 : 4;
				m->pool.resetAll();
			}));
	}
};


Model* modelOnbetap = createModel<Onbetap, OnbetapWidget>("Onbetap");
