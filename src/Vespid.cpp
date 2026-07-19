#include "plugin.hpp"
#include "vespid/engine.hpp"

// Vespid — EDP Wasp-style CMOS state-variable filter (stereo).
//
// DSP: wasp::WaspFilter (src/vespid/WaspFilter.hpp) driven through an
// oversampled wasp::EnginePool (src/vespid/engine.hpp), following the
// MF-20 control-rate pattern (src/mf20/MF20Filter.cpp): params/CVs are read
// every ~2.5 ms in modulate(), audio-rate smoothers slew the resulting
// targets per sample. Panel positions mirror res/Vespid.svg (see
// panel-specs/vespid.yaml) and are untouched here.

struct Vespid : Module {
	enum ParamId {
		FREQ_PARAM,
		RES_PARAM,
		DRIVE_PARAM,
		BLEND_PARAM,       // LP <-> notch <-> HP crossfade on the Mix output
		FREQ_CV_PARAM,     // Frequency CV attenuverter
		RES_CV_PARAM,      // Resonance CV attenuverter
		DRIVE_CV_PARAM,    // Drive CV attenuverter
		NUM_PARAMS
	};
	enum InputId {
		AUDIO_INPUT,
		AUDIO_INPUT_R,
		FREQ_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
		BLEND_INPUT,
		NUM_INPUTS
	};
	enum OutputId {
		LP_OUTPUT,  LP_OUTPUT_R,
		BP_OUTPUT,  BP_OUTPUT_R,
		HP_OUTPUT,  HP_OUTPUT_R,
		MIX_OUTPUT, MIX_OUTPUT_R,   // Blend of LP/HP (notch at centre)
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	// Character: false = tamed (overload limiter on, no self-oscillation, like
	// the original EDP Wasp); true = screaming (limiter off, self-oscillates).
	// Persisted now; acted on once the DSP lands.
	bool screaming = false;

	// Panel theme: 0 = charcoal (default), 1 = gold. Persisted; the widget reads
	// it to pick which faceplate SVG to draw.
	int panelTheme = 0;

	// Per-voice DSP state (up to 16 voices), stereo, oversampled.
	wasp::EnginePool _pool;

	// Rate divider — modulate() runs every ~2.5 ms instead of every sample.
	int _modulationSteps = 100;
	int _steps = 100;  // >= _modulationSteps: first process() modulates immediately

	float _sampleRate = 44100.f;   // host rate; mirrors the engine rate for modulate-rate math

	// Oversampling: 0 = auto (4x <=48k, 2x <=96k, 1x above), else 1/2/4 forced.
	int _osMenu = 0;
	int _osActual = 4;             // resolved factor currently applied to the pool

	// Accuracy: pivot-only (false) vs pivot + 2 Newton iterations (true).
	bool _highAcc = true;

	// Input trim (dB), folded into the drive gain at modulate rate.
	float _inputTrimDb = 0.f;

	// Output level (dB), applied as a post-gain to every output (both
	// modes) — the rebalancing tool for the Tame/Screaming loudness gap
	// left by the per-mode makeup constants (see WaspFilter.hpp). Computed
	// at modulate rate as a linear gain target, slewed like the other
	// module-level shared control (Blend, below), applied after the filter
	// so it never disturbs the Mix output's notch structure.
	float _outputLevelDb = 0.f;

	// Inverter bandwidth (Hz) feeding the kC2eff self-oscillation term.
	float _fPole = 80000.f;

	// Self-oscillation pitch tracking: false = hardware-accurate (drifts flat
	// at high resonance in Screaming), true = corrected to track the knob.
	bool _oscPitchCorrected = false;

	// Blend (LP<->HP crossfade) is shared across voices (no per-voice CV path
	// in the panel), so it gets a single module-level smoother rather than
	// living in EnginePool.
	OnePoleSmoother _blendSlew { 0.5f };
	float _blendTarget = 0.5f;

	// Output-level gain: same "shared, module-level, no per-voice CV"
	// pattern as Blend above.
	OnePoleSmoother _outputLevelSlew { 1.f };
	float _outputLevelTarget = 1.f;

	// Deterministic denormal-prevention dither: alternates sign each sample
	// (cheaper than an RNG and bit-reproducible VCV vs MetaModule).
	float _dither = 1e-9f;

	Vespid() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(FREQ_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "Frequency", " Hz", 2.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(BLEND_PARAM, 0.f, 1.f, 0.5f, "Blend (LP–notch–HP)");
		configParam(FREQ_CV_PARAM,  -1.f, 1.f, 1.f, "Frequency CV", "x");
		configParam(RES_CV_PARAM,   -1.f, 1.f, 1.f, "Resonance CV", "x");
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 1.f, "Drive CV", "x");

		configInput(AUDIO_INPUT,   "Audio L");
		configInput(AUDIO_INPUT_R, "Audio R");
		configInput(FREQ_INPUT,  "Frequency CV");
		configInput(RES_INPUT,   "Resonance CV");
		configInput(DRIVE_INPUT, "Drive CV");
		configInput(BLEND_INPUT, "Blend CV");

		configOutput(LP_OUTPUT,   "Lowpass L");
		configOutput(LP_OUTPUT_R, "Lowpass R");
		configOutput(BP_OUTPUT,   "Bandpass L");
		configOutput(BP_OUTPUT_R, "Bandpass R");
		configOutput(HP_OUTPUT,   "Highpass L");
		configOutput(HP_OUTPUT_R, "Highpass R");
		configOutput(MIX_OUTPUT,   "Mix L (LP–notch–HP blend)");
		configOutput(MIX_OUTPUT_R, "Mix R (LP–notch–HP blend)");

		// Bypass routes the dry audio input to every output.
		for (int out : {LP_OUTPUT, BP_OUTPUT, HP_OUTPUT, MIX_OUTPUT})
			configBypass(AUDIO_INPUT, out);
		for (int out : {LP_OUTPUT_R, BP_OUTPUT_R, HP_OUTPUT_R, MIX_OUTPUT_R})
			configBypass(AUDIO_INPUT_R, out);
	}

	// Recompute the oversampling factor and push it (plus the current host
	// rate) down into the pool. Called on sample-rate change and whenever the
	// Oversampling menu selection changes.
	void updateOversampling() {
		int os = _osMenu;
		if (os == 0) {
#if defined(METAMODULE)
			// MetaModule's Cortex-A7 core is far weaker than a desktop CPU.
			// Headless-simulator timing (tests/vespid/mm-sim-notes.md)
			// showed 4x oversampling costing ~7x what 1x costs (halfband
			// resampler overhead, not just the 4x extra WaspFilter::process()
			// calls), and true-stereo patches pay double a mono/normalled-R
			// patch. Auto stays conservative at 2x here; users who want 4x on
			// MM can still pick it explicitly from the Oversampling menu.
			os = (_sampleRate <= 96000.f) ? 2 : 1;
#else
			os = (_sampleRate <= 48000.f) ? 4 : (_sampleRate <= 96000.f) ? 2 : 1;
#endif
		}
		_osActual = os;

		float fsInt = _sampleRate * (float)os;
		_pool.setSampleRate(fsInt);

		float alpha = smootherAlpha(_sampleRate, 0.005f);  // 5 ms, at host rate
		for (auto& eng : _pool.engines) {
			eng.gSlew.setAlpha(alpha);
			eng.kC2Slew.setAlpha(alpha);
			eng.rhoSlew.setAlpha(alpha);
			eng.driveSlew.setAlpha(alpha);
			eng.beta0Slew.setAlpha(alpha);
			eng.beta1Slew.setAlpha(alpha);
			eng.alpha1Slew.setAlpha(alpha);
			// Resampler histories don't survive a rate/factor change; filter
			// core state (bp/lp/etc.) carries across it fine.
			eng.resetResamplers();
		}
		_blendSlew.setAlpha(alpha);
		_outputLevelSlew.setAlpha(alpha);

		// Force the next process() to re-run modulate() immediately: the
		// g/kC2/H1 targets all depend on the internal rate, so a menu-driven
		// os change must not run on stale-rate coefficients for ~2.5 ms.
		_steps = _modulationSteps;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		_sampleRate = e.sampleRate;
		_modulationSteps = std::max(1, (int)(_sampleRate * 0.0025f));  // 2.5 ms
		updateOversampling();  // also resets _steps for an immediate modulate()
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		_pool.resetAll();
	}

	// Called at ~2.5 ms intervals: reads params/CVs, updates per-voice
	// smoother targets. Division-heavy math (tan, computeH1) lives here, not
	// in the audio path.
	void modulate() {
		const wasp::ModeConfig& mode = screaming ? wasp::kScreaming : wasp::kTame;
		float fsInt = _sampleRate * (float)_osActual;

		float freqLog     = params[FREQ_PARAM].getValue();
		float freqCvAtten = params[FREQ_CV_PARAM].getValue();
		float res         = clamp(params[RES_PARAM].getValue(), 0.f, 1.f);
		float resCvAtten  = params[RES_CV_PARAM].getValue();
		float driveKnob   = clamp(params[DRIVE_PARAM].getValue(), 0.f, 1.f);
		float driveCvAtten = params[DRIVE_CV_PARAM].getValue();

		bool freqCvConn  = inputs[FREQ_INPUT].isConnected();
		bool resCvConn   = inputs[RES_INPUT].isConnected();
		bool driveCvConn = inputs[DRIVE_INPUT].isConnected();

		// Input trim is a fixed (non-CV) menu setting -> shared gain factor.
		float trimGain = std::pow(10.f, _inputTrimDb / 20.f);

		for (int c = 0; c < _pool.activeVoices; c++) {
			wasp::VoiceEngine& eng = _pool.engines[c];
			eng.sanitize();

			eng.l.filt.setMode(mode);
			eng.r.filt.setMode(mode);

			// Frequency: 1 V/oct CV on the log2 knob value, then to Hz,
			// clamped [1 Hz, 0.45*fsInt] before the wcComp/internal clamps.
			float fcLog = freqLog;
			if (freqCvConn)
				fcLog += freqCvAtten * inputs[FREQ_INPUT].getPolyVoltage(c);
			float fc = clamp(std::exp2(fcLog), 1.f, 0.45f * fsInt);

			float fcInt = fc * mode.wcComp;

			// Self-oscillation pitch correction (Screaming only): the
			// rail-bounded limit cycle runs ~0.72x the knob's small-signal
			// placement at rho=1; blend toward a corrected scale as rho
			// approaches 1 so the perceived pitch tracks the knob there.
			if (_oscPitchCorrected && screaming) {
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
			               - mode.kR2 * wcInt / (2.f * float(M_PI) * _fPole);

			// Resonance.
			float rho = res;
			if (resCvConn)
				rho += resCvAtten * inputs[RES_INPUT].getPolyVoltage(c) / 10.f;
			rho = clamp(rho, 0.f, 1.f);
			eng.rhoTarget = rho;

			// Drive: knob 0..1 -> 1x..8x, times the fixed input-trim gain.
			float drive01 = driveKnob;
			if (driveCvConn)
				drive01 += driveCvAtten * inputs[DRIVE_INPUT].getPolyVoltage(c) / 10.f;
			drive01 = clamp(drive01, 0.f, 1.f);
			eng.driveTarget = std::exp2(3.f * drive01) * trimGain;

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
		_blendTarget = clamp(blend, 0.f, 1.f);

		// Output level: fixed (non-CV) menu setting, post-filter gain.
		_outputLevelTarget = std::pow(10.f, _outputLevelDb / 20.f);
	}

	// Process one voice (channel c). Advances its smoothers and runs the
	// audio cascade for L, and for R unless it's normalled to L. The shared
	// module-level smoothers (blend, output level) are advanced once per
	// sample in process() — not here — so every voice sees the same value
	// and the slew time constant doesn't shrink with the voice count.
	void processChannel(int c, float m, float outGain) {
		wasp::VoiceEngine& eng = _pool.engines[c];

		float g     = eng.gSlew.process(eng.gTarget);
		float kC2   = eng.kC2Slew.process(eng.kC2Target);
		float drive = eng.driveSlew.process(eng.driveTarget);
		// rhoSlew doesn't feed the audio path directly (H1's own coefficient
		// smoothers do that) but is kept current every sample so modulate()
		// can read back a smoothed resonance for the self-osc pitch formula.
		eng.rhoSlew.process(eng.rhoTarget);
		wasp::H1Coeffs h1{
			eng.beta0Slew.process(eng.beta0Target),
			eng.beta1Slew.process(eng.beta1Target),
			eng.alpha1Slew.process(eng.alpha1Target)
		};

		float inL = inputs[AUDIO_INPUT].getPolyVoltage(c);
		wasp::WaspFilter::Out oL =
			eng.l.process(inL * drive + _dither, _osActual, g, h1, kC2, _highAcc);

		outputs[LP_OUTPUT].setVoltage(outGain * oL.lp, c);
		outputs[BP_OUTPUT].setVoltage(outGain * oL.bp, c);
		outputs[HP_OUTPUT].setVoltage(outGain * oL.hp, c);
		outputs[MIX_OUTPUT].setVoltage(outGain * ((1.f - m) * oL.lp + m * oL.hp), c);

		if (inputs[AUDIO_INPUT_R].isConnected()) {
			// True stereo: process R through its own filter/resampler chain.
			float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c);
			wasp::WaspFilter::Out oR =
				eng.r.process(inR * drive + _dither, _osActual, g, h1, kC2, _highAcc);
			outputs[LP_OUTPUT_R].setVoltage(outGain * oR.lp, c);
			outputs[BP_OUTPUT_R].setVoltage(outGain * oR.bp, c);
			outputs[HP_OUTPUT_R].setVoltage(outGain * oR.hp, c);
			outputs[MIX_OUTPUT_R].setVoltage(outGain * ((1.f - m) * oR.lp + m * oR.hp), c);
		} else {
			// R normalled to L: mirror L's outputs, skip the R compute
			// entirely (matches the MF-20 mono-patch optimization).
			outputs[LP_OUTPUT_R].setVoltage(outGain * oL.lp, c);
			outputs[BP_OUTPUT_R].setVoltage(outGain * oL.bp, c);
			outputs[HP_OUTPUT_R].setVoltage(outGain * oL.hp, c);
			outputs[MIX_OUTPUT_R].setVoltage(outGain * ((1.f - m) * oL.lp + m * oL.hp), c);
		}
	}

	void process(const ProcessArgs& args) override {
		int channels = std::max({1, inputs[AUDIO_INPUT].getChannels(),
		                            inputs[AUDIO_INPUT_R].getChannels()});
		_pool.setVoices(channels);

		for (int out = 0; out < NUM_OUTPUTS; out++)
			outputs[out].setChannels(channels);

		if (++_steps >= _modulationSteps) {
			_steps = 0;
			modulate();
		}

		_dither = -_dither;
		// Shared smoothers advance once per sample (MF-20 pattern), not per
		// voice — see processChannel's comment.
		float m = _blendSlew.process(_blendTarget);
		float outGain = _outputLevelSlew.process(_outputLevelTarget);
		for (int c = 0; c < channels; c++)
			processChannel(c, m, outGain);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "screaming", json_boolean(screaming));
		json_object_set_new(root, "panelTheme", json_integer(panelTheme));
		json_object_set_new(root, "highAcc", json_boolean(_highAcc));
		json_object_set_new(root, "osMenu", json_integer(_osMenu));
		json_object_set_new(root, "inputTrimDb", json_real(_inputTrimDb));
		json_object_set_new(root, "outputLevelDb", json_real(_outputLevelDb));
		json_object_set_new(root, "fPole", json_real(_fPole));
		json_object_set_new(root, "oscPitchCorrected", json_boolean(_oscPitchCorrected));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "screaming");
		if (s)
			screaming = json_boolean_value(s);
		json_t* p = json_object_get(root, "panelTheme");
		if (p)
			panelTheme = json_integer_value(p);
		json_t* ha = json_object_get(root, "highAcc");
		if (ha)
			_highAcc = json_boolean_value(ha);
		json_t* om = json_object_get(root, "osMenu");
		if (om)
			_osMenu = json_integer_value(om);
		// Validate loaded values that feed DSP invariants (a hand-edited or
		// corrupted patch must not desync the internal rate from the cascade
		// or invert clamp bounds). Mirrors the menu Quantity setters' ranges.
		if (_osMenu != 0 && _osMenu != 1 && _osMenu != 2 && _osMenu != 4)
			_osMenu = 0;
		json_t* it = json_object_get(root, "inputTrimDb");
		if (it)
			_inputTrimDb = clamp((float)json_real_value(it), -12.f, 12.f);
		json_t* ol = json_object_get(root, "outputLevelDb");
		if (ol)
			_outputLevelDb = clamp((float)json_real_value(ol), -12.f, 12.f);
		json_t* fp = json_object_get(root, "fPole");
		if (fp)
			_fPole = clamp((float)json_real_value(fp), 60000.f, 220000.f);
		json_t* pc = json_object_get(root, "oscPitchCorrected");
		if (pc)
			_oscPitchCorrected = json_boolean_value(pc);
		// Missing keys already default correctly via member initializers
		// above; re-derive the oversampling factor in case osMenu changed
		// (onSampleRateChange may or may not have run yet at this point).
		updateOversampling();
	}
};


// Menu-slider Quantities (VCV desktop only — MetaModule's context menu has no
// ui::Slider widget; see the #ifndef METAMODULE guards below, same pattern as
// Particules' ManualGainSlider/Quantity).

// Input trim: linear dB, ±12 dB, default 0.
struct InputTrimQuantity : Quantity {
	Vespid* module;
	InputTrimQuantity(Vespid* m) : module(m) {}
	void setValue(float value) override {
		if (module)
			module->_inputTrimDb = clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override { return module ? module->_inputTrimDb : getDefaultValue(); }
	float getMinValue() override { return -12.f; }
	float getMaxValue() override { return 12.f; }
	float getDefaultValue() override { return 0.f; }
	std::string getLabel() override { return "Input trim"; }
	std::string getUnit() override { return " dB"; }
	std::string getDisplayValueString() override { return string::f("%.1f", getValue()); }
};

// Output level: linear dB, ±12 dB, default 0. Post-filter gain applied to
// every output, both modes — same shape as InputTrimQuantity above.
struct OutputLevelQuantity : Quantity {
	Vespid* module;
	OutputLevelQuantity(Vespid* m) : module(m) {}
	void setValue(float value) override {
		if (module)
			module->_outputLevelDb = clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override { return module ? module->_outputLevelDb : getDefaultValue(); }
	float getMinValue() override { return -12.f; }
	float getMaxValue() override { return 12.f; }
	float getDefaultValue() override { return 0.f; }
	std::string getLabel() override { return "Output level"; }
	std::string getUnit() override { return " dB"; }
	std::string getDisplayValueString() override { return string::f("%.1f", getValue()); }
};

// Inverter bandwidth: log-scaled 60 kHz - 220 kHz, default 80 kHz. The ceiling
// is 220 kHz because Screaming's self-oscillation threshold is ~218 kHz
// (kC2 = wc*(R3*C2 - kR2/(2*pi*fPole)) crosses zero there); the floor is
// 60 kHz because Tame free-runs below ~55-60 kHz. The Quantity's own
// value/min/max live in log2(kHz) space so the slider position is
// linear-in-log; getDisplayValue/getDisplayValueString convert back to kHz.
struct FPoleQuantity : Quantity {
	Vespid* module;
	FPoleQuantity(Vespid* m) : module(m) {}
	void setValue(float value) override {
		if (module)
			module->_fPole = 1000.f * std::pow(2.f, clamp(value, getMinValue(), getMaxValue()));
	}
	float getValue() override {
		return module ? std::log2(module->_fPole / 1000.f) : getDefaultValue();
	}
	float getMinValue() override { return std::log2(60.f); }
	float getMaxValue() override { return std::log2(220.f); }
	float getDefaultValue() override { return std::log2(80.f); }
	float getDisplayValue() override { return std::pow(2.f, getValue()); }
	void setDisplayValue(float displayValue) override { setValue(std::log2(displayValue)); }
	std::string getLabel() override { return "Inverter bandwidth"; }
	std::string getUnit() override { return " kHz"; }
	std::string getDisplayValueString() override { return string::f("%.1f", getDisplayValue()); }
};

#ifndef METAMODULE
struct InputTrimSlider : ui::Slider {
	InputTrimSlider(InputTrimQuantity* q) {
		quantity = q;
		box.size.x = 200.f;
	}
	~InputTrimSlider() { delete quantity; }
};

struct FPoleSlider : ui::Slider {
	FPoleSlider(FPoleQuantity* q) {
		quantity = q;
		box.size.x = 200.f;
	}
	~FPoleSlider() { delete quantity; }
};

struct OutputLevelSlider : ui::Slider {
	OutputLevelSlider(OutputLevelQuantity* q) {
		quantity = q;
		box.size.x = 200.f;
	}
	~OutputLevelSlider() { delete quantity; }
};
#endif

struct VespidWidget : ModuleWidget {
	app::SvgPanel* panel = nullptr;

	// Swap the faceplate SVG for the selected theme (0 = charcoal, 1 = gold).
	// Screws stay dark (ScrewBlack) on both themes.
	void setPanelTheme(int t) {
		std::string f = (t == 1) ? "res/Vespid-gold.svg" : "res/Vespid.svg";
		panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, f)));
	}

	VespidWidget(Vespid* module) {
		setModule(module);
		panel = createPanel(asset::plugin(pluginInstance, "res/Vespid.svg"));
		setPanel(panel);
		if (module)
			setPanelTheme(module->panelTheme);

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
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.550, 69.550)), module, Vespid::AUDIO_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.250, 69.550)), module, Vespid::AUDIO_INPUT_R));

		// Lower zone. Left column: Blend CV / Blend knob / Mix. Right column: HP / BP / LP.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.480, 86.750)), module, Vespid::BLEND_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.450, 86.750)), module, Vespid::HP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.150, 86.750)), module, Vespid::HP_OUTPUT_R));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(13.497, 101.950)), module, Vespid::BLEND_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.447, 101.950)), module, Vespid::BP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.147, 101.950)), module, Vespid::BP_OUTPUT_R));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.650, 117.150)), module, Vespid::MIX_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(18.350, 117.150)), module, Vespid::MIX_OUTPUT_R));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.450, 117.150)), module, Vespid::LP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.150, 117.150)), module, Vespid::LP_OUTPUT_R));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Vespid*>(module);
		if (!m) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Character"));
		menu->addChild(createMenuItem("Tame (limited)",
			!m->screaming ? "✓" : "",
			[m]() { m->screaming = false; }));
		menu->addChild(createMenuItem("Screaming (self-oscillates)",
			m->screaming ? "✓" : "",
			[m]() { m->screaming = true; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Accuracy"));
		menu->addChild(createMenuItem("Standard",
			!m->_highAcc ? "✓" : "",
			[m]() { m->_highAcc = false; }));
		menu->addChild(createMenuItem("High (default)",
			m->_highAcc ? "✓" : "",
			[m]() { m->_highAcc = true; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Oversampling",
			{"Auto", "1x", "2x", "4x"},
			[m]() -> size_t {
				switch (m->_osMenu) {
					case 1: return 1;
					case 2: return 2;
					case 4: return 3;
					default: return 0;
				}
			},
			[m](size_t i) {
				static const int kValues[4] = {0, 1, 2, 4};
				m->_osMenu = kValues[i];
				m->updateOversampling();
			}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Self-oscillation pitch (Screaming)"));
		menu->addChild(createMenuItem("Hardware (drifts flat)",
			!m->_oscPitchCorrected ? "✓" : "",
			[m]() { m->_oscPitchCorrected = false; }));
		menu->addChild(createMenuItem("Corrected (tracks knob)",
			m->_oscPitchCorrected ? "✓" : "",
			[m]() { m->_oscPitchCorrected = true; }));

		menu->addChild(new MenuSeparator);
#ifdef METAMODULE
		// MetaModule's context menu has no ui::Slider widget (see the
		// InputTrimSlider/OutputLevelSlider/FPoleSlider #ifndef METAMODULE guards
		// above), so MM gets discrete-choice submenus instead of continuous
		// sliders — same precedent as Particules' manual-gain menu
		// (src/particules/Particules.cpp, the `#ifdef METAMODULE` 0-32 dB list
		// under ManualGainItem). Persistence is unchanged: both paths write/read
		// the same _inputTrimDb/_outputLevelDb/_fPole floats and JSON fields, so
		// patches roundtrip identically between hosts.
		menu->addChild(createIndexSubmenuItem("Input trim",
			{"-12 dB", "-6 dB", "0 dB", "+6 dB", "+12 dB"},
			[m]() -> size_t {
				static const float kValues[5] = {-12.f, -6.f, 0.f, 6.f, 12.f};
				size_t best = 2;
				float bestDiff = std::fabs(kValues[2] - m->_inputTrimDb);
				for (size_t i = 0; i < 5; i++) {
					float diff = std::fabs(kValues[i] - m->_inputTrimDb);
					if (diff < bestDiff) { bestDiff = diff; best = i; }
				}
				return best;
			},
			[m](size_t i) {
				static const float kValues[5] = {-12.f, -6.f, 0.f, 6.f, 12.f};
				m->_inputTrimDb = kValues[i];
			}));
		menu->addChild(createIndexSubmenuItem("Output level",
			{"-12 dB", "-6 dB", "0 dB", "+6 dB", "+12 dB"},
			[m]() -> size_t {
				static const float kValues[5] = {-12.f, -6.f, 0.f, 6.f, 12.f};
				size_t best = 2;
				float bestDiff = std::fabs(kValues[2] - m->_outputLevelDb);
				for (size_t i = 0; i < 5; i++) {
					float diff = std::fabs(kValues[i] - m->_outputLevelDb);
					if (diff < bestDiff) { bestDiff = diff; best = i; }
				}
				return best;
			},
			[m](size_t i) {
				static const float kValues[5] = {-12.f, -6.f, 0.f, 6.f, 12.f};
				m->_outputLevelDb = kValues[i];
			}));
		menu->addChild(createIndexSubmenuItem("Inverter bandwidth",
			{"60 kHz", "80 kHz", "120 kHz", "160 kHz", "220 kHz"},
			[m]() -> size_t {
				static const float kValues[5] = {60000.f, 80000.f, 120000.f, 160000.f, 220000.f};
				size_t best = 1;
				float bestDiff = std::fabs(kValues[1] - m->_fPole);
				for (size_t i = 0; i < 5; i++) {
					float diff = std::fabs(kValues[i] - m->_fPole);
					if (diff < bestDiff) { bestDiff = diff; best = i; }
				}
				return best;
			},
			[m](size_t i) {
				static const float kValues[5] = {60000.f, 80000.f, 120000.f, 160000.f, 220000.f};
				m->_fPole = kValues[i];
			}));
#else
		menu->addChild(new InputTrimSlider(new InputTrimQuantity(m)));
		menu->addChild(new OutputLevelSlider(new OutputLevelQuantity(m)));
		menu->addChild(new FPoleSlider(new FPoleQuantity(m)));
#endif

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Panel",
			{"Charcoal", "Gold"},
			[m]() -> size_t { return m->panelTheme; },
			[m, this](size_t i) { m->panelTheme = (int)i; setPanelTheme((int)i); }));
	}
};

Model* modelVespid = createModel<Vespid, VespidWidget>("Vespid");
