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
		MIX_OUTPUT, MIX_OUTPUT_R,   // Blend of LP/HP (notch at centre)
		HP_OUTPUT,  HP_OUTPUT_R,
		BP_OUTPUT,  BP_OUTPUT_R,
		LP_OUTPUT,  LP_OUTPUT_R,
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	// Character: false = British (the original EDP Wasp — overload limiter on, no
	// self-oscillation); true = German (the Doepfer A-124 mod — limiter off,
	// self-oscillates). Named Tame/Screaming, then EDP/Doepfer, before 2026-07-19;
	// the legacy "screaming"/"doepfer" patch keys are still read (see dataFromJson).
	bool german = false;

	// Panel theme: 0 = charcoal (default), 1 = gold. Persisted; the widget reads
	// it to pick which faceplate SVG to draw. MetaModule is locked to charcoal:
	// no Panel menu there, and dataFromJson ignores the key.
	int panelTheme = 0;

	// Per-voice DSP state (up to 16 voices), stereo, oversampled.
	wasp::EnginePool _pool;

	// Rate divider — modulate() runs every ~2.5 ms instead of every sample.
	int _modulationSteps = 100;
	int _steps = 100;  // >= _modulationSteps: first process() modulates immediately

	float _sampleRate = 44100.f;   // host rate; mirrors the engine rate for modulate-rate math

	// Oversampling: 0 = auto (4x <=48k, 2x <=96k, 1x above), else 1/2/4 forced.
	// MetaModule has no Auto entry (see appendContextMenu) and defaults to 1x:
	// headless-simulator timing (tests/vespid/mm-sim-notes.md) showed 4x costing
	// ~7x what 1x costs on the Cortex-A7 core — halfband resampler overhead, not
	// just the 4x extra WaspFilter::process() calls — and true-stereo patches pay
	// double a mono/normalled-R patch. 2x and 4x stay available from the menu.
#if defined(METAMODULE)
	static constexpr int kDefaultOsMenu = 1;
#else
	static constexpr int kDefaultOsMenu = 0;   // Auto
#endif
	int _osMenu = kDefaultOsMenu;
	int _osActual = kDefaultOsMenu == 0 ? 4 : kDefaultOsMenu;  // resolved factor currently applied to the pool

	// Inverter bandwidth (Hz) feeding the kC2eff self-oscillation term.
	// Baked per mode (2026-07-19, was a 30-220 kHz menu slider):
	// German 50 kHz — eager self-oscillation, onset well under a second
	// at mid/high cutoffs, reaching down to ~300-400 Hz cutoffs; British
	// 60 kHz — its free-run threshold sits at ~55-60 kHz
	// (fitted_constants.md), so 60 keeps the no-self-oscillation promise.
	// See 2026-07-19-vespid-selfosc-onset-design.md (baked addendum).
	static constexpr float kFPoleGerman = 50000.f;
	static constexpr float kFPoleBritish      = 60000.f;

	// Self-oscillation pitch tracking: false = hardware-accurate (drifts flat
	// at high resonance in German mode), true = corrected to track the knob.
	bool _oscPitchCorrected = false;

	// Blend (LP<->HP crossfade) is shared across voices (no per-voice CV path
	// in the panel), so it gets a single module-level smoother rather than
	// living in EnginePool.
	OnePoleSmoother _blendSlew { 0.5f };
	float _blendTarget = 0.5f;

	// Deterministic noise-floor seed: alternates sign each sample (cheaper
	// than an RNG and bit-reproducible VCV vs MetaModule). Does double duty:
	// denormal prevention, and seeding self-oscillation growth. 1e-4 V
	// (~0.1 mV, -94 dBV — inaudible, and it sits at Nyquist) approximates a
	// real circuit's noise floor; the old 1e-9 seed made German mode's
	// exponential onset pay for ~5 extra decades of growth (measured ~40%
	// slower — see 2026-07-19-vespid-selfosc-onset-design.md).
	float _dither = 1e-4f;

	Vespid() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(FREQ_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "Frequency", " Hz", 2.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(BLEND_PARAM, 0.f, 1.f, 0.5f, "Blend: LP/notch/HP");
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
		configOutput(MIX_OUTPUT,   "Mix L");
		configOutput(MIX_OUTPUT_R, "Mix R");

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
			// No Auto entry on MetaModule, so a 0 can only reach here from a
			// desktop patch; it lands on the MM default of 1x (see kDefaultOsMenu).
			os = 1;
#else
			os = (_sampleRate <= 48000.f) ? 4 : (_sampleRate <= 96000.f) ? 2 : 1;
#endif
		}
		_osActual = os;

		float fsInt = _sampleRate * (float)os;
		_pool.setSampleRates(_sampleRate, fsInt);

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
		const wasp::ModeConfig& mode = german ? wasp::kGerman : wasp::kBritish;
		float fsInt = _sampleRate * (float)_osActual;

		float fPole = german ? kFPoleGerman : kFPoleBritish;

		float freqLog     = params[FREQ_PARAM].getValue();
		float freqCvAtten = params[FREQ_CV_PARAM].getValue();
		float res         = clamp(params[RES_PARAM].getValue(), 0.f, 1.f);
		float resCvAtten  = params[RES_CV_PARAM].getValue();
		float driveKnob   = clamp(params[DRIVE_PARAM].getValue(), 0.f, 1.f);
		float driveCvAtten = params[DRIVE_CV_PARAM].getValue();

		bool freqCvConn  = inputs[FREQ_INPUT].isConnected();
		bool resCvConn   = inputs[RES_INPUT].isConnected();
		bool driveCvConn = inputs[DRIVE_INPUT].isConnected();

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

			// Self-oscillation pitch correction (German mode only): the
			// rail-bounded limit cycle runs ~0.72x the knob's small-signal
			// placement at rho=1; blend toward a corrected scale as rho
			// approaches 1 so the perceived pitch tracks the knob there.
			if (_oscPitchCorrected && german) {
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
			if (resCvConn)
				rho += resCvAtten * inputs[RES_INPUT].getPolyVoltage(c) / 10.f;
			rho = clamp(rho, 0.f, 1.f);
			eng.rhoTarget = rho;

			// Drive: knob 0..1 -> 2x..64x (2x fixed pre-gain, 30 dB span),
			// times the per-mode hardware level staging (mode.inGain). At
			// drive 0 a 5 V signal lands at German mode's Euro-hot staging
			// (10 V eq., clean, onset ~6% up the knob) and British mode's
			// EDP-nominal 2.5 V (the original's light rasp, ~12% THD). See
			// 2026-07-19-vespid-drive-remap-design.md and
			// 2026-07-19-vespid-input-calibration-design.md.
			float drive01 = driveKnob;
			if (driveCvConn)
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
		_blendTarget = clamp(blend, 0.f, 1.f);
	}

	// Process one voice (channel c). Advances its smoothers and runs the
	// audio cascade for L, and for R unless it's normalled to L. The shared
	// module-level Blend smoother is advanced once per sample in process() —
	// not here — so every voice sees the same value and the slew time
	// constant doesn't shrink with the voice count. maskL/maskR select which
	// signals each Channel decimates (built once per sample in process()).
	void processChannel(int c, float m, bool rConnected, int maskL, int maskR,
	                    float makeup) {
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
		wasp::Channel::Out oL =
			eng.l.process(inL * drive + _dither, _osActual, g, h1, kC2,
			              maskL, m, makeup);

		outputs[LP_OUTPUT].setVoltage(oL.lp, c);
		outputs[BP_OUTPUT].setVoltage(oL.bp, c);
		outputs[HP_OUTPUT].setVoltage(oL.hp, c);
		outputs[MIX_OUTPUT].setVoltage(oL.mix, c);

		if (rConnected) {
			// True stereo: process R through its own filter/resampler chain.
			float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c);
			wasp::Channel::Out oR =
				eng.r.process(inR * drive + _dither, _osActual, g, h1, kC2,
				              maskR, m, makeup);
			outputs[LP_OUTPUT_R].setVoltage(oR.lp, c);
			outputs[BP_OUTPUT_R].setVoltage(oR.bp, c);
			outputs[HP_OUTPUT_R].setVoltage(oR.hp, c);
			outputs[MIX_OUTPUT_R].setVoltage(oR.mix, c);
		} else {
			// R normalled to L: mirror L's outputs, skip the R compute
			// entirely (matches the MF-20 mono-patch optimization). maskL
			// already includes the R jacks' needs in this case.
			outputs[LP_OUTPUT_R].setVoltage(oL.lp, c);
			outputs[BP_OUTPUT_R].setVoltage(oL.bp, c);
			outputs[HP_OUTPUT_R].setVoltage(oL.hp, c);
			outputs[MIX_OUTPUT_R].setVoltage(oL.mix, c);
		}
	}

	void process(const ProcessArgs& args) override {
		int channels = std::max({1, inputs[AUDIO_INPUT].getChannels(),
		                            inputs[AUDIO_INPUT_R].getChannels()});
		_pool.setVoices(channels);

		// setChannels only needs to run when the voice count changes, not
		// 8 outputs x every sample.
		if (outputs[LP_OUTPUT].getChannels() != channels)
			for (int out = 0; out < NUM_OUTPUTS; out++)
				outputs[out].setChannels(channels);

		if (++_steps >= _modulationSteps) {
			_steps = 0;
			modulate();
		}

		_dither = -_dither;
		// Blend smoother advances once per sample (MF-20 pattern), not per
		// voice — see processChannel's comment. The stereo check and the
		// output-select masks are hoisted out of the voice loop (Onbetap
		// already hoists its stereo check).
		float m = _blendSlew.process(_blendTarget);
		bool rConnected = inputs[AUDIO_INPUT_R].isConnected();
		// Only connected outputs pay for decimation/DC blocking. When R is
		// normalled to L, the R jacks are fed from the L channel's outputs,
		// so their needs fold into maskL.
		int maskL = (outputs[LP_OUTPUT].isConnected()  ? wasp::kOutLp  : 0)
		          | (outputs[BP_OUTPUT].isConnected()  ? wasp::kOutBp  : 0)
		          | (outputs[HP_OUTPUT].isConnected()  ? wasp::kOutHp  : 0)
		          | (outputs[MIX_OUTPUT].isConnected() ? wasp::kOutMix : 0);
		int maskR = (outputs[LP_OUTPUT_R].isConnected()  ? wasp::kOutLp  : 0)
		          | (outputs[BP_OUTPUT_R].isConnected()  ? wasp::kOutBp  : 0)
		          | (outputs[HP_OUTPUT_R].isConnected()  ? wasp::kOutHp  : 0)
		          | (outputs[MIX_OUTPUT_R].isConnected() ? wasp::kOutMix : 0);
		if (!rConnected) {
			maskL |= maskR;
			maskR = 0;
		}
		float makeup = (german ? wasp::kGerman : wasp::kBritish).makeup;
		for (int c = 0; c < channels; c++)
			processChannel(c, m, rConnected, maskL, maskR, makeup);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "german", json_boolean(german));
		json_object_set_new(root, "panelTheme", json_integer(panelTheme));
		json_object_set_new(root, "osMenu", json_integer(_osMenu));
		json_object_set_new(root, "oscPitchCorrected", json_boolean(_oscPitchCorrected));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "german");
		if (!s)
			s = json_object_get(root, "doepfer");     // pre-rename patches...
		if (!s)
			s = json_object_get(root, "screaming");   // ...and pre-pre-rename
		if (s)
			german = json_boolean_value(s);
#if !defined(METAMODULE)
		// Desktop only: MetaModule is locked to charcoal, so a patch carrying
		// the key can't unlock it there — the member initializer stands.
		json_t* p = json_object_get(root, "panelTheme");
		if (p)
			panelTheme = json_integer_value(p);
#endif
		json_t* om = json_object_get(root, "osMenu");
		if (om)
			_osMenu = json_integer_value(om);
		// Validate loaded values that feed DSP invariants (a hand-edited or
		// corrupted patch must not desync the internal rate from the cascade
		// or invert clamp bounds). Mirrors the menu Quantity setters' ranges.
#if defined(METAMODULE)
		// 0 (Auto) is not a legal selection on MetaModule — a desktop patch
		// carrying it, like any corrupt value, lands on the 1x default.
		if (_osMenu != 1 && _osMenu != 2 && _osMenu != 4)
			_osMenu = kDefaultOsMenu;
#else
		if (_osMenu != 0 && _osMenu != 1 && _osMenu != 2 && _osMenu != 4)
			_osMenu = kDefaultOsMenu;
#endif
		// Older patches carry "inputTrimDb"/"outputLevelDb" (Input trim and
		// Output level menu sliders, removed — both were unity by default), an
		// "fPole" key (Inverter bandwidth slider, removed 2026-07-19 — baked
		// per mode above), and a "highAcc" key (Accuracy menu, removed — the
		// Newton refinement is now always on in every build; see
		// WaspFilter.hpp); all are ignored.
		json_t* pc = json_object_get(root, "oscPitchCorrected");
		if (pc)
			_oscPitchCorrected = json_boolean_value(pc);
		// Missing keys already default correctly via member initializers
		// above; re-derive the oversampling factor in case osMenu changed
		// (onSampleRateChange may or may not have run yet at this point).
		updateOversampling();
	}
};



struct VespidWidget : ModuleWidget {
	app::SvgPanel* panel = nullptr;

#if !defined(METAMODULE)
	// Swap the faceplate SVG for the selected theme (0 = charcoal, 1 = gold).
	// Screws stay dark (ScrewBlack) on both themes. Desktop only: MetaModule is
	// locked to charcoal, so it never leaves the panel set below.
	void setPanelTheme(int t) {
		std::string f = (t == 1) ? "res/Vespid-gold.svg" : "res/Vespid.svg";
		panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, f)));
	}
#endif

	VespidWidget(Vespid* module) {
		setModule(module);
		panel = createPanel(asset::plugin(pluginInstance, "res/Vespid.svg"));
		setPanel(panel);
#if !defined(METAMODULE)
		if (module)
			setPanelTheme(module->panelTheme);
#endif

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
		menu->addChild(createMenuItem("British (limited)",
			!m->german ? "✓" : "",
			[m]() { m->german = false; }));
		menu->addChild(createMenuItem("German (self-oscillates)",
			m->german ? "✓" : "",
			[m]() { m->german = true; }));

		menu->addChild(new MenuSeparator);
#if defined(METAMODULE)
		// No Auto on MetaModule: it would only ever resolve to the 1x default
		// there, so the menu is the three explicit factors (matching Onbetap).
		menu->addChild(createIndexSubmenuItem("Oversampling",
			{"1x", "2x", "4x"},
			[m]() -> size_t {
				switch (m->_osMenu) {
					case 2: return 1;
					case 4: return 2;
					default: return 0;
				}
			},
			[m](size_t i) {
				static const int kValues[3] = {1, 2, 4};
				m->_osMenu = kValues[i];
				m->updateOversampling();
			}));
#else
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
#endif

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Self-oscillation pitch (German)"));
		menu->addChild(createMenuItem("Hardware (drifts flat)",
			!m->_oscPitchCorrected ? "✓" : "",
			[m]() { m->_oscPitchCorrected = false; }));
		menu->addChild(createMenuItem("Corrected (tracks knob)",
			m->_oscPitchCorrected ? "✓" : "",
			[m]() { m->_oscPitchCorrected = true; }));

#if !defined(METAMODULE)
		// Desktop only: MetaModule is locked to the charcoal faceplate.
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Panel",
			{"Charcoal", "Gold"},
			[m]() -> size_t { return m->panelTheme; },
			[m, this](size_t i) { m->panelTheme = (int)i; setPanelTheme((int)i); }));
#endif
	}
};

Model* modelVespid = createModel<Vespid, VespidWidget>("Vespid");
