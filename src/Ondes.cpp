#include "plugin.hpp"
#include "host/ondes_voice.h"
#include "stmlib/dsp/hysteresis_quantizer.h"

#include <atomic>

// Ondes: the wavetable voice of Mutable Instruments Beads (the oscillator
// Beads plays when nothing is patched into its audio inputs): 12 banks of 16
// band-limited waves, with Position morphing through a bank and Bank
// switching banks with a short crossfade. The context menu can oversample the
// voice (ondes_voice.h).

static constexpr int kNumBanks = 12;   // beads/dsp/synth.h kNumBanks
static constexpr int kTableSize = beads::kTableSize;
static constexpr int kNumWaves = beads::kNumWavesPerTable;
static_assert(WAV_INTEGRATED_WAVES_SIZE == kNumBanks * kNumWaves * (kTableSize + 4), "Beads wavetable layout");

// Ondes keeps VCV's 0 V = C4 tuning; Beads' voice is rooted on C3.
static constexpr float kTuningSemitones = 12.f;
// Beads' Digital (16-bit) setting; Ondes has no Quality control.
static constexpr int kBitShift = 16;

// One cycle of the wave the voice plays at this bank and position, scaled to
// its own peak, for the display. Beads stores each wave integrated, so the
// shape is the table's first difference.
static void waveTrace(int bank, float position, float trace[kTableSize]) {
	const float wave = clamp(position, 0.f, 1.f) * (kNumWaves - 1);
	const int waveIntegral = std::min((int)wave, kNumWaves - 2);
	const float waveFractional = wave - waveIntegral;
	const int16_t* a = beads::wav_integrated_waves + (bank * kNumWaves + waveIntegral) * (kTableSize + 4);
	const int16_t* b = a + kTableSize + 4;
	float peak = 1e-6f;
	for (int i = 0; i < kTableSize; i++) {
		const float da = float(a[i + 2]) - float(a[i + 1]);
		const float db = float(b[i + 2]) - float(b[i + 1]);
		trace[i] = da + (db - da) * waveFractional;
		peak = std::max(peak, std::fabs(trace[i]));
	}
	for (int i = 0; i < kTableSize; i++)
		trace[i] /= peak;
}

struct Ondes : Module {
	enum ParamId {
		PITCH_PARAM,
		POSITION_PARAM,
		POSITION_CV_PARAM,
		BANK_PARAM,
		BANK_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT,
		BANK_INPUT,
		POSITION_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	robotboy_beads::OndesVoice voice;
	stmlib::HysteresisQuantizer2 bankQuantizer;
	float sampleRate = 48000.f;
	// Anti-aliasing (context menu): the voice runs at this multiple of the
	// host rate (1 = off, as on Beads; 2 or 4).
	int oversampling = 1;
	// The bank and position last played, for the display (UI thread).
	std::atomic<int> displayBank{0};
	std::atomic<float> displayPosition{0.f};

	Ondes() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// A plain linear +-24 semitones: no notches, unlike the Beads pitch
		// knob on Particules and Retours.
		configParam(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch", " st", 0.f, 48.f, -24.f)->displayPrecision = 3;
		configParam(POSITION_PARAM, 0.f, 1.f, 0.f, "Position");
		configParam(POSITION_CV_PARAM, -1.f, 1.f, 0.f, "Position CV amount");
		configParam(BANK_PARAM, 0.f, 1.f, 0.f, "Bank");
		configParam(BANK_CV_PARAM, -1.f, 1.f, 0.f, "Bank CV amount");
		configInput(VOCT_INPUT, "1V/octave pitch");
		configInput(POSITION_INPUT, "Position CV");
		configInput(BANK_INPUT, "Bank CV");
		configOutput(OUT_OUTPUT, "Audio out");
		initVoice(APP->engine->getSampleRate());
	}

	void initVoice(float rate) {
		sampleRate = rate;
		voice.Init(sampleRate, oversampling);
		// Beads' synth: 12 banks, 0.048 of hysteresis between them.
		bankQuantizer.Init(kNumBanks, 0.048f, false);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		initVoice(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		oversampling = 1;
	}

	void process(const ProcessArgs& args) override {
		float pitch = (params[PITCH_PARAM].getValue() - 0.5f) * 48.f
			+ inputs[VOCT_INPUT].getVoltage() * 12.f + kTuningSemitones;
		// Beads' SemitonesToRatio table covers +-128 semitones.
		pitch = std::isfinite(pitch) ? clamp(pitch, -120.f, 120.f) : 0.f;
		float position = params[POSITION_PARAM].getValue()
			+ inputs[POSITION_INPUT].getVoltage() * 0.2f * params[POSITION_CV_PARAM].getValue();
		position = std::isfinite(position) ? clamp(position, 0.f, 1.f) : 0.f;
		float bank = params[BANK_PARAM].getValue()
			+ inputs[BANK_INPUT].getVoltage() * 0.2f * params[BANK_CV_PARAM].getValue();
		bank = std::isfinite(bank) ? clamp(bank, 0.f, 1.f) : 0.f;

		// A menu change restarts the voice at its new rate.
		if (oversampling != voice.oversampling())
			voice.Init(sampleRate, oversampling);

		const int bankIndex = bankQuantizer.Process(bank);
		displayBank.store(bankIndex, std::memory_order_relaxed);
		displayPosition.store(position, std::memory_order_relaxed);

		outputs[OUT_OUTPUT].setVoltage(voice.Render(bankIndex, kBitShift, pitch, position) * 5.f);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "oversampling", json_integer(oversampling));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// Patches from before the option load with it off.
		json_t* j = json_object_get(rootJ, "oversampling");
		const int factor = j ? (int)json_integer_value(j) : 1;
		oversampling = factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
	}
};

// The wave Ondes is playing, as a cyan trace that morphs with Bank and
// Position (and their CV). In the module browser: bank 1, first wave.
struct WavetableDisplay : LedDisplay {
	Ondes* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			float trace[kTableSize];
			if (module)
				waveTrace(clamp(module->displayBank.load(std::memory_order_relaxed), 0, kNumBanks - 1),
					module->displayPosition.load(std::memory_order_relaxed), trace);
			else
				waveTrace(0, 0.f, trace);

			nvgScissor(args.vg, RECT_ARGS(args.clipBox));
			const Rect scope = Rect(Vec(0, 0), box.size).shrink(Vec(4, 5));
			nvgBeginPath(args.vg);
			for (int i = 0; i <= kTableSize; i++) {
				const Vec p = scope.pos + scope.size.mult(Vec(float(i) / kTableSize, 0.5f - 0.5f * trace[i % kTableSize]));
				if (i == 0)
					nvgMoveTo(args.vg, VEC_ARGS(p));
				else
					nvgLineTo(args.vg, VEC_ARGS(p));
			}
			nvgLineCap(args.vg, NVG_ROUND);
			nvgMiterLimit(args.vg, 2.f);
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStrokeColor(args.vg, nvgRGB(0x00, 0xe5, 0xff));
			nvgStroke(args.vg);
			nvgResetScissor(args.vg);
		}
		LedDisplay::drawLayer(args, layer);
	}
};

struct OndesWidget : ModuleWidget {
	OndesWidget(Ondes* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Ondes.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		WavetableDisplay* display = createWidget<WavetableDisplay>(mm2px(Vec(3.0, 9.4)));
		display->box.size = mm2px(Vec(34.64, 24.5));
		display->module = module;
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.145, 51.35)), module, Ondes::PITCH_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(26.495, 51.35)), module, Ondes::VOCT_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.66, 74.35)), module, Ondes::BANK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(28.98, 74.35)), module, Ondes::POSITION_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.66, 86.7)), module, Ondes::BANK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(28.98, 86.7)), module, Ondes::POSITION_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.66, 97.05)), module, Ondes::BANK_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(28.98, 97.05)), module, Ondes::POSITION_CV_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(20.32, 113.7)), module, Ondes::OUT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Ondes* module = getModule<Ondes>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		// ASCII labels: the MetaModule font has no multiplication sign.
		static const int kFactors[] = {1, 2, 4};
		menu->addChild(createIndexSubmenuItem("Anti-aliasing",
			{"Off (as on Beads)", "2x oversampling", "4x oversampling"},
			[=]() { return module->oversampling == 4 ? 2 : module->oversampling == 2 ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set anti-aliasing", [=]() { module->oversampling = kFactors[i]; });
			}));
	}
};

Model* modelOndes = createModel<Ondes, OndesWidget>("Ondes");
