#include "plugin.hpp"
#include "ShapeGenerator.hpp"
#include "tune/FrequencyTracker.hpp"
#include <atomic>

// Tune: a tuner and scope. The pitch tracking and the scope's trigger are
// the CV Funk Tuner's (src/tune/, see its NOTICE.md), one channel of it with
// its OFFSET and GAIN knobs fixed at their defaults. Its WIDTH knob is DIV.

struct Tune : Module {
	enum ParamId {
		WIDTH_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		AUDIO_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		FREQ_OUTPUT,
		AUDIO_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// CV Funk's knob defaults: OFFSET at 0 (it subtracts 0.05 V). GAIN was 1.
	static constexpr float kOffset = -0.05f;
	// Cycles across the scope with DIV at its default
	static constexpr float kDefaultCycles = 2.f;
	static constexpr int kScopePoints = 1024;
	// Samples between checks of the input and updates of V/OCT and the readout
	static constexpr int kControlInterval = 100;
	// Autocorrelation lags per tracker call, as in every CV Funk speed setting
	static constexpr int kLagsPerCall = 8;
	// Analysis samples skipped between tracker calls, in menu order
	static constexpr int kSkipChoices[4] = {0, 1, 3, 7};

	DualRangeTracker tracker;
	PhaseAccumulatorTrigger scopeTrigger;
	// The input in volts / 2, as many cycles of it as DIV sets. Written by the audio thread and
	// drawn by the UI without a lock: a torn frame is one bad picture.
	CircularBuffer<float, kScopePoints> scope;
	// The tracked frequency for the readout; below 0 when there's no lock
	std::atomic<float> displayHz{-1.f};

	float sampleRate = 48000.f;
	int controlCounter = 0;
	bool inputConnected = false;
	float cycles = kDefaultCycles;
	float prevIn = 0.f;
	bool capturing = false;
	float captureProgress = 0.f;
	int prevScopeIndex = 0;

	// Context menu setting, saved with the patch
	int analysisSkip = 1;

	Tune() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(WIDTH_PARAM, 0.f, 5.f, 0.f, "Scope width",
			{"2 cycles", "4 cycles", "6 cycles", "8 cycles", "10 cycles", "12 cycles"});
		configInput(AUDIO_INPUT, "Audio");
		configOutput(FREQ_OUTPUT, "Frequency (V/oct)");
		configOutput(AUDIO_OUTPUT, "Audio");
		configBypass(AUDIO_INPUT, AUDIO_OUTPUT);
		for (int i = 0; i < kScopePoints; i++)
			scope[i] = 0.f;
		setRate(APP->engine->getSampleRate());
	}

	void setRate(float rate) {
		sampleRate = rate;
		tracker.setSampleRate(rate);
		// setSampleRate resets the trackers' settings
		tracker.setLagsPerCall(kLagsPerCall);
		tracker.setAnalysisSkip(analysisSkip);
	}

	void setAnalysisSkip(int skip) {
		analysisSkip = skip;
		tracker.setAnalysisSkip(skip);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		setRate(e.sampleRate);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "analysisSkip", json_integer(analysisSkip));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* j = json_object_get(rootJ, "analysisSkip");
		int skip = j ? (int) json_integer_value(j) : 1;
		analysisSkip = 1;
		for (int choice : kSkipChoices)
			if (skip == choice)
				analysisSkip = choice;
		tracker.setAnalysisSkip(analysisSkip);
	}

	bool locked(float hz) const {
		return hz > 0.0001f;
	}

	void process(const ProcessArgs& args) override {
		// OUT is IN, untouched, so the tuner can sit in the middle of a cable run
		Input& audio = inputs[AUDIO_INPUT];
		outputs[AUDIO_OUTPUT].setChannels(audio.getChannels());
		outputs[AUDIO_OUTPUT].writeVoltages(audio.getVoltages());

		bool control = ++controlCounter >= kControlInterval;
		if (control) {
			controlCounter = 0;
			inputConnected = audio.isConnected();
			cycles = 2.f * (1.f + std::round(params[WIDTH_PARAM].getValue()));
		}

		float in = 0.f;
		if (inputConnected)
			in = clamp(audio.getVoltage(0) + kOffset, -10.f, 10.f);

		float hz = tracker.process(in);

		if (control) {
			// 0 V = C4
			outputs[FREQ_OUTPUT].setVoltage(hz > 0.f ? std::log2(hz / dsp::FREQ_C4) : 0.f);
			displayHz.store(hz, std::memory_order_relaxed);
		}

		// The scope: a capture starts once per `cycles` periods, lined up with
		// a rising zero crossing, and writes those periods across the buffer.
		scopeTrigger.update(hz, sampleRate, cycles, in, prevIn);
		prevIn = in;

		if (scopeTrigger.triggered) {
			capturing = true;
			captureProgress = 0.f;
			prevScopeIndex = 0;
		}

		if (capturing) {
			float inc = hz > 0.f ? kScopePoints * hz / (cycles * sampleRate) : 1.f;
			// Saturate one slot past the end, so the last slot is always
			// reached before the next trigger restarts the frame
			captureProgress = std::min(captureProgress + inc, (float) kScopePoints);

			int index = clamp((int) captureProgress, 0, kScopePoints - 1);
			float sample = in * 0.5f;
			scope[index] = sample;

			// High pitches advance several slots a sample: fill the gap
			if (index != prevScopeIndex) {
				int from = prevScopeIndex;
				float fromValue = scope[from];
				if (from < index) {
					int gap = index - from;
					for (int i = 1; i < gap; i++)
						scope[from + i] = fromValue + (sample - fromValue) * i / gap;
				}
				else {
					// Wrapped around the end of the buffer
					int toEnd = kScopePoints - 1 - from;
					for (int i = 1; i <= toEnd; i++)
						scope[from + i] = fromValue + (sample - fromValue) * i / (toEnd + 1);
					for (int i = 0; i <= index; i++)
						scope[i] = fromValue + (sample - fromValue) * (i + 1) / (index + 1);
				}
				prevScopeIndex = index;
			}
		}
	}
};

constexpr int Tune::kSkipChoices[4];

namespace {

// Cents either side of a note that count as in tune
constexpr int kInTuneCents = 2;

NVGcolor inTuneColor() {
	return nvgRGB(0x6f, 0xdc, 0x7a);
}
NVGcolor offPitchColor() {
	return nvgRGB(0xff, 0x9d, 0x3c);
}
NVGcolor scopeColor() {
	return nvgRGB(0x5e, 0xc8, 0xff);
}

void fillRect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor color) {
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w, h);
	nvgFillColor(vg, shapeui::screenColor(color));
	nvgFill(vg);
}

}  // namespace

// The upper window: note name and octave, cents off the note, frequency, and
// a meter spanning 50 cents either side. Laid out in mm for its 54 x 40.5 box.
struct TuneReadout : shapeui::Screen {
	Tune* module = nullptr;

	TuneReadout() {
		// The plates' corner radius
		radiusMm = 2.f;
	}

	void drawScreen(NVGcontext* vg) override {
		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (!font)
			return;
		nvgFontFaceId(vg, font->handle);

		// The module browser shows A4, in tune
		float hz = module ? module->displayHz.load(std::memory_order_relaxed) : 440.f;
		bool locked = module ? module->locked(hz) : true;

		const float pad = mm2px(3.24f);
		const float right = box.size.x - pad;
		const float noteSize = mm2px(18.6f);
		const float octaveSize = mm2px(9.3f);
		const float centsSize = mm2px(7.f);
		const float hzSize = mm2px(4.4f);
		const float base = mm2px(18.6f);

		// The meter: a tick every 10 cents, the centre one taller and brighter
		const float meterY = mm2px(34.f);
		const float tick = (right - pad) / 10.f;
		const float centre = box.size.x / 2.f;
		const float hair = mm2px(0.3f);
		fillRect(vg, pad, meterY - hair / 2.f, right - pad, hair, shapeui::mutedColor());
		for (int i = -5; i <= 5; i++) {
			float half = mm2px(i == 0 ? 2.2f : (i == -5 || i == 5) ? 1.4f : 0.8f);
			fillRect(vg, centre + i * tick - hair / 2.f, meterY - half, hair, 2.f * half,
				i == 0 ? shapeui::inkColor() : shapeui::mutedColor());
		}

		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		shapeui::fontSize(vg, noteSize);
		if (!locked) {
			nvgFillColor(vg, shapeui::screenColor(shapeui::dividerColor()));
			nvgText(vg, pad, base, "--", nullptr);
			return;
		}

		static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		float midi = 69.f + 12.f * std::log2(hz / 440.f);
		int note = (int) std::lround(midi);
		float centsOff = (midi - note) * 100.f;
		int cents = (int) std::lround(centsOff);
		// + 1200 keeps the division and remainder positive below C-1
		const char* name = kNames[(note + 1200) % 12];
		int octave = (note + 1200) / 12 - 101;
		NVGcolor color = std::abs(cents) <= kInTuneCents ? inTuneColor() : offPitchColor();

		nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
		nvgText(vg, pad, base, name, nullptr);

		shapeui::fontSize(vg, octaveSize);
		nvgFillColor(vg, shapeui::screenColor(shapeui::mutedColor()));
		nvgText(vg, pad + shapeui::textWidth(noteSize, strlen(name)) + mm2px(0.4f), base,
			string::f("%d", octave).c_str(), nullptr);

		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		shapeui::fontSize(vg, hzSize);
		nvgText(vg, right, base, string::f("%.1f Hz", hz).c_str(), nullptr);

		shapeui::fontSize(vg, centsSize);
		nvgFillColor(vg, shapeui::screenColor(color));
		nvgText(vg, right, base - mm2px(7.8f), cents == 0 ? "0" : string::f("%+d", cents).c_str(), nullptr);

		// The marker follows the exact pitch, not the rounded number
		const float markerW = mm2px(1.f), markerH = mm2px(5.2f);
		fillRect(vg, centre + centsOff / 10.f * tick - markerW / 2.f, meterY - markerH / 2.f,
			markerW, markerH, color);
	}
};

// The lower window: a few cycles of the input, +-10 V top to bottom.
struct TuneScope : shapeui::Screen {
	Tune* module = nullptr;

	// Scope slots drawn per point. MetaModule draws a quarter of them.
#ifdef METAMODULE
	static constexpr int kStep = 4;
#else
	static constexpr int kStep = 1;
#endif
	// The first and last 4 slots can hold the previous frame, so they're skipped
	static constexpr int kSkip = 4;
	static constexpr int kPoints = (Tune::kScopePoints - 2 * kSkip) / kStep;

	TuneScope() {
		radiusMm = 2.f;
	}

	void drawScreen(NVGcontext* vg) override {
		const float pad = mm2px(1.f);
		const float w = box.size.x - 2.f * pad;
		const float hair = mm2px(0.2f);
		fillRect(vg, pad, box.size.y / 2.f - hair / 2.f, w, hair, shapeui::dividerColor());

		float values[kPoints];
		bool locked = !module || module->displayHz.load(std::memory_order_relaxed) >= 0.f;
		for (int k = 0; k < kPoints; k++) {
			if (!locked)
				values[k] = 0.f;
			else if (module)
				values[k] = module->scope[kSkip + k * kStep] / 5.f;
			else
				// The module browser's picture: a 5 V sine
				values[k] = 0.5f * std::sin(2.f * M_PI * Tune::kDefaultCycles * k / (kPoints - 1));
		}

		shapeui::TraceStyle style;
		style.color = scopeColor();
		style.closed = false;
		shapeui::drawTrace(vg, values, kPoints, pad, 0.f, w, box.size.y, style);
	}
};

struct TuneWidget : ModuleWidget {
	TuneWidget(Tune* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Tune.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		TuneReadout* readout = createWidget<TuneReadout>(mm2px(Vec(3.48, 14.0)));
		readout->box.size = mm2px(Vec(54.0, 40.5));
		readout->module = module;
		addChild(readout);

		TuneScope* scope = createWidget<TuneScope>(mm2px(Vec(3.48, 56.5)));
		scope->box.size = mm2px(Vec(54.0, 40.5));
		scope->module = module;
		addChild(scope);

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.745, 109.0)), module, Tune::AUDIO_INPUT));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(23.235, 109.0)), module, Tune::WIDTH_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(37.725, 109.0)), module, Tune::FREQ_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(52.215, 109.0)), module, Tune::AUDIO_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Tune* module = getModule<Tune>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Analysis speed",
			{"Precise: fastest lock, most CPU", "Balanced", "Light: slower lock, less CPU", "Minimal: slowest lock, least CPU"},
			[=]() {
				for (int i = 0; i < 4; i++)
					if (module->analysisSkip == Tune::kSkipChoices[i])
						return i;
				return 1;
			},
			[=](int i) {
				withMenuUndo(module, "set analysis speed", [=]() { module->setAnalysisSkip(Tune::kSkipChoices[i]); });
			}));
	}
};

Model* modelTune = createModel<Tune, TuneWidget>("Tune");
