#include "plugin.hpp"
#include "ShapeGenerator.hpp"
#include "shapes/Combine.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/Timebase.hpp"
#include "shapes/WeaverPresets.hpp"

#include <atomic>
#include <cctype>

// Weaver: a shape modulator. Three operators (a wave each, or an outside CV)
// are woven together by one of eleven algorithms and faded by a per-cycle
// envelope. Fifteen factory presets; the first is the default state.

static constexpr int kTracePoints = 256;
static constexpr int kWavePoints = 64;
// Samples between display updates.
// Operator traces on the main display are clamped to this.
static constexpr float kOperatorLimit = 1.2f;

static const char* const kWaveNames[shapes::kNumWaves] = {
	"Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Envelope", "Stepped random", "Smooth random", "Stepped random loop", "Smooth random loop", "Input",
};

// The WAVE bezel's colour for each wave.
static constexpr float kWaveColors[shapes::kNumWaves][3] = {
	{0.f, 1.f, 0.25f},
	{1.f, 0.9f, 0.f},
	{0.1f, 0.4f, 1.f},
	{0.6f, 0.2f, 1.f},
	{1.f, 0.1f, 0.1f},
	{1.f, 0.5f, 0.f},
	{1.f, 1.f, 1.f},
	{0.6f, 0.85f, 1.f},
	{1.f, 0.55f, 0.6f},
	{0.f, 1.f, 0.85f},
	{1.f, 0.f, 1.f},
};

// What the displays show, written by the audio thread.
struct WeaverFrame {
	float result[kTracePoints] = {};  // -1..1 within the output range
	float scaled[3][kTracePoints] = {};  // each operator times its LEVEL
	float wave[3][kWavePoints] = {};  // one cycle of each operator's wave
	int waves[3] = {};
	int ratios[3] = {};
	bool shown[3] = {};  // LEVEL != 0
	bool traceOn[3] = {true, true, true};  // the TRACE buttons
	int algorithm = 0;  // the one running
	int preset = 0;  // -1 when the settings match none
	float bpm = 120.f;
};

static const shapes::CombineControls& defaults() {
	return shapes::kWeaverPresets[0].controls;
}

// Fills the frame from the formulas: right for every operator that isn't Input.
static void renderFormulas(WeaverFrame& f, const shapes::CombineEngine& e, const shapes::CombineControls& c,
                           double cycle, shapes::Treat mode) {
	double peak = e.peak();
	for (int k = 0; k < kTracePoints; k++) {
		// Open traces: the last point ends this cycle rather than starting the next
		double t = cycle + std::min((double) k / (kTracePoints - 1), 1.0 - 1e-9);
		f.result[k] = (float) shapes::sanitize(shapes::treat(e.resultAt(c, t, false), mode, peak), 0.0);
		for (int i = 0; i < 3; i++)
			f.scaled[i][k] = (float) (e.operatorAt(c, i, t, false) * c.op[i].level);
	}
	for (int i = 0; i < 3; i++) {
		for (int k = 0; k < kWavePoints; k++) {
			// Just short of 1 so the last point is the end of this cycle
			double v = shapes::evalOperator(c.op[i], (double) k / (kWavePoints - 1) * 0.99999, i);
			// The envelope is 0..1; drawn, like the others, across the full height
			if (c.op[i].wave == shapes::Envelope)
				v = 2.0 * v - 1.0;
			f.wave[i][k] = (float) shapes::sanitize(v, 0.0);
		}
		f.waves[i] = c.op[i].wave;
		f.ratios[i] = c.op[i].ratio;
		f.shown[i] = c.op[i].level != 0.0;
	}
	f.algorithm = c.algorithm;
}

struct Weaver : Module {
	// Each operator's params, inputs and lights, in this order, from A_*
	static constexpr int kOpParams = 8;
	static constexpr int kOpInputs = 4;
	enum ParamId {
		A_RATIO_PARAM,
		A_WAVE_PARAM,
		A_PARAM_PARAM,
		A_PHASE_PARAM,
		A_LEVEL_PARAM,
		A_PARAM_CV_PARAM,
		A_PHASE_CV_PARAM,
		A_LEVEL_CV_PARAM,
		B_RATIO_PARAM,
		B_WAVE_PARAM,
		B_PARAM_PARAM,
		B_PHASE_PARAM,
		B_LEVEL_PARAM,
		B_PARAM_CV_PARAM,
		B_PHASE_CV_PARAM,
		B_LEVEL_CV_PARAM,
		C_RATIO_PARAM,
		C_WAVE_PARAM,
		C_PARAM_PARAM,
		C_PHASE_PARAM,
		C_LEVEL_PARAM,
		C_PARAM_CV_PARAM,
		C_PHASE_CV_PARAM,
		C_LEVEL_CV_PARAM,
		ALGORITHM_PARAM,
		WINDOW_PARAM,
		PRESET_PREV_PARAM,
		PRESET_NEXT_PARAM,
		BPM_PARAM,
		RESET_PARAM,
		A_TRACE_PARAM,
		B_TRACE_PARAM,
		C_TRACE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		A_RATIO_INPUT,
		A_PARAM_INPUT,
		A_PHASE_INPUT,
		A_LEVEL_INPUT,
		B_RATIO_INPUT,
		B_PARAM_INPUT,
		B_PHASE_INPUT,
		B_LEVEL_INPUT,
		C_RATIO_INPUT,
		C_PARAM_INPUT,
		C_PHASE_INPUT,
		C_LEVEL_INPUT,
		ALGORITHM_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MAIN_OUTPUT,
		INV_OUTPUT,
		EOC_OUTPUT,
		A_OUTPUT,
		B_OUTPUT,
		C_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(A_WAVE_LIGHT, 3),
		ENUMS(B_WAVE_LIGHT, 3),
		ENUMS(C_WAVE_LIGHT, 3),
		ENUMS(TRACE_LIGHT, 3),
		LIGHTS_LEN
	};
	static_assert(B_RATIO_PARAM == A_RATIO_PARAM + kOpParams && C_RATIO_PARAM == B_RATIO_PARAM + kOpParams, "");
	static_assert(B_RATIO_INPUT == A_RATIO_INPUT + kOpInputs && C_RATIO_INPUT == B_RATIO_INPUT + kOpInputs, "");

	// Context menu settings
	shapeui::Settings settings;
	// The waves and the algorithm the buttons and their menus have chosen
	int waves[3] = {};
	int algorithm = 0;
	// Each operator's loop pattern: new when a loop wave is chosen, saved
	uint32_t loopSeeds[3] = {};
	// A reset also makes new loop patterns
	bool newLoopOnReset = false;
	// A new algorithm waits for the end of the cycle
	bool algorithmAtCycleEnd = false;
	int heldAlgorithm = 0;
	// Take the algorithm now even when waiting (preset, load). Set from the
	// UI thread after the choices it publishes.
	std::atomic<bool> algorithmNow{true};

	shapes::Timebase timebase;
	shapes::CombineEngine engine;
	shapes::Slew slew[4];  // OUT, then A, B, C
	shapes::Trigger eoc;
	dsp::BooleanTrigger waveTriggers[3], algorithmTrigger;
#ifdef METAMODULE
	// MetaModule has no widget events, so the PRESET buttons are read here
	dsp::BooleanTrigger presetPrevTrigger, presetNextTrigger;
#endif
	float sampleRate = 48000.f;
	float appliedSlewMs = -1.f;
	int sincePublish = shapeui::kPublishInterval;

	// The last cycle as it played, one slot per point of the open trace, for the
	// traces that have no formula: Input operators and a result using them
	float historyResult[kTracePoints] = {};
	float historyScaled[3][kTracePoints] = {};
	float historyRaw[3][kTracePoints] = {};
	int historyWrite = 0;

	WeaverFrame frames[2];
	std::atomic<int> frontFrame{0};
	std::atomic<float> displayPos{0.f};

	static int param(int op, int first) {
		return first + op * kOpParams;
	}
	static int input(int op, int first) {
		return first + op * kOpInputs;
	}

	Weaver();

	void setRate(float rate) {
		sampleRate = rate;
		timebase.setSampleRate(rate);
		engine.setSampleRate(rate);
		appliedSlewMs = -1.f;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		setRate(e.sampleRate);
	}

	// Choosing a loop wave makes a new pattern. Called from the UI thread
	// (menus) and from process() (the buttons).
	void selectWave(int i, int w) {
		if (w != waves[i] && shapes::isLoop((shapes::Wave) w))
			loopSeeds[i] = random::u32();
		waves[i] = w;
	}

	void newLoops() {
		for (int i = 0; i < 3; i++)
			if (shapes::isLoop((shapes::Wave) waves[i]))
				loopSeeds[i] = random::u32();
	}

	void resetChoices() {
		for (int i = 0; i < 3; i++)
			waves[i] = defaults().op[i].wave;
		algorithm = defaults().algorithm;
		algorithmNow.store(true, std::memory_order_release);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		resetChoices();
		settings.reset();
		algorithmAtCycleEnd = false;
		newLoopOnReset = false;
	}

	// Picks new waves and loop patterns too; never Input, which needs a cable to be heard
	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		// Input is the last wave
		for (int i = 0; i < 3; i++)
			waves[i] = (int) (random::u32() % shapes::Input);
		newLoops();
	}

	float cv(int in) {
		return (float) shapes::sanitize(inputs[in].getVoltage(), 0.0);
	}

	// Knob plus attenuverted CV (10 V spans the knob's range), unclamped.
	double modulated(int knob, int in, int trim, double span) {
		double v = params[knob].getValue() + cv(in) * 0.2 * params[trim].getValue() * span;
		return shapes::sanitize(v, params[knob].getValue());
	}

	// The settings as the panel shows them, CV left out, for preset matching.
	shapes::CombineControls panelControls() {
		shapes::CombineControls c;
		for (int i = 0; i < 3; i++) {
			shapes::OperatorControls& o = c.op[i];
			o.wave = (shapes::Wave) waves[i];
			o.ratio = (int) std::round(params[param(i, A_RATIO_PARAM)].getValue());
			o.param = params[param(i, A_PARAM_PARAM)].getValue();
			o.phase = params[param(i, A_PHASE_PARAM)].getValue();
			o.level = params[param(i, A_LEVEL_PARAM)].getValue();
		}
		c.algorithm = (shapes::Algorithm) algorithm;
		c.window = params[WINDOW_PARAM].getValue();
		return c;
	}

	void loadPreset(int n) {
		const shapes::CombineControls& p = shapes::kWeaverPresets[n].controls;
		for (int i = 0; i < 3; i++) {
			const shapes::OperatorControls& o = p.op[i];
			waves[i] = o.wave;
			params[param(i, A_RATIO_PARAM)].setValue((float) o.ratio);
			params[param(i, A_PARAM_PARAM)].setValue((float) o.param);
			params[param(i, A_PHASE_PARAM)].setValue((float) o.phase);
			params[param(i, A_LEVEL_PARAM)].setValue((float) o.level);
		}
		algorithm = p.algorithm;
		params[WINDOW_PARAM].setValue((float) p.window);
		algorithmNow.store(true, std::memory_order_release);
	}

	// Steps through the presets; from settings that match none, Next starts
	// at the first and Previous at the last. Called from the UI thread in VCV
	// Rack and from process() on MetaModule.
	void stepPreset(int delta) {
		int n = shapes::presetMatches(panelControls());
		if (n < 0)
			n = delta > 0 ? 0 : shapes::kNumWeaverPresets - 1;
		else
			n = (n + delta + shapes::kNumWeaverPresets) % shapes::kNumWeaverPresets;
		loadPreset(n);
	}

	shapes::CombineControls controls() {
		shapes::CombineControls c;
		for (int i = 0; i < 3; i++) {
			shapes::OperatorControls& o = c.op[i];
			o.wave = (shapes::Wave) waves[i];
			o.loopSeed = loopSeeds[i];
			// 1 V per ratio step; huge voltages are clamped first so the
			// rounding can't overflow
			float ratioCv = clamp(cv(input(i, A_RATIO_INPUT)), -100.f, 100.f);
			int ratio = (int) std::round(params[param(i, A_RATIO_PARAM)].getValue()) + (int) std::round(ratioCv);
			o.ratio = clamp(ratio, 0, shapes::kNumRatios - 1);
			o.phase = shapes::frac(modulated(param(i, A_PHASE_PARAM), input(i, A_PHASE_INPUT), param(i, A_PHASE_CV_PARAM), 1.0));
			o.level = std::clamp(modulated(param(i, A_LEVEL_PARAM), input(i, A_LEVEL_INPUT), param(i, A_LEVEL_CV_PARAM), 2.0), -1.0, 1.0);
			if (o.wave == shapes::Input) {
				// The PARAM jack is the signal, its trimpot the gain, and the
				// knob the slew
				o.param = clamp(params[param(i, A_PARAM_PARAM)].getValue(), 0.f, 1.f);
				o.inputVolts = cv(input(i, A_PARAM_INPUT));
				o.inputGain = 1.0 + params[param(i, A_PARAM_CV_PARAM)].getValue();
			}
			else {
				o.param = std::clamp(modulated(param(i, A_PARAM_PARAM), input(i, A_PARAM_INPUT), param(i, A_PARAM_CV_PARAM), 1.0), 0.0, 1.0);
			}
		}
		c.window = clamp(params[WINDOW_PARAM].getValue(), 0.f, 1.f);
		return c;
	}

	int targetAlgorithm() {
		// 10 V of CV spans all the algorithms
		double algorithmCv = clamp(cv(ALGORITHM_INPUT), -100.f, 100.f);
		int a = algorithm + (int) std::round(algorithmCv / 10.0 * (int) shapes::kNumAlgorithms);
		return ((a % shapes::kNumAlgorithms) + shapes::kNumAlgorithms) % shapes::kNumAlgorithms;
	}

	float output(int index, double x) {
		double volts = slew[index].process(shapes::sanitize(shapes::toVolts(x, settings.range), 0.0));
		return (float) shapes::sanitize(volts, 0.0);
	}

	void process(const ProcessArgs& args) override {
#ifdef METAMODULE
		if (presetPrevTrigger.process(params[PRESET_PREV_PARAM].getValue() > 0.5f))
			stepPreset(-1);
		if (presetNextTrigger.process(params[PRESET_NEXT_PARAM].getValue() > 0.5f))
			stepPreset(1);
#endif
		for (int i = 0; i < 3; i++)
			if (waveTriggers[i].process(params[param(i, A_WAVE_PARAM)].getValue() > 0.5f))
				selectWave(i, (waves[i] + 1) % shapes::kNumWaves);
		if (algorithmTrigger.process(params[ALGORITHM_PARAM].getValue() > 0.5f))
			algorithm = (algorithm + 1) % shapes::kNumAlgorithms;
		if (settings.slewMs != appliedSlewMs) {
			for (shapes::Slew& s : slew)
				s.set(settings.slewMs, sampleRate);
			appliedSlewMs = settings.slewMs;
		}

		shapes::TimebaseInput tin;
		tin.bpm = shapeui::bpmOf(params[BPM_PARAM].getValue());
		tin.clockConnected = inputs[CLOCK_INPUT].isConnected();
		tin.clockVoltage = cv(CLOCK_INPUT);
		tin.resetVoltage = cv(RESET_INPUT);
		tin.resetButton = params[RESET_PARAM].getValue() > 0.5f;
		tin.pulsesPerCycle = settings.pulsesPerCycle;
		shapes::Timebase::Step step = timebase.process(tin);
		if (step.reset) {
			engine.reset();
			if (newLoopOnReset)
				newLoops();
		}
		// The Input peak rolls over here, before this sample is processed
		if (step.wrapped)
			engine.cycleWrapped();
		// Reset starts a cycle too, so a waiting algorithm lands there
		bool cycleStart = step.wrapped || step.reset;

		shapes::CombineControls c = controls();
		if (algorithmNow.exchange(false, std::memory_order_acquire) || !algorithmAtCycleEnd || cycleStart)
			heldAlgorithm = targetAlgorithm();
		c.algorithm = (shapes::Algorithm) heldAlgorithm;

		double t = (double) step.cycle + step.pos;
		shapes::CombineFrame f = engine.process(c, t, false);

		double x = shapes::sanitize(shapes::treat(f.result, settings.treatMode, engine.peak()), 0.0);
		float out = output(0, x);
		outputs[MAIN_OUTPUT].setVoltage(out);
		outputs[INV_OUTPUT].setVoltage((float) shapes::sanitize(shapes::invertVolts(out, settings.range), 0.0));
		if (step.wrapped)
			eoc.fire();
		outputs[EOC_OUTPUT].setVoltage(eoc.process(args.sampleTime));
		// Each operator's own wave at full level. A single wave has no cycle
		// peak to fit (an Input can be any size), so Fit clips here.
		shapes::Treat opMode = settings.treatMode == shapes::Treat::Fit ? shapes::Treat::Clip : settings.treatMode;
		for (int i = 0; i < 3; i++) {
			double v = f.raw[i];
			// An Envelope is 0..1: in 0-10 V it spans the full range like the others
			if (settings.range == shapes::Range::Unipolar10 && c.op[i].wave == shapes::Envelope)
				v = 2.0 * v - 1.0;
			outputs[A_OUTPUT + i].setVoltage(output(1 + i, shapes::treat(v, opMode, 1.0)));
		}

		historyWrite = clamp((int) std::lround(step.pos * (kTracePoints - 1)), 0, kTracePoints - 1);
		historyResult[historyWrite] = (float) x;
		for (int i = 0; i < 3; i++) {
			historyScaled[i][historyWrite] = (float) f.scaled[i];
			historyRaw[i][historyWrite] = (float) f.raw[i];
		}

		for (int i = 0; i < 3; i++) {
			const float* rgb = kWaveColors[waves[i]];
			for (int k = 0; k < 3; k++)
				lights[A_WAVE_LIGHT + 3 * i + k].setBrightness(rgb[k]);
		}
		for (int i = 0; i < 3; i++)
			lights[TRACE_LIGHT + i].setBrightness(params[A_TRACE_PARAM + i].getValue());

		displayPos.store((float) step.pos, std::memory_order_relaxed);
		if (++sincePublish >= shapeui::kPublishInterval) {
			sincePublish = 0;
			publish(c, (double) step.cycle);
		}
	}

	void publish(const shapes::CombineControls& c, double cycle) {
		int back = 1 - frontFrame.load(std::memory_order_relaxed);
		WeaverFrame& f = frames[back];
		renderFormulas(f, engine, c, cycle, settings.treatMode);
		for (int i = 0; i < 3; i++)
			f.traceOn[i] = params[A_TRACE_PARAM + i].getValue() > 0.5f;
		bool anyInput = false;
		for (int i = 0; i < 3; i++) {
			if (c.op[i].wave != shapes::Input)
				continue;
			anyInput = true;
			std::copy(historyScaled[i], historyScaled[i] + kTracePoints, f.scaled[i]);
			// Oldest first, so the newest input is at the right and the trace scrolls
			for (int k = 0; k < kWavePoints; k++)
				f.wave[i][k] = historyRaw[i][(historyWrite + 1 + k * (kTracePoints / kWavePoints)) % kTracePoints];
		}
		if (anyInput)
			std::copy(historyResult, historyResult + kTracePoints, f.result);
		f.preset = shapes::presetMatches(panelControls());
		f.bpm = (float) timebase.tempoBpm();
		frontFrame.store(back, std::memory_order_release);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		settings.toJson(rootJ);
		json_t* wavesJ = json_array();
		for (int i = 0; i < 3; i++)
			json_array_append_new(wavesJ, json_integer(waves[i]));
		json_object_set_new(rootJ, "waves", wavesJ);
		// 2: the loop waves sit before Input
		json_object_set_new(rootJ, "wavesVersion", json_integer(2));
		json_t* seedsJ = json_array();
		for (int i = 0; i < 3; i++)
			json_array_append_new(seedsJ, json_integer(loopSeeds[i]));
		json_object_set_new(rootJ, "loopSeeds", seedsJ);
		json_object_set_new(rootJ, "newLoopOnReset", json_boolean(newLoopOnReset));
		json_object_set_new(rootJ, "algorithm", json_integer(algorithm));
		json_object_set_new(rootJ, "algorithmAtCycleEnd", json_boolean(algorithmAtCycleEnd));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		settings.fromJson(rootJ);
		// Missing or unknown values fall back to the first preset
		resetChoices();
		json_t* wavesJ = json_object_get(rootJ, "waves");
		bool beforeLoops = !json_object_get(rootJ, "wavesVersion");
		json_t* seedsJ = json_object_get(rootJ, "loopSeeds");
		for (int i = 0; i < 3; i++) {
			json_t* j = json_array_get(wavesJ, i);
			if (j && json_is_integer(j) && json_integer_value(j) >= 0 && json_integer_value(j) < shapes::kNumWaves) {
				waves[i] = (int) json_integer_value(j);
				// Input was 8 before the loop waves were added
				if (beforeLoops && waves[i] == 8)
					waves[i] = shapes::Input;
			}
			j = json_array_get(seedsJ, i);
			if (j && json_is_integer(j))
				loopSeeds[i] = (uint32_t) json_integer_value(j);
		}
		json_t* j = json_object_get(rootJ, "algorithm");
		if (j && json_is_integer(j) && json_integer_value(j) >= 0 && json_integer_value(j) < shapes::kNumAlgorithms)
			algorithm = (int) json_integer_value(j);
		j = json_object_get(rootJ, "algorithmAtCycleEnd");
		algorithmAtCycleEnd = j && json_is_true(j);
		j = json_object_get(rootJ, "newLoopOnReset");
		newLoopOnReset = j && json_is_true(j);
	}
};

// Tooltips name the chosen wave and algorithm.
struct WaveQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		Weaver* m = dynamic_cast<Weaver*>(module);
		return m ? kWaveNames[m->waves[(paramId - Weaver::A_WAVE_PARAM) / Weaver::kOpParams]] : "";
	}
};

struct AlgorithmQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		Weaver* m = dynamic_cast<Weaver*>(module);
		return m ? shapes::algorithmName((shapes::Algorithm) m->algorithm) : "";
	}
};

// PARAM is named for what it does to the chosen wave ("A attack").
struct OperatorParamQuantity : ParamQuantity {
	std::string getLabel() override {
		Weaver* m = dynamic_cast<Weaver*>(module);
		int op = (paramId - Weaver::A_PARAM_PARAM) / Weaver::kOpParams;
		std::string letter(1, (char) ('A' + op));
		if (!m)
			return letter + " param";
		return letter + " " + string::lowercase(shapes::paramName((shapes::Wave) m->waves[op]));
	}
};

Weaver::Weaver() {
	config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	std::vector<std::string> ratioNames;
	for (int r = 0; r < shapes::kNumRatios; r++)
		ratioNames.push_back(shapes::ratioLabel(r));
	for (int i = 0; i < 3; i++) {
		const shapes::OperatorControls& d = defaults().op[i];
		std::string op(1, (char) ('A' + i));
		configSwitch(param(i, A_RATIO_PARAM), 0.f, shapes::kNumRatios - 1, (float) d.ratio, op + " ratio", ratioNames);
		configButton<WaveQuantity>(param(i, A_WAVE_PARAM), op + " wave");
		configParam<OperatorParamQuantity>(param(i, A_PARAM_PARAM), 0.f, 1.f, (float) d.param, op + " param", "%", 0.f, 100.f);
		configParam(param(i, A_PHASE_PARAM), 0.f, 1.f, (float) d.phase, op + " phase", " deg", 0.f, 360.f);
		configParam(param(i, A_LEVEL_PARAM), -1.f, 1.f, (float) d.level, op + " level", "%", 0.f, 100.f);
		configParam(param(i, A_PARAM_CV_PARAM), -1.f, 1.f, 0.f, op + " param CV amount (Input: gain)");
		configParam(param(i, A_PHASE_CV_PARAM), -1.f, 1.f, 0.f, op + " phase CV amount");
		configParam(param(i, A_LEVEL_CV_PARAM), -1.f, 1.f, 0.f, op + " level CV amount");
		configInput(input(i, A_RATIO_INPUT), op + " ratio CV");
		configInput(input(i, A_PARAM_INPUT), op + " param CV (Input: the signal)");
		configInput(input(i, A_PHASE_INPUT), op + " phase CV");
		configInput(input(i, A_LEVEL_INPUT), op + " level CV");
		configOutput(A_OUTPUT + i, "Operator " + op);
		configLight(A_WAVE_LIGHT + 3 * i, op + " wave");
	}
	configButton<AlgorithmQuantity>(ALGORITHM_PARAM, "Algorithm");
	configParam(WINDOW_PARAM, 0.f, 1.f, (float) defaults().window, "Envelope", "%", 0.f, 100.f);
	configButton(PRESET_PREV_PARAM, "Previous preset");
	configButton(PRESET_NEXT_PARAM, "Next preset");
	shapeui::configBpm(this, BPM_PARAM);
	configButton(RESET_PARAM, "Reset");
	for (int i = 0; i < 3; i++) {
		std::string op(1, (char) ('A' + i));
		// Display settings, not part of the sound
		configSwitch(A_TRACE_PARAM + i, 0.f, 1.f, 1.f, op + " trace on the display", {"Hidden", "Shown"})->randomizeEnabled = false;
		configLight(TRACE_LIGHT + i, op + " trace");
	}
	configInput(ALGORITHM_INPUT, "Algorithm CV");
	configInput(CLOCK_INPUT, "Clock");
	configInput(RESET_INPUT, "Reset");
	configOutput(MAIN_OUTPUT, "Main");
	configOutput(INV_OUTPUT, "Inverted");
	configOutput(EOC_OUTPUT, "End of cycle");
	resetChoices();
	for (int i = 0; i < 3; i++)
		loopSeeds[i] = random::u32();
	setRate(APP->engine->getSampleRate());
}

// The module's latest frame, or, in the module browser, the first preset
// at default settings, built once.
static const WeaverFrame& frameOf(Weaver* module) {
	if (module)
		return module->frames[module->frontFrame.load(std::memory_order_acquire)];
	static WeaverFrame preview;
	static bool built = false;
	if (!built) {
		shapes::CombineEngine e;
		e.process(defaults(), 0.0, false);
		renderFormulas(preview, e, defaults(), 0.0, shapes::Treat::Fit);
		preview.preset = 0;
		built = true;
	}
	return preview;
}

static void upperCase(char* out, size_t size, const char* in) {
	size_t n = 0;
	for (; in[n] && n < size - 1; n++)
		out[n] = (char) std::toupper((unsigned char) in[n]);
	out[n] = 0;
}

// One cycle of the result (after range and treatment, before slew) over the
// operators as dashed lines, with a playhead, the A B C legend, the preset
// the settings match and the tempo.
struct WeaverMainDisplay : shapeui::Screen {
	Weaver* module = nullptr;

	WeaverMainDisplay() {
		// The plates' corner radius
		radiusMm = 2.f;
	}

	void drawScreen(NVGcontext* vg) override {
		// Copied so a publish mid-draw can't tear the traces
		WeaverFrame f = frameOf(module);
		float pos = module ? module->displayPos.load(std::memory_order_relaxed) : 0.62f;
		if (!std::isfinite(pos))
			pos = 0.f;

		const float pad = mm2px(2.f);
		const float gx = pad, gy = mm2px(7.f), gw = box.size.x - 2 * pad, gh = box.size.y - mm2px(9.f);
		shapeui::drawGrid(vg, gx, gy, gw, gh);

		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (font) {
			const float baseline = mm2px(4.6f);
			nvgFontFaceId(vg, font->handle);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
			shapeui::fontSize(vg, mm2px(2.4f));
			for (int i = 0; i < 3; i++) {
				char letter[2] = {(char) ('A' + i), 0};
				// Dimmed while its trace is hidden
				nvgFillColor(vg, shapeui::screenColor(f.traceOn[i] ? shapeui::operatorColor(i) : shapeui::dividerColor()));
				nvgText(vg, gx + mm2px(3.4f) * i, baseline, letter, nullptr);
			}
			if (f.preset >= 0 && f.preset < shapes::kNumWeaverPresets) {
				char name[40], title[48];
				upperCase(name, sizeof(name), shapes::kWeaverPresets[f.preset].name);
				snprintf(title, sizeof(title), "%02d %s", f.preset + 1, name);
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
				shapeui::fontSize(vg, mm2px(2.6f));
				nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
				nvgText(vg, gx + gw / 2, baseline, title, nullptr);
			}
			char right[24];
			snprintf(right, sizeof(right), "%.2f BPM", f.bpm);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			shapeui::fontSize(vg, mm2px(2.2f));
			nvgFillColor(vg, shapeui::screenColor(shapeui::mutedColor()));
			nvgText(vg, gx + gw, baseline, right, nullptr);
		}

		shapeui::TraceStyle dashed;
		dashed.widthMm = 0.22f;
		dashed.limit = kOperatorLimit;
		dashed.dashMm = 0.9f;
		dashed.gapMm = 0.6f;
		dashed.closed = false;
		for (int i = 0; i < 3; i++) {
			if (!f.shown[i] || !f.traceOn[i])
				continue;
			dashed.color = shapeui::operatorColor(i);
			shapeui::drawTrace(vg, f.scaled[i], kTracePoints, gx, gy, gw, gh, dashed);
		}
		shapeui::TraceStyle solid;
		solid.color = shapeui::signalColor();
		// A jump at the loop point shows at the end of the cycle
		solid.closed = false;
		solid.leadOut = true;
		shapeui::drawTrace(vg, f.result, kTracePoints, gx, gy, gw, gh, solid);
		shapeui::drawPlayhead(vg, gx, gy, gw, gh, pos, shapeui::traceAt(f.result, kTracePoints, pos, false));
	}
};

// The running algorithm's name, as large as fits, over its formula.
struct AlgorithmDisplay : shapeui::Screen {
	Weaver* module = nullptr;

	void drawScreen(NVGcontext* vg) override {
		// Copied, as the main display does, so a publish can't change it mid-draw
		int a = clamp(frameOf(module).algorithm, 0, shapes::kNumAlgorithms - 1);
		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (!font)
			return;
		const char* name = shapes::algorithmName((shapes::Algorithm) a);
		const char* formula = shapes::algorithmFormula((shapes::Algorithm) a);
		// Share Tech Mono: advance 0.54 em, cap height 0.70 em (sizes in mm)
		const float advance = 0.54f, cap = 0.70f, gap = 1.6f;
		float w = box.size.x / mm2px(1.f), h = box.size.y / mm2px(1.f);
		auto fit = [&](float size, const char* text) {
			size_t n = std::max<size_t>(strlen(text), 1);
			return std::min(size, (w - 2.f) / (n * advance));
		};
		float ns = fit(6.0f, name), fs = fit(2.4f, formula);
		float nameBaseline = (h - (cap * ns + gap + cap * fs)) / 2 + cap * ns;
		float formulaBaseline = nameBaseline + gap + cap * fs;

		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		shapeui::fontSize(vg, mm2px(ns));
		nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
		nvgText(vg, box.size.x / 2, mm2px(nameBaseline), name, nullptr);
		shapeui::fontSize(vg, mm2px(fs));
		nvgFillColor(vg, shapeui::screenColor(shapeui::mutedColor()));
		nvgText(vg, box.size.x / 2, mm2px(formulaBaseline), formula, nullptr);
	}
};

// One operator's wave: what PARAM does for it in a top band, one cycle of the
// wave (an Input: its last cycle, scrolling), then the wave's name in a bottom
// band.
struct WaveDisplay : shapeui::Screen {
	Weaver* module = nullptr;
	int op = 0;
	// mm; the top band's text is level with the PARAM knob
	static constexpr float kLabelBand = 4.9f;
	static constexpr float kLabelBaseline = 3.9f;
	static constexpr float kNameBand = 3.4f;

	WaveDisplay() {
		radiusMm = 0.4f;
	}

	void divider(NVGcontext* vg, float y) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0, mm2px(y));
		nvgLineTo(vg, box.size.x, mm2px(y));
		nvgStrokeWidth(vg, mm2px(0.2f));
		nvgStrokeColor(vg, shapeui::screenColor(shapeui::dividerColor()));
		nvgStroke(vg);
	}

	void drawScreen(NVGcontext* vg) override {
		// Copied, as the main display does, so a publish can't tear the trace
		const WeaverFrame& shared = frameOf(module);
		float values[kWavePoints];
		std::copy(shared.wave[op], shared.wave[op] + kWavePoints, values);
		int wave = clamp(shared.waves[op], 0, shapes::kNumWaves - 1);

		float w = box.size.x / mm2px(1.f), h = box.size.y / mm2px(1.f);
		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (font) {
			nvgFontFaceId(vg, font->handle);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			shapeui::fontSize(vg, mm2px(2.0f));
			nvgFillColor(vg, shapeui::screenColor(shapeui::mutedColor()));
			nvgText(vg, mm2px(w - 1.f), mm2px(kLabelBaseline), shapes::paramName((shapes::Wave) wave), nullptr);

			// As large as fits, up to the top band's size (Share Tech Mono:
			// advance 0.54 em, cap height 0.70 em)
			char name[24];
			// The loop waves' full names would shrink too small to read
			const char* label = wave == shapes::SteppedLoop ? "Stepped loop"
			                    : wave == shapes::SmoothLoop ? "Smooth loop"
			                                                 : kWaveNames[wave];
			upperCase(name, sizeof(name), label);
			float size = std::min(2.0f, (w - 1.4f) / (std::max<size_t>(strlen(name), 1) * 0.54f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			shapeui::fontSize(vg, mm2px(size));
			nvgFillColor(vg, shapeui::screenColor(shapeui::operatorColor(op)));
			nvgText(vg, box.size.x / 2, mm2px(h - kNameBand / 2 + 0.35f * size), name, nullptr);
		}
		divider(vg, kLabelBand);
		divider(vg, h - kNameBand);

		float ty = kLabelBand + 0.7f, th = h - kNameBand - 0.7f - ty;
		nvgBeginPath(vg);
		nvgMoveTo(vg, mm2px(1.f), mm2px(ty + th / 2));
		nvgLineTo(vg, mm2px(w - 1.f), mm2px(ty + th / 2));
		nvgStrokeWidth(vg, mm2px(0.12f));
		nvgStrokeColor(vg, shapeui::screenColor(shapeui::gridColor()));
		nvgStroke(vg);

		shapeui::TraceStyle style;
		style.color = shapeui::operatorColor(op);
		style.widthMm = 0.3f;
		style.closed = false;
		shapeui::drawTrace(vg, values, kWavePoints, mm2px(1.2f), mm2px(ty), mm2px(w - 2.4f), mm2px(th), style);
	}
};

// The operator's ratio (x1, 1/4), or IN for an Input.
struct RatioDisplay : shapeui::Screen {
	Weaver* module = nullptr;
	int op = 0;

	RatioDisplay() {
		radiusMm = 0.4f;
	}

	void drawScreen(NVGcontext* vg) override {
		// Copied, as the main display does, so the two fields agree
		const WeaverFrame& shared = frameOf(module);
		int wave = shared.waves[op];
		int ratio = clamp(shared.ratios[op], 0, shapes::kNumRatios - 1);
		const char* text = wave == shapes::Input ? "IN" : shapes::ratioLabel(ratio);
		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (!font)
			return;
		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		shapeui::fontSize(vg, mm2px(3.2f));
		nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
		nvgText(vg, box.size.x / 2, box.size.y / 2 + mm2px(1.1f), text, nullptr);
	}
};

// The PRESET buttons load on the UI thread so the change can be undone.
struct PresetButton : TL1105 {
	int delta = 1;

	void onDragStart(const DragStartEvent& e) override {
		Weaver* module = getParamQuantity() ? dynamic_cast<Weaver*>(getParamQuantity()->module) : nullptr;
		if (module && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			int d = delta;
			withMenuUndo(module, "load preset", [=]() { module->stepPreset(d); });
		}
		TL1105::onDragStart(e);
	}
};

// Clicking WAVE or ALGORITHM steps to the next choice; right-clicking lists
// them all below the usual parameter menu.
template <typename TBase>
struct ChoiceMenuButton : TBase {
	virtual void appendChoices(Menu* menu, Weaver* module) = 0;

	void appendContextMenu(Menu* menu) override {
		Weaver* module = this->getParamQuantity() ? dynamic_cast<Weaver*>(this->getParamQuantity()->module) : nullptr;
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		appendChoices(menu, module);
	}
};

struct WaveButton : ChoiceMenuButton<VCVLightBezel<RedGreenBlueLight>> {
	int op = 0;

	void appendChoices(Menu* menu, Weaver* module) override {
		menu->addChild(createMenuLabel(std::string("Operator ") + (char) ('A' + op) + " wave"));
		for (int w = 0; w < shapes::kNumWaves; w++) {
			int o = op;
			menu->addChild(createCheckMenuItem(kWaveNames[w], "",
				[=]() { return module->waves[o] == w; },
				[=]() {
					if (module->waves[o] != w)
						withMenuUndo(module, "set wave", [=]() { module->selectWave(o, w); });
				}));
		}
	}
};

struct AlgorithmButton : ChoiceMenuButton<VCVButton> {
	void appendChoices(Menu* menu, Weaver* module) override {
		menu->addChild(createMenuLabel("Algorithm"));
		for (int a = 0; a < shapes::kNumAlgorithms; a++) {
			std::string formula = shapes::algorithmFormula((shapes::Algorithm) a);
			menu->addChild(createCheckMenuItem(shapes::algorithmName((shapes::Algorithm) a), formula,
				[=]() { return module->algorithm == a; },
				[=]() {
					if (module->algorithm != a)
						withMenuUndo(module, "set algorithm", [=]() { module->algorithm = a; });
				}));
		}
	}
};

// A trace button's light, in its operator's colour.
template <int OP>
struct OperatorLight : GrayModuleLightWidget {
	OperatorLight() {
		addBaseColor(shapeui::operatorColor(OP));
	}
};

struct WeaverWidget : ModuleWidget {
	template <typename T>
	T* addScreen(Weaver* module, float x, float y, float w, float h) {
		T* screen = createWidget<T>(mm2px(Vec(x, y)));
		screen->box.size = mm2px(Vec(w, h));
		screen->module = module;
		addChild(screen);
		return screen;
	}

	void addWaveButton(Weaver* module, Vec pos, int paramId, int lightId, int op) {
		WaveButton* b = createLightParamCentered<WaveButton>(pos, module, paramId, lightId);
		b->op = op;
		addParam(b);
	}

	WeaverWidget(Weaver* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Weaver.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addScreen<WeaverMainDisplay>(module, 1.5, 17.0, 87.74, 45.7);
		addScreen<AlgorithmDisplay>(module, 3.9, 67.1, 26.0, 13.3);
		addScreen<WaveDisplay>(module, 110.35, 28.39, 14.0, 17.0)->op = 0;
		addScreen<WaveDisplay>(module, 110.35, 61.22, 14.0, 17.0)->op = 1;
		addScreen<WaveDisplay>(module, 110.35, 94.05, 14.0, 17.0)->op = 2;
		addScreen<RatioDisplay>(module, 94.25, 26.8, 11.0, 6.0)->op = 0;
		addScreen<RatioDisplay>(module, 94.25, 59.63, 11.0, 6.0)->op = 1;
		addScreen<RatioDisplay>(module, 94.25, 92.46, 11.0, 6.0)->op = 2;

		// Operator A
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(99.75, 39.9)), module, Weaver::A_RATIO_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(99.75, 51.03)), module, Weaver::A_RATIO_INPUT));
		addWaveButton(module, mm2px(Vec(117.35, 51.03)), Weaver::A_WAVE_PARAM, Weaver::A_WAVE_LIGHT, 0);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(134.88, 31.59)), module, Weaver::A_PARAM_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(134.88, 41.7)), module, Weaver::A_PARAM_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(134.88, 51.03)), module, Weaver::A_PARAM_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(150.78, 31.59)), module, Weaver::A_PHASE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(150.78, 41.7)), module, Weaver::A_PHASE_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(150.78, 51.03)), module, Weaver::A_PHASE_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(167.79, 31.59)), module, Weaver::A_LEVEL_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(167.79, 41.7)), module, Weaver::A_LEVEL_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(167.79, 51.03)), module, Weaver::A_LEVEL_INPUT));

		// Operator B
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(99.75, 72.73)), module, Weaver::B_RATIO_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(99.75, 83.86)), module, Weaver::B_RATIO_INPUT));
		addWaveButton(module, mm2px(Vec(117.35, 83.86)), Weaver::B_WAVE_PARAM, Weaver::B_WAVE_LIGHT, 1);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(134.88, 64.42)), module, Weaver::B_PARAM_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(134.88, 74.53)), module, Weaver::B_PARAM_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(134.88, 83.86)), module, Weaver::B_PARAM_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(150.78, 64.42)), module, Weaver::B_PHASE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(150.78, 74.53)), module, Weaver::B_PHASE_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(150.78, 83.86)), module, Weaver::B_PHASE_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(167.79, 64.42)), module, Weaver::B_LEVEL_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(167.79, 74.53)), module, Weaver::B_LEVEL_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(167.79, 83.86)), module, Weaver::B_LEVEL_INPUT));

		// Operator C
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(99.75, 105.56)), module, Weaver::C_RATIO_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(99.75, 116.69)), module, Weaver::C_RATIO_INPUT));
		addWaveButton(module, mm2px(Vec(117.35, 116.69)), Weaver::C_WAVE_PARAM, Weaver::C_WAVE_LIGHT, 2);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(134.88, 97.25)), module, Weaver::C_PARAM_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(134.88, 107.36)), module, Weaver::C_PARAM_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(134.88, 116.69)), module, Weaver::C_PARAM_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(150.78, 97.25)), module, Weaver::C_PHASE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(150.78, 107.36)), module, Weaver::C_PHASE_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(150.78, 116.69)), module, Weaver::C_PHASE_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(167.79, 97.25)), module, Weaver::C_LEVEL_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(167.79, 107.36)), module, Weaver::C_LEVEL_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(167.79, 116.69)), module, Weaver::C_LEVEL_INPUT));

		// Algorithm, envelope and presets
		addParam(createParamCentered<AlgorithmButton>(mm2px(Vec(42.13, 71.9)), module, Weaver::ALGORITHM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(51.13, 71.9)), module, Weaver::ALGORITHM_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(71.61, 71.9)), module, Weaver::WINDOW_PARAM));
#ifdef METAMODULE
		// Plain buttons: process() steps the presets, as PresetButton never gets events here
		addParam(createParamCentered<TL1105>(mm2px(Vec(80.6, 88.8)), module, Weaver::PRESET_NEXT_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(74.0, 88.8)), module, Weaver::PRESET_PREV_PARAM));
#else
		PresetButton* next = createParamCentered<PresetButton>(mm2px(Vec(80.6, 88.8)), module, Weaver::PRESET_NEXT_PARAM);
		next->delta = 1;
		addParam(next);
		PresetButton* prev = createParamCentered<PresetButton>(mm2px(Vec(74.0, 88.8)), module, Weaver::PRESET_PREV_PARAM);
		prev->delta = -1;
		addParam(prev);
#endif

		// Clock row
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.3, 92.0)), module, Weaver::BPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.8, 92.0)), module, Weaver::CLOCK_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(31.865, 92.0)), module, Weaver::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.365, 92.0)), module, Weaver::RESET_INPUT));

		// Outputs, and each operator's trace button over its output
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(11.215, 112.1)), module, Weaver::MAIN_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(24.955, 112.1)), module, Weaver::INV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.695, 112.1)), module, Weaver::EOC_OUTPUT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<OperatorLight<0>>>>(mm2px(Vec(57.59, 103.0)), module, Weaver::A_TRACE_PARAM, Weaver::TRACE_LIGHT + 0));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<OperatorLight<1>>>>(mm2px(Vec(69.81, 103.0)), module, Weaver::B_TRACE_PARAM, Weaver::TRACE_LIGHT + 1));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<OperatorLight<2>>>>(mm2px(Vec(82.03, 103.0)), module, Weaver::C_TRACE_PARAM, Weaver::TRACE_LIGHT + 2));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(57.59, 112.1)), module, Weaver::A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(69.81, 112.1)), module, Weaver::B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(82.03, 112.1)), module, Weaver::C_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Weaver* module = getModule<Weaver>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		shapeui::appendSettingsMenu(menu, module, &module->settings);
		menu->addChild(createIndexSubmenuItem("Algorithm changes", {"Immediately", "At end of cycle"},
			[=]() { return module->algorithmAtCycleEnd ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set algorithm change timing", [=]() { module->algorithmAtCycleEnd = i == 1; });
			}));
		menu->addChild(createBoolMenuItem("New random loops on reset", "",
			[=]() { return module->newLoopOnReset; },
			[=](bool on) {
				withMenuUndo(module, "set new random loops on reset", [=]() { module->newLoopOnReset = on; });
			}));
#ifdef METAMODULE
		// No per-param right-click menus here, so list the choices in this one
		std::vector<std::string> waveNames, algorithmNames;
		for (int w = 0; w < shapes::kNumWaves; w++)
			waveNames.push_back(kWaveNames[w]);
		for (int a = 0; a < shapes::kNumAlgorithms; a++)
			algorithmNames.push_back(shapes::algorithmName((shapes::Algorithm) a));
		static const char* const kOpLabels[3] = {"A wave", "B wave", "C wave"};
		for (int o = 0; o < 3; o++)
			menu->addChild(createIndexSubmenuItem(kOpLabels[o], waveNames,
				[=]() { return module->waves[o]; },
				[=](int i) {
					withMenuUndo(module, "set wave", [=]() { module->selectWave(o, i); });
				}));
		menu->addChild(createIndexSubmenuItem("Algorithm", algorithmNames,
			[=]() { return module->algorithm; },
			[=](int i) {
				withMenuUndo(module, "set algorithm", [=]() { module->algorithm = i; });
			}));
#endif
	}
};

Model* modelWeaver = createModel<Weaver, WeaverWidget>("Weaver");
