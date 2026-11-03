#include "plugin.hpp"
#include "FolderAccess.hpp"
#include "ShapeGenerator.hpp"
#include "shapes/Combine.hpp"
#include "shapes/CycleSpeed.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/ScreenMessage.hpp"
#include "shapes/Timebase.hpp"
#include "shapes/TrimMod.hpp"
#include "shapes/WeaverPresetFiles.hpp"
#include "shapes/WeaverPresets.hpp"

// On MetaModule only for the file filter; its file browser is the firmware's
#include <osdialog.h>
#ifdef METAMODULE
#include "filesystem/async_filebrowser.hh"
#include "threads/async_thread.hh"
#endif

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>

// Weaver: a shape modulator. Three operators (a wave each, or an outside CV)
// are woven together by one of eleven algorithms. SPEED runs the cycle at
// 1/4x to 4x the tempo. Presets are files in a folder, the fifteen factory
// presets written there first; the first is the default state.

static constexpr int kTracePoints = 256;
static constexpr int kWavePoints = 64;
// Operator traces on the main display are clamped to this.
static constexpr float kOperatorLimit = 1.2f;
// How long the outputs take to glide to a newly loaded preset's shape.
static constexpr float kRealignGlideS = 0.01f;
static constexpr float kMessageSeconds = 3.f;
// Characters the display's preset line has room for
static constexpr size_t kMessageChars = 26;

#ifdef METAMODULE
static constexpr bool kOnMetaModule = true;
#else
static constexpr bool kOnMetaModule = false;
#endif

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

// What the displays show, drawn up on the UI thread from a snapshot.
struct WeaverFrame {
	float result[kTracePoints] = {};  // -1..1 within the output range
	float scaled[3][kTracePoints] = {};  // each operator times its LEVEL
	float wave[3][kWavePoints] = {};  // one cycle of each operator's wave
	int waves[3] = {};
	int ratios[3] = {};  // running
	int ratioTargets[3] = {};  // chosen (knob + CV), waiting for a cycle start
	bool shown[3] = {};  // LEVEL != 0
	bool traceOn[3] = {true, true, true};  // the TRACE buttons
	int algorithm = 0;  // the one running
	float bpm = 120.f;
	int speed = 0;  // SPEED in octaves, running and chosen
	int speedTarget = 0;
};

// What the audio thread hands the displays: little enough to copy each
// publish, so the traces (hundreds of formula evaluations) are worked out on
// the UI thread, not in process().
struct WeaverSnapshot {
	shapes::CombineControls controls;
	// Its phase offsets and Fit peak
	shapes::CombineEngine engine;
	double cycle = 0.0;
	shapes::Treat treatMode = shapes::Treat::Fit;
	bool traceOn[3] = {true, true, true};
	float bpm = 120.f;
	int speed = 0;
	int speedTarget = 0;
	int ratioTargets[3] = {};
	// The last cycle as it played, one slot per point of the open trace, for
	// the traces that have no formula: Input operators and a result using
	// them. Copied only when an operator is an Input.
	float historyResult[kTracePoints] = {};
	float historyScaled[3][kTracePoints] = {};
	float historyRaw[3][kTracePoints] = {};
	int historyWrite = 0;
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
		f.ratioTargets[i] = c.op[i].ratio;
		f.shown[i] = c.op[i].level != 0.0;
	}
	f.algorithm = c.algorithm;
}

static void buildFrame(WeaverFrame& f, const WeaverSnapshot& s) {
	renderFormulas(f, s.engine, s.controls, s.cycle, s.treatMode);
	for (int i = 0; i < 3; i++)
		f.traceOn[i] = s.traceOn[i];
	bool anyInput = false;
	for (int i = 0; i < 3; i++) {
		if (s.controls.op[i].wave != shapes::Input)
			continue;
		anyInput = true;
		std::copy(s.historyScaled[i], s.historyScaled[i] + kTracePoints, f.scaled[i]);
		// Oldest first, so the newest input is at the right and the trace scrolls
		for (int k = 0; k < kWavePoints; k++)
			f.wave[i][k] = s.historyRaw[i][(s.historyWrite + 1 + k * (kTracePoints / kWavePoints)) % kTracePoints];
	}
	if (anyInput)
		std::copy(s.historyResult, s.historyResult + kTracePoints, f.result);
	f.bpm = s.bpm;
	f.speed = s.speed;
	f.speedTarget = s.speedTarget;
	for (int i = 0; i < 3; i++)
		f.ratioTargets[i] = s.ratioTargets[i];
}

// A preset as listed for PRESET's buttons: a file in the folder, or one of
// the factory presets when they aren't there as files.
struct PresetEntry {
	std::string name;  // the file's stem, or the factory preset's name
	std::string file;  // its file name; empty for a built-in
	shapes::CombineControls controls;
	// A file saved with a loop wave keeps its patterns
	bool hasSeeds = false;
	uint32_t seeds[3] = {};
};

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
		// Next algorithm (the old single ALGORITHM button's slot)
		ALGORITHM_NEXT_PARAM,
		// The old ENVELOPE knob: no control, its slot kept so old patches load
		UNUSED_ENVELOPE_PARAM,
		PRESET_PREV_PARAM,
		PRESET_NEXT_PARAM,
		BPM_PARAM,
		RESET_PARAM,
		A_TRACE_PARAM,
		B_TRACE_PARAM,
		C_TRACE_PARAM,
		SPEED_DOWN_PARAM,
		SPEED_UP_PARAM,
		SPEED_RESET_PARAM,
		ALGORITHM_PREV_PARAM,
		PRESET_SAVE_PARAM,
		PRESET_DELETE_PARAM,
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
		SPEED_DOWN_INPUT,
		SPEED_UP_INPUT,
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
	bool newLoopOnReset = true;
	// A new algorithm waits for the end of the cycle
	bool algorithmAtCycleEnd = false;
	// A new ratio waits for a cycle start where it stays lined up with the
	// cycle (see holdRatios()), and a new SPEED for one the clock shares
	bool speedRatioAtCycleEnd = true;
	// With a clock, RESET waits for the next pulse; off, it acts at once and
	// the cycle keeps that offset from the clock
	bool resetOnPulse = true;
	int heldRatios[3] = {};
	int chosenRatios[3] = {};
	// The main display leaves out operators at LEVEL 0
	bool hideZeroLevelTraces = false;
	int heldAlgorithm = 0;
	// Take the algorithm now even when waiting (preset, load). Set from the
	// UI thread after the choices it publishes.
	std::atomic<bool> algorithmNow{true};
	// Line the operators up with the cycle again (a preset load), dropping
	// the phase offsets that keep ratio changes smooth. Set like algorithmNow.
	std::atomic<bool> realignNow{false};
	// Take the chosen ratios now (load, undo), without realigning
	std::atomic<bool> ratiosNow{true};
	// After a realign the outputs glide to the new shape over this many
	// samples, counting down
	int glideLeft = 0;
	// Each operator's output on the previous sample: the feedback into its
	// empty CV jacks
	float lastOpOut[3] = {};

	shapes::Timebase timebase;
	shapes::CycleSpeed speed;
	shapes::CombineEngine engine;
	shapes::Slew slew[4];  // OUT, then A, B, C
	shapes::Trigger eoc;
	dsp::BooleanTrigger waveTriggers[3], algorithmPrevTrigger, algorithmNextTrigger, speedDownTrigger, speedUpTrigger, speedResetTrigger;
	dsp::SchmittTrigger speedDownCv, speedUpCv;
	dsp::BooleanTrigger presetSaveTrigger, presetDeleteTrigger;
#ifdef METAMODULE
	// MetaModule has no widget events, so the PRESET buttons are read here
	dsp::BooleanTrigger presetPrevTrigger, presetNextTrigger;
	// The wave each WAVE knob was last seen at; kAdoptWave until the first
	// process() after a load, which turns the knobs to the loaded waves
	static constexpr int kAdoptWave = -1;
	int knobWaves[3] = {kAdoptWave, kAdoptWave, kAdoptWave};
#endif
	float sampleRate = 48000.f;
	float appliedSlewMs = -1.f;
	int sincePublish = shapeui::kPublishInterval;
	// The engine runs every kControlInterval samples, and the outputs (MAIN,
	// then A, B, C) ramp between
	shapes::Ramp ramps[4];
	bool rampStarted = false;
	int untilEval = 0;
	shapes::CombineControls evalControls;

	// The last cycle as it played (see WeaverSnapshot)
	float historyResult[kTracePoints] = {};
	float historyScaled[3][kTracePoints] = {};
	float historyRaw[3][kTracePoints] = {};
	int historyWrite = 0;

	WeaverSnapshot snapshots[2];
	std::atomic<int> frontSnapshot{0};
	std::atomic<uint32_t> publishCount{0};
	std::atomic<float> displayPos{0.f};
	// UI thread only: the snapshot being drawn and the frame made from it
	WeaverSnapshot uiSnapshot;
	WeaverFrame uiFrame;
	uint32_t uiCount = ~0u;

	// Presets. The file service (the UI thread on VCV, a thread of its own on
	// MetaModule) does all the file work; the rest reaches it through these.
	// The folder of the last preset file saved as; empty means the default
	// folder. Saved with the patch.
	shapes::SharedPath<1023> folder;
	// The preset last loaded or saved, preferred when several match; saved
	// with the patch
	shapes::SharedPath<255> currentPreset;
	// The preset the settings match, for the display; "" for none
	shapes::SharedPath<255> presetTitle;
	// Shown in the preset's place for a while
	shapes::ScreenMessage<kMessageChars> message;
	// Button presses (process() and the menus) for the file service
	std::atomic<int> presetStepRequest{0}, saveRequest{0}, deleteRequest{0};
	std::atomic<bool> restoreRequest{false};
	// The folder may have changed (a patch load)
	std::atomic<bool> rescanRequest{true};
	// File service only: the presets in order, the folder they're from, and
	// a DELETE waiting for its second press
	std::vector<PresetEntry> presets;
	std::string presetsDir;
	std::string deleteArmed;
	double deleteArmedUntil = 0.0;

#ifdef METAMODULE
	// File service to process(): a preset to load, as the file service can't
	// set the module's state while process() runs. 0 empty, 1 ready.
	PresetEntry pendingPreset;
	std::atomic<int> pendingState{0};
	// Menu to file service: open the Save as browser (2)
	std::atomic<int> dialogRequest{0};
	// File browser (GUI) to file service: the path chosen, in a single slot.
	// 0 empty, 1 being written, 2 ready.
	std::atomic<int> dialogState{0};
	std::string dialogPath;
	// Runs the file service whether or not the module is on screen
	MetaModule::AsyncThread fileThread{this, [this]() { serviceFiles(); }};
#endif

	static int param(int op, int first) {
		return first + op * kOpParams;
	}
	static int input(int op, int first) {
		return first + op * kOpInputs;
	}

	Weaver();
	~Weaver() {
#ifdef METAMODULE
		fileThread.stop();
#endif
	}

	void setRate(float rate) {
		sampleRate = rate;
		timebase.setSampleRate(rate);
		// Its Input slews run once an evaluation
		engine.setSampleRate(rate / shapes::kControlInterval);
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
		loadPreset(0);
		speed.jumpTo(0);
		settings.reset();
		algorithmAtCycleEnd = false;
		speedRatioAtCycleEnd = true;
		resetOnPulse = true;
		timebase.setClockOffset(0.0);
		newLoopOnReset = true;
		hideZeroLevelTraces = false;
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

	// Operator i's knob plus attenuverted CV (5 V spans the knob's range),
	// unclamped. With the jack empty, clockwise feeds the operator's own
	// output in as if it were patched, and counter-clockwise adds a random
	// offset that glides from one of the operator's cycles to the next. opT
	// is the operator's position in its own cycles, its phase left out.
	double modulated(int i, int knob, int in, int trim, double span, double opT) {
		double amount = params[trim].getValue();
		double v = params[knob].getValue();
		if (inputs[in].isConnected())
			v += cv(in) * 0.2 * amount * span;
		else if (amount > 0.0)
			v += lastOpOut[i] * 0.2 * amount * span;
		else if (amount < 0.0)
			v += -amount * span * shapes::trimRandomOffset((uint32_t) trim, opT, shapes::TrimSpread::Bipolar, true);
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
		return c;
	}

	void loadPreset(int n) {
		applyControls(shapes::kWeaverPresets[n].controls);
	}

	void applyControls(const shapes::CombineControls& p) {
		for (int i = 0; i < 3; i++) {
			const shapes::OperatorControls& o = p.op[i];
			waves[i] = o.wave;
			params[param(i, A_RATIO_PARAM)].setValue((float) o.ratio);
			params[param(i, A_PARAM_PARAM)].setValue((float) o.param);
			params[param(i, A_PHASE_PARAM)].setValue((float) o.phase);
			params[param(i, A_LEVEL_PARAM)].setValue((float) o.level);
		}
		algorithm = p.algorithm;
		algorithmNow.store(true, std::memory_order_release);
		realignNow.store(true, std::memory_order_release);
	}

	// A listed preset's settings, and its loop patterns if it kept them. On
	// VCV the UI thread calls it; on MetaModule process() does.
	void applyPreset(const PresetEntry& p) {
		applyControls(p.controls);
		for (int i = 0; i < 3; i++) {
			if (p.hasSeeds)
				loopSeeds[i] = p.seeds[i];
			else if (shapes::isLoop((shapes::Wave) waves[i]))
				loopSeeds[i] = random::u32();
		}
	}

	void showMessage(const std::string& text) {
		message.set(text, system::getTime() + kMessageSeconds);
	}

	// The file service's work, below the module
	void serviceFiles();
	std::string presetFolder();
	void scanPresets();
	int matchingPreset();
	void stepPreset(int delta);
	void loadListed(const PresetEntry& p);
	void quickSave();
	void saveAs();
	void saveTo(std::string path, bool named);
	void deletePress();
	void restoreFactory();
#ifdef METAMODULE
	// GUI context: the file browser's choice, for the file service
	void postDialogPath(const char* path) {
		int expected = 0;
		if (!dialogState.compare_exchange_strong(expected, 1)) {
			showMessage("BUSY");
			return;
		}
		dialogPath = path;
		dialogState.store(2, std::memory_order_release);
	}
#endif

	// At t cycles since reset
	shapes::CombineControls controls(double t) {
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
			// The running ratio and the engine's offset from the last
			// evaluation, so a ratio change doesn't jump the random
			double opT = t * shapes::ratioValue(heldRatios[i]) + engine.ratioOffset(i);
			o.phase = shapes::frac(modulated(i, param(i, A_PHASE_PARAM), input(i, A_PHASE_INPUT), param(i, A_PHASE_CV_PARAM), 1.0, opT));
			o.level = std::clamp(modulated(i, param(i, A_LEVEL_PARAM), input(i, A_LEVEL_INPUT), param(i, A_LEVEL_CV_PARAM), 2.0, opT), -1.0, 1.0);
			if (o.wave == shapes::Input) {
				// The PARAM jack is the signal, its trimpot the gain, and the
				// knob the slew
				o.param = clamp(params[param(i, A_PARAM_PARAM)].getValue(), 0.f, 1.f);
				o.inputVolts = cv(input(i, A_PARAM_INPUT));
				o.inputGain = 1.0 + params[param(i, A_PARAM_CV_PARAM)].getValue();
			}
			else {
				o.param = std::clamp(modulated(i, param(i, A_PARAM_PARAM), input(i, A_PARAM_INPUT), param(i, A_PARAM_CV_PARAM), 1.0, opT), 0.0, 1.0);
			}
		}
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
		shapes::TimebaseInput tin;
		tin.bpm = shapeui::bpmOf(params[BPM_PARAM].getValue());
		tin.clockConnected = inputs[CLOCK_INPUT].isConnected();
		tin.clockVoltage = cv(CLOCK_INPUT);
		tin.resetVoltage = cv(RESET_INPUT);
		tin.resetButton = params[RESET_PARAM].getValue() > 0.5f;
		tin.resetOnPulse = resetOnPulse;
		tin.pulsesPerCycle = settings.pulsesPerCycle;
		shapes::Timebase::Step step = timebase.process(tin);
		// From here on a cycle is SPEED's
		shapes::CycleSpeed::Step sped = speed.process(step.cycle, step.pos, step.reset, step.wrapped);
		if (step.reset) {
			engine.reset();
			if (newLoopOnReset)
				newLoops();
		}
		// The Input peak rolls over here, before this sample is processed
		if (sped.wrapped)
			engine.cycleWrapped();
		// Reset starts a cycle too, so a waiting algorithm lands there
		bool cycleStart = sped.wrapped || step.reset;

		// A cycle start is worked out on its own sample, so the algorithm
		// held for it lands there
		if (cycleStart || --untilEval <= 0) {
			untilEval = shapes::kControlInterval;
			evaluate(sped.t, cycleStart);
		}

		float out = output(0, ramps[0].next());
		outputs[MAIN_OUTPUT].setVoltage(out);
		outputs[INV_OUTPUT].setVoltage((float) shapes::sanitize(shapes::invertVolts(out, settings.range), 0.0));
		if (sped.wrapped)
			eoc.fire();
		outputs[EOC_OUTPUT].setVoltage(eoc.process(args.sampleTime));
		for (int i = 0; i < 3; i++) {
			lastOpOut[i] = output(1 + i, ramps[1 + i].next());
			outputs[A_OUTPUT + i].setVoltage(lastOpOut[i]);
		}

		double cycle = std::floor(sped.t);
		displayPos.store((float) (sped.t - cycle), std::memory_order_relaxed);
		if (++sincePublish >= shapeui::kPublishInterval) {
			sincePublish = 0;
			publish(evalControls, cycle);
		}
	}

	// With speedRatioAtCycleEnd, each operator keeps its running ratio until a
	// cycle start where the change leaves a whole-number phase offset, so the
	// operator stays lined up with the cycle: the next one for the whole-number
	// ratios, up to 8 cycles for 1/2, 1/4 and 1/8. Otherwise, or on a preset
	// load, a change is taken at once.
	void holdRatios(shapes::CombineControls& c, double t, bool cycleStart, bool now) {
		double start = std::floor(t);
		bool aligned = false;
		for (int i = 0; i < 3; i++) {
			int chosen = c.op[i].ratio;
			chosenRatios[i] = chosen;
			if (chosen == heldRatios[i])
				continue;
			if (now || !speedRatioAtCycleEnd) {
				heldRatios[i] = chosen;
				continue;
			}
			if (!cycleStart)
				continue;
			double turns = start * (shapes::ratioValue(heldRatios[i]) - shapes::ratioValue(chosen));
			if (turns == std::floor(turns)) {
				heldRatios[i] = chosen;
				aligned = true;
			}
		}
		for (int i = 0; i < 3; i++)
			c.op[i].ratio = heldRatios[i];
		if (aligned)
			engine.ratioChangesAt(start);
	}

	// The buttons, controls, engine, display history and lights, at control
	// rate, at t cycles since reset: sets the outputs' next ramps
	void evaluate(double t, bool cycleStart) {
		if (presetSaveTrigger.process(params[PRESET_SAVE_PARAM].getValue() > 0.5f))
			saveRequest.fetch_add(1);
		if (presetDeleteTrigger.process(params[PRESET_DELETE_PARAM].getValue() > 0.5f))
			deleteRequest.fetch_add(1);
#ifdef METAMODULE
		if (presetPrevTrigger.process(params[PRESET_PREV_PARAM].getValue() > 0.5f))
			presetStepRequest.fetch_sub(1);
		if (presetNextTrigger.process(params[PRESET_NEXT_PARAM].getValue() > 0.5f))
			presetStepRequest.fetch_add(1);
		if (pendingState.load(std::memory_order_acquire) == 1) {
			applyPreset(pendingPreset);
			pendingState.store(0, std::memory_order_release);
		}
		// The WAVE knobs: turning one picks its wave, and a wave picked any
		// other way (preset, load, randomize) turns the knob to match
		for (int i = 0; i < 3; i++) {
			Param& knob = params[param(i, A_WAVE_PARAM)];
			int w = clamp((int) std::round(knob.getValue()), 0, shapes::kNumWaves - 1);
			if (knobWaves[i] != kAdoptWave && w != knobWaves[i])
				selectWave(i, w);
			else if (w != waves[i])
				knob.setValue((float) waves[i]);
			knobWaves[i] = waves[i];
		}
#else
		for (int i = 0; i < 3; i++)
			if (waveTriggers[i].process(params[param(i, A_WAVE_PARAM)].getValue() > 0.5f))
				selectWave(i, (waves[i] + 1) % shapes::kNumWaves);
#endif
		if (algorithmPrevTrigger.process(params[ALGORITHM_PREV_PARAM].getValue() > 0.5f))
			algorithm = (algorithm + shapes::kNumAlgorithms - 1) % shapes::kNumAlgorithms;
		if (algorithmNextTrigger.process(params[ALGORITHM_NEXT_PARAM].getValue() > 0.5f))
			algorithm = (algorithm + 1) % shapes::kNumAlgorithms;
		// SPEED's jacks do what their buttons do
		bool down = speedDownTrigger.process(params[SPEED_DOWN_PARAM].getValue() > 0.5f);
		down |= speedDownCv.process(cv(SPEED_DOWN_INPUT), 0.1f, 1.f);
		bool up = speedUpTrigger.process(params[SPEED_UP_PARAM].getValue() > 0.5f);
		up |= speedUpCv.process(cv(SPEED_UP_INPUT), 0.1f, 1.f);
		speed.setImmediate(!speedRatioAtCycleEnd);
		if (down)
			speed.setTarget(speed.targetOctaves() - 1);
		if (up)
			speed.setTarget(speed.targetOctaves() + 1);
		if (speedResetTrigger.process(params[SPEED_RESET_PARAM].getValue() > 0.5f))
			speed.setTarget(0);
		if (settings.slewMs != appliedSlewMs) {
			for (shapes::Slew& s : slew)
				s.set(settings.slewMs, sampleRate);
			appliedSlewMs = settings.slewMs;
		}

		shapes::CombineControls c = controls(t);
		if (algorithmNow.exchange(false, std::memory_order_acquire) || !algorithmAtCycleEnd || cycleStart)
			heldAlgorithm = targetAlgorithm();
		c.algorithm = (shapes::Algorithm) heldAlgorithm;
		bool realign = realignNow.exchange(false, std::memory_order_acquire);
		holdRatios(c, t, cycleStart, realign || ratiosNow.exchange(false, std::memory_order_acquire));
		evalControls = c;

		if (realign) {
			engine.realign();
			glideLeft = (int) (kRealignGlideS * sampleRate);
		}
		shapes::CombineFrame f = engine.process(c, t, false);

		double v[4];
		v[0] = shapes::sanitize(shapes::treat(f.result, settings.treatMode, engine.peak()), 0.0);
		// Each operator's own wave at full level. A single wave has no cycle
		// peak to fit (an Input can be any size), so Fit clips here.
		shapes::Treat opMode = settings.treatMode == shapes::Treat::Fit ? shapes::Treat::Clip : settings.treatMode;
		for (int i = 0; i < 3; i++) {
			double r = f.raw[i];
			// An Envelope is 0..1: in a unipolar range it spans the full range like the others
			if (shapes::isUnipolar(settings.range) && c.op[i].wave == shapes::Envelope)
				r = 2.0 * r - 1.0;
			v[1 + i] = shapes::treat(r, opMode, 1.0);
		}
		for (int i = 0; i < 4; i++) {
			// A realign's jump becomes a glide that lands on the new shape
			if (rampStarted)
				ramps[i].target(v[i], std::max(shapes::kControlInterval, glideLeft));
			else
				ramps[i].jump(v[i]);
		}
		rampStarted = true;
		glideLeft = std::max(glideLeft - shapes::kControlInterval, 0);

		historyWrite = clamp((int) std::lround((t - std::floor(t)) * (kTracePoints - 1)), 0, kTracePoints - 1);
		historyResult[historyWrite] = (float) v[0];
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
	}

	void publish(const shapes::CombineControls& c, double cycle) {
		int back = 1 - frontSnapshot.load(std::memory_order_relaxed);
		WeaverSnapshot& s = snapshots[back];
		s.controls = c;
		s.engine = engine;
		s.cycle = cycle;
		s.treatMode = settings.treatMode;
		for (int i = 0; i < 3; i++)
			s.traceOn[i] = params[A_TRACE_PARAM + i].getValue() > 0.5f;
		bool anyInput = false;
		for (int i = 0; i < 3; i++) {
			if (c.op[i].wave != shapes::Input)
				continue;
			anyInput = true;
			std::copy(historyScaled[i], historyScaled[i] + kTracePoints, s.historyScaled[i]);
			std::copy(historyRaw[i], historyRaw[i] + kTracePoints, s.historyRaw[i]);
		}
		if (anyInput)
			std::copy(historyResult, historyResult + kTracePoints, s.historyResult);
		s.historyWrite = historyWrite;
		s.bpm = (float) timebase.tempoBpm();
		s.speed = speed.octaves();
		s.speedTarget = speed.targetOctaves();
		for (int i = 0; i < 3; i++)
			s.ratioTargets[i] = chosenRatios[i];
		frontSnapshot.store(back, std::memory_order_release);
		publishCount.fetch_add(1, std::memory_order_release);
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
		json_object_set_new(rootJ, "speedRatioAtCycleEnd", json_boolean(speedRatioAtCycleEnd));
		json_object_set_new(rootJ, "resetOnPulse", json_boolean(resetOnPulse));
		json_object_set_new(rootJ, "clockOffset", json_real(timebase.clockOffset()));
		json_object_set_new(rootJ, "hideZeroLevelTraces", json_boolean(hideZeroLevelTraces));
		json_object_set_new(rootJ, "speed", json_integer(speed.targetOctaves()));
		json_object_set_new(rootJ, "presetFolder", json_string(folder.get().c_str()));
		json_object_set_new(rootJ, "preset", json_string(currentPreset.get().c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
#ifdef METAMODULE
		// The WAVE knobs follow the loaded waves (a patch from before they were knobs saved 0)
		for (int i = 0; i < 3; i++)
			knobWaves[i] = kAdoptWave;
#endif
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
		j = json_object_get(rootJ, "speedRatioAtCycleEnd");
		speedRatioAtCycleEnd = !j || json_is_true(j);
		j = json_object_get(rootJ, "resetOnPulse");
		resetOnPulse = !j || json_is_true(j);
		j = json_object_get(rootJ, "clockOffset");
		timebase.setClockOffset(j && json_is_number(j) ? json_number_value(j) : 0.0);
		j = json_object_get(rootJ, "newLoopOnReset");
		newLoopOnReset = !j || json_is_true(j);
		j = json_object_get(rootJ, "hideZeroLevelTraces");
		hideZeroLevelTraces = j && json_is_true(j);
		j = json_object_get(rootJ, "speed");
		speed.jumpTo(j && json_is_integer(j) ? (int) json_integer_value(j) : 0);
		ratiosNow.store(true, std::memory_order_release);
		// The other platform's folder means nothing here: the default instead
		j = json_object_get(rootJ, "presetFolder");
		folder.set(shapes::usableFolder(j && json_is_string(j) ? json_string_value(j) : "", kOnMetaModule));
		j = json_object_get(rootJ, "preset");
		currentPreset.set(j && json_is_string(j) ? json_string_value(j) : "");
		rescanRequest.store(true);
	}
};

// The wave button names the chosen wave
struct WaveQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		Weaver* m = dynamic_cast<Weaver*>(module);
		return m ? kWaveNames[m->waves[(paramId - Weaver::A_WAVE_PARAM) / Weaver::kOpParams]] : "";
	}
};

// The algorithm buttons name the algorithm each one steps to
struct AlgorithmQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		Weaver* m = dynamic_cast<Weaver*>(module);
		if (!m)
			return "";
		int a = m->algorithm + (paramId == Weaver::ALGORITHM_PREV_PARAM ? shapes::kNumAlgorithms - 1 : 1);
		return shapes::algorithmName((shapes::Algorithm) (a % shapes::kNumAlgorithms));
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
#ifdef METAMODULE
	std::vector<std::string> waveNames(kWaveNames, kWaveNames + shapes::kNumWaves);
#endif
	// Double-clicking a knob gives these neutral values (x1, 50%, no phase, LEVEL 0);
	// the module itself starts on the first preset
	const shapes::OperatorControls neutral;
	for (int i = 0; i < 3; i++) {
		std::string op(1, (char) ('A' + i));
		configSwitch(param(i, A_RATIO_PARAM), 0.f, shapes::kNumRatios - 1, (float) neutral.ratio, op + " ratio", ratioNames);
#ifdef METAMODULE
		// A knob that names the wave; randomize picks waves itself, never Input
		configSwitch(param(i, A_WAVE_PARAM), 0.f, shapes::kNumWaves - 1, (float) defaults().op[i].wave, op + " wave", waveNames)->randomizeEnabled = false;
#else
		configButton<WaveQuantity>(param(i, A_WAVE_PARAM), op + " wave");
#endif
		configParam<OperatorParamQuantity>(param(i, A_PARAM_PARAM), 0.f, 1.f, (float) neutral.param, op + " param", "%", 0.f, 100.f);
		configParam(param(i, A_PHASE_PARAM), 0.f, 1.f, (float) neutral.phase, op + " phase", " deg", 0.f, 360.f);
		configParam(param(i, A_LEVEL_PARAM), -1.f, 1.f, 0.f, op + " level", "%", 0.f, 100.f);
		configParam(param(i, A_PARAM_CV_PARAM), -1.f, 1.f, 0.f, op + " param CV amount");
		configParam(param(i, A_PHASE_CV_PARAM), -1.f, 1.f, 0.f, op + " phase CV amount");
		configParam(param(i, A_LEVEL_CV_PARAM), -1.f, 1.f, 0.f, op + " level CV amount");
		configInput(input(i, A_RATIO_INPUT), op + " ratio CV");
		configInput(input(i, A_PARAM_INPUT), op + " param CV");
		configInput(input(i, A_PHASE_INPUT), op + " phase CV");
		configInput(input(i, A_LEVEL_INPUT), op + " level CV");
		configOutput(A_OUTPUT + i, "Operator " + op);
		configLight(A_WAVE_LIGHT + 3 * i, op + " wave");
	}
	configButton<AlgorithmQuantity>(ALGORITHM_PREV_PARAM, "Previous algorithm");
	configButton<AlgorithmQuantity>(ALGORITHM_NEXT_PARAM, "Next algorithm");
	configParam(UNUSED_ENVELOPE_PARAM, 0.f, 1.f, 0.f, "Unused")->randomizeEnabled = false;
	configButton(PRESET_PREV_PARAM, "Previous preset");
	configButton(PRESET_NEXT_PARAM, "Next preset");
	configButton(PRESET_SAVE_PARAM, "Save preset");
	configButton(PRESET_DELETE_PARAM, "Delete preset");
	configButton(SPEED_DOWN_PARAM, "Half speed");
	configButton(SPEED_UP_PARAM, "Double speed");
	configButton(SPEED_RESET_PARAM, "Speed x1");
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
	configInput(SPEED_DOWN_INPUT, "Half speed");
	configInput(SPEED_UP_INPUT, "Double speed");
	configOutput(MAIN_OUTPUT, "Main");
	configOutput(INV_OUTPUT, "Inverted");
	configOutput(EOC_OUTPUT, "End of cycle");
	loadPreset(0);
	for (int i = 0; i < 3; i++)
		loopSeeds[i] = random::u32();
#ifdef METAMODULE
	// One point an evaluation: the full scan at once dominated the cost
	engine.setScanSpread(1);
#endif
	setRate(APP->engine->getSampleRate());
#ifdef METAMODULE
	fileThread.start();
#endif
}

// File work, all in the file service (on VCV, the UI thread)

// Name to index in a list of names, ignoring case; -1 when it's none of them
static int indexOfName(const char* name, int count, const char* (*nameOf)(int)) {
	std::string want = shapes::lowerCase(name);
	for (int i = 0; i < count; i++)
		if (shapes::lowerCase(nameOf(i)) == want)
			return i;
	return -1;
}

static const char* waveName(int w) {
	return kWaveNames[w];
}
static const char* algorithmNameOf(int a) {
	return shapes::algorithmName((shapes::Algorithm) a);
}

// Waves, ratios and the algorithm go by name, so a file still loads if
// their order changes
static bool writePresetFile(const std::string& path, const PresetEntry& p) {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "weaverPreset", json_integer(1));
	json_object_set_new(rootJ, "algorithm", json_string(algorithmNameOf(p.controls.algorithm)));
	json_t* opsJ = json_array();
	for (int i = 0; i < 3; i++) {
		const shapes::OperatorControls& o = p.controls.op[i];
		json_t* opJ = json_object();
		json_object_set_new(opJ, "wave", json_string(kWaveNames[o.wave]));
		json_object_set_new(opJ, "ratio", json_string(shapes::ratioLabel(o.ratio)));
		json_object_set_new(opJ, "param", json_real(o.param));
		json_object_set_new(opJ, "phase", json_real(o.phase));
		json_object_set_new(opJ, "level", json_real(o.level));
		if (p.hasSeeds)
			json_object_set_new(opJ, "loopSeed", json_integer(p.seeds[i]));
		json_array_append_new(opsJ, opJ);
	}
	json_object_set_new(rootJ, "operators", opsJ);
	char* text = json_dumps(rootJ, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
	json_decref(rootJ);
	if (!text)
		return false;
	FILE* f = std::fopen(path.c_str(), "wb");
	bool ok = false;
	if (f) {
		size_t n = strlen(text);
		ok = std::fwrite(text, 1, n, f) == n;
		ok = std::fclose(f) == 0 && ok;
	}
	std::free(text);
	return ok;
}

static double numberIn(json_t* objJ, const char* key, double lo, double hi, bool& ok) {
	json_t* j = json_object_get(objJ, key);
	if (!j || !json_is_number(j) || !std::isfinite(json_number_value(j))) {
		ok = false;
		return lo;
	}
	return std::clamp(json_number_value(j), lo, hi);
}

static const char* stringIn(json_t* objJ, const char* key) {
	json_t* j = json_object_get(objJ, key);
	return j && json_is_string(j) ? json_string_value(j) : "";
}

// False for anything that isn't a whole preset (a deleted one on MetaModule
// is an empty file)
static bool readPresetFile(const std::string& path, PresetEntry& p) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::string text;
	char buf[1024];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 65536)
		text.append(buf, n);
	std::fclose(f);
	json_error_t error;
	json_t* rootJ = json_loads(text.c_str(), 0, &error);
	if (!rootJ)
		return false;
	bool ok = json_is_object(rootJ);
	int a = indexOfName(stringIn(rootJ, "algorithm"), shapes::kNumAlgorithms, algorithmNameOf);
	ok = ok && a >= 0;
	p.controls.algorithm = (shapes::Algorithm) std::max(a, 0);
	json_t* opsJ = json_object_get(rootJ, "operators");
	ok = ok && opsJ && json_is_array(opsJ) && json_array_size(opsJ) == 3;
	p.hasSeeds = ok;
	for (int i = 0; i < 3 && ok; i++) {
		json_t* opJ = json_array_get(opsJ, i);
		shapes::OperatorControls& o = p.controls.op[i];
		int w = indexOfName(stringIn(opJ, "wave"), shapes::kNumWaves, waveName);
		int r = indexOfName(stringIn(opJ, "ratio"), shapes::kNumRatios, shapes::ratioLabel);
		ok = w >= 0 && r >= 0;
		o.wave = (shapes::Wave) std::max(w, 0);
		o.ratio = std::max(r, 0);
		o.param = numberIn(opJ, "param", 0.0, 1.0, ok);
		o.phase = numberIn(opJ, "phase", 0.0, 1.0, ok);
		o.level = numberIn(opJ, "level", -1.0, 1.0, ok);
		json_t* seedJ = json_object_get(opJ, "loopSeed");
		if (seedJ && json_is_integer(seedJ))
			p.seeds[i] = (uint32_t) json_integer_value(seedJ);
		else
			p.hasSeeds = false;
	}
	json_decref(rootJ);
	return ok;
}

static PresetEntry factoryEntry(int n) {
	PresetEntry p;
	p.name = shapes::factoryPresetName(n);
	p.controls = shapes::kWeaverPresets[n].controls;
	return p;
}

// The factory presets as files, and the marker that says they've been written
static bool writeFactoryPresets(const std::string& dir) {
	bool ok = true;
	for (int n = 0; n < shapes::kNumWeaverPresets; n++) {
		PresetEntry p = factoryEntry(n);
		ok = writePresetFile(system::join(dir, p.name + ".json"), p) && ok;
	}
	FILE* f = std::fopen(system::join(dir, shapes::kFactoryMarker).c_str(), "wb");
	if (!f)
		return false;
	std::fputs("Weaver's factory presets were written to this folder.\n", f);
	return std::fclose(f) == 0 && ok;
}

static std::string defaultPresetFolder() {
#ifdef METAMODULE
	// MetaModule can't create folders: the first of these that exists,
	// looked up each time so a card put in later is found. "" when there's none.
	return shapes::pickFolder(shapes::kWeaverMetaModuleFolders, isFolder);
#else
	return asset::user("RobotBoy/Weaver");
#endif
}

// The saved folder if it's still there, else the default; "" when there's neither
std::string Weaver::presetFolder() {
	std::string f = folder.get();
	if (!f.empty() && isFolder(f))
		return f;
	return defaultPresetFolder();
}

// Lists the folder's presets. The default folder gets the factory presets
// as files the first time; a folder without them (a card's root, say) lists
// them first, built in.
void Weaver::scanPresets() {
	std::string dir = presetFolder();
#ifndef METAMODULE
	if (dir == defaultPresetFolder() && !ensureDirectory(dir))
		dir = "";
#endif
	std::vector<std::string> names = dir.empty() ? std::vector<std::string>() : fileNames(dir);
	auto hasMarker = [&]() { return std::find(names.begin(), names.end(), shapes::kFactoryMarker) != names.end(); };
	if (!dir.empty() && !hasMarker() && !shapes::isVolumeRoot(dir) && dir == defaultPresetFolder()) {
		writeFactoryPresets(dir);
		names = fileNames(dir);
	}
	presets.clear();
	if (!hasMarker())
		for (int n = 0; n < shapes::kNumWeaverPresets; n++)
			presets.push_back(factoryEntry(n));
	for (const std::string& file : shapes::sortedPresetNames(names)) {
		PresetEntry p;
		if (!readPresetFile(system::join(dir, file), p))
			continue;
		p.name = system::getStem(file);
		p.file = file;
		presets.push_back(p);
	}
	presetsDir = dir;
}

// The listed preset the settings match, the current one if it does; -1 for none
int Weaver::matchingPreset() {
	shapes::CombineControls c = panelControls();
	std::string current = currentPreset.get();
	int first = -1;
	for (int i = 0; i < (int) presets.size(); i++) {
		if (!shapes::sameSettings(presets[i].controls, c))
			continue;
		if (presets[i].name == current)
			return i;
		if (first < 0)
			first = i;
	}
	return first;
}

void Weaver::loadListed(const PresetEntry& p) {
#ifdef METAMODULE
	if (pendingState.load(std::memory_order_acquire) != 0) {
		showMessage("BUSY");
		return;
	}
	pendingPreset = p;
	pendingState.store(1, std::memory_order_release);
#else
	applyPreset(p);
#endif
	currentPreset.set(p.name);
	// The new preset's name shows at once, and a DELETE waiting for its
	// second press is called off
	message.set("", 0.0);
	deleteArmed.clear();
}

// Steps through the presets; from settings that match none, Next starts at
// the first and Previous at the last. The folder is listed again first, so
// files added since show up.
void Weaver::stepPreset(int delta) {
	scanPresets();
	if (presets.empty()) {
		showMessage("NO PRESETS");
		return;
	}
	int n = (int) presets.size();
	int i = matchingPreset();
	i = i < 0 ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
	loadListed(presets[i]);
}

// Deletes a preset's file. MetaModule can't delete files, so there it's
// emptied instead, which drops it from the list.
static bool removePresetFile(const std::string& path) {
	if (system::remove(path))
		return true;
	FILE* f = std::fopen(path.c_str(), "wb");
	return f && std::fclose(f) == 0;
}

// Saves the settings to path, unless a preset in the same folder already
// has them: SAVE then just names it, and Save as (named) renames its file
// to the name chosen. A built-in can't be renamed.
void Weaver::saveTo(std::string path, bool named) {
	if (shapes::lowerCase(system::getExtension(path)) != ".json")
		path += ".json";
	scanPresets();
	auto folderOf = [](const std::string& dir) { return shapes::usableFolder(dir, kOnMetaModule); };
	int match = folderOf(system::getDirectory(path)) == folderOf(presetsDir) ? matchingPreset() : -1;
	std::string replaced;
	if (match >= 0) {
		const PresetEntry& m = presets[match];
		if (!named || m.file.empty()) {
			currentPreset.set(m.name);
			showMessage("ALREADY SAVED: " + m.name);
			return;
		}
		if (shapes::lowerCase(m.file) != shapes::lowerCase(system::getFilename(path)))
			replaced = system::join(presetsDir, m.file);
	}
	PresetEntry p;
	p.controls = panelControls();
	p.hasSeeds = true;
	for (int i = 0; i < 3; i++)
		p.seeds[i] = loopSeeds[i];
	if (!writePresetFile(path, p)) {
		showMessage("SAVE FAILED");
		return;
	}
	if (!replaced.empty())
		removePresetFile(replaced);
	std::string stem = system::getStem(path);
	currentPreset.set(stem);
	showMessage("SAVED " + stem);
	scanPresets();
}

#ifdef METAMODULE
// Opens the firmware's file browser; the path chosen reaches the file
// service through postDialogPath
static void presetDialog(Weaver* m, const std::string& dir, const char* name) {
	osdialog_filters* filters = osdialog_filters_parse("Weaver preset:json");
	async_osdialog_file(OSDIALOG_SAVE, dir.c_str(), name, filters, [m](char* path) {
		if (!path)
			return;
		m->postDialogPath(path);
		std::free(path);
	});
	osdialog_filters_free(filters);
}
#endif

void Weaver::saveAs() {
	std::string dir = presetFolder();
	std::string name = shapes::nextPresetName(fileNames(dir));
#ifdef METAMODULE
	presetDialog(this, dir, name.c_str());
#else
	osdialog_filters* filters = osdialog_filters_parse("Weaver preset:json");
	char* path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), name.c_str(), filters);
	osdialog_filters_free(filters);
	if (!path)
		return;
	std::string chosen = path;
	std::free(path);
	folder.set(shapes::usableFolder(system::getDirectory(chosen), kOnMetaModule));
	saveTo(chosen, true);
#endif
}

// SAVE: no dialog, the next free name in the current folder
void Weaver::quickSave() {
	std::string dir = presetFolder();
#ifdef METAMODULE
	// Nothing to save to (no card, say), and no making a folder here
	bool usable = !dir.empty();
#else
	bool usable = ensureDirectory(dir);
#endif
	if (usable)
		saveTo(system::join(dir, shapes::nextPresetName(fileNames(dir))), false);
	else
		saveAs();
}

// The first press asks for a second within kMessageSeconds; the second
// deletes the preset the settings match
void Weaver::deletePress() {
	int i = matchingPreset();
	if (i < 0) {
		showMessage("NO PRESET TO DELETE");
		return;
	}
	const PresetEntry& p = presets[i];
	if (p.file.empty()) {
		showMessage("BUILT IN: CAN'T DELETE");
		return;
	}
	double now = system::getTime();
	if (deleteArmed != p.name || now >= deleteArmedUntil) {
		deleteArmed = p.name;
		deleteArmedUntil = now + kMessageSeconds;
		showMessage("DELETE PRESET? PRESS AGAIN");
		return;
	}
	deleteArmed.clear();
	std::string path = system::join(presetsDir, p.file);
	std::string name = p.name;
	if (!removePresetFile(path)) {
		showMessage("DELETE FAILED");
		return;
	}
	if (currentPreset.get() == name)
		currentPreset.set("");
	showMessage("DELETED " + name);
	scanPresets();
}

// Writes the factory presets back, over any of the same name, leaving the
// rest alone
void Weaver::restoreFactory() {
	std::string dir = presetFolder();
#ifndef METAMODULE
	if (!ensureDirectory(dir))
		dir = "";
#endif
	if (dir.empty()) {
		showMessage(kOnMetaModule ? "NO CARD" : "NO FOLDER");
		return;
	}
	if (shapes::isVolumeRoot(dir)) {
		// They're listed built in, and can't be deleted there
		showMessage("FACTORY PRESETS BUILT IN");
		return;
	}
	showMessage(writeFactoryPresets(dir) ? "FACTORY PRESETS RESTORED" : "RESTORE FAILED");
	scanPresets();
}

// All the file work: VCV's widget calls it each frame; MetaModule's file
// thread calls it whether or not the module is on screen
void Weaver::serviceFiles() {
	if (rescanRequest.exchange(false))
		scanPresets();
#ifdef METAMODULE
	if (dialogState.load(std::memory_order_acquire) == 2) {
		std::string path = dialogPath;
		dialogState.store(0, std::memory_order_release);
		folder.set(shapes::usableFolder(system::getDirectory(path), kOnMetaModule));
		saveTo(path, true);
	}
	if (dialogRequest.exchange(0) == 2)
		saveAs();
#endif
	if (restoreRequest.exchange(false))
		restoreFactory();
	if (saveRequest.exchange(0) > 0)
		quickSave();
	if (deleteRequest.exchange(0) > 0)
		deletePress();
	// Presses on both cancel out
	int delta = presetStepRequest.exchange(0);
	if (delta != 0)
		stepPreset(delta);
	int i = matchingPreset();
	presetTitle.set(i >= 0 ? presets[i].name : "");
}

// The module's latest frame, drawn up once per publish, or, in the module
// browser, the first preset at default settings, built once. UI thread only.
static const WeaverFrame& frameOf(Weaver* module) {
	if (module) {
		uint32_t count = module->publishCount.load(std::memory_order_acquire);
		if (count != module->uiCount) {
			module->uiSnapshot = module->snapshots[module->frontSnapshot.load(std::memory_order_acquire)];
			buildFrame(module->uiFrame, module->uiSnapshot);
			module->uiCount = count;
		}
		return module->uiFrame;
	}
	static WeaverFrame preview;
	static bool built = false;
	if (!built) {
		shapes::CombineEngine e;
		e.process(defaults(), 0.0, false);
		renderFormulas(preview, e, defaults(), 0.0, shapes::Treat::Fit);
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
// operators as dashed lines, with a playhead, the A B C legend (on MetaModule
// followed by the algorithm), the preset the settings match, and the tempo with
// SPEED beside it.
struct WeaverMainDisplay : shapeui::Screen {
	Weaver* module = nullptr;

	WeaverMainDisplay() {
		// The plates' corner radius
		radiusMm = 2.f;
	}

	// At MetaModule's text size the preset's name no longer fits in the
	// header, so it moves to a band below the grid (mm)
#ifdef METAMODULE
	static constexpr float kPresetBand = 4.f;
#else
	static constexpr float kPresetBand = 0.f;
#endif
	static constexpr float kText = shapeui::kTextScale;

	void drawScreen(NVGcontext* vg) override {
		const WeaverFrame& f = frameOf(module);
		float pos = module ? module->displayPos.load(std::memory_order_relaxed) : 0.62f;
		if (!std::isfinite(pos))
			pos = 0.f;

		const float pad = mm2px(2.f);
		const float gx = pad, gy = mm2px(7.f), gw = box.size.x - 2 * pad, gh = box.size.y - mm2px(9.f + kPresetBand);
		shapeui::drawGrid(vg, gx, gy, gw, gh);

		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (font) {
			const float baseline = mm2px(4.6f);
			nvgFontFaceId(vg, font->handle);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
			shapeui::fontSize(vg, mm2px(2.4f * kText));
			for (int i = 0; i < 3; i++) {
				char letter[2] = {(char) ('A' + i), 0};
				// Dimmed while its trace is hidden
				nvgFillColor(vg, shapeui::screenColor(f.traceOn[i] ? shapeui::operatorColor(i) : shapeui::dividerColor()));
				nvgText(vg, gx + mm2px(3.4f * kText) * i, baseline, letter, nullptr);
			}
#ifdef METAMODULE
			// Full screen hides the algorithm display, so name it here too
			nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
			shapeui::fontSize(vg, mm2px(2.2f * kText));
			nvgText(vg, gx + mm2px(10.2f * kText), baseline,
			        shapes::algorithmName((shapes::Algorithm) clamp(f.algorithm, 0, shapes::kNumAlgorithms - 1)), nullptr);
#endif
			// The preset the settings match, or for a while a message in its place
			char text[kMessageChars + 1];
			std::string title;
			if (module && module->message.get(text, system::getTime()))
				title = text;
			else
				title = shapes::screenText(module ? module->presetTitle.get() : shapes::factoryPresetName(0), kMessageChars);
			if (!title.empty()) {
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
				shapeui::fontSize(vg, mm2px(2.6f * kText));
				nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
				nvgText(vg, gx + gw / 2, kPresetBand > 0.f ? box.size.y - mm2px(1.6f) : baseline, title.c_str(), nullptr);
			}
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			shapeui::fontSize(vg, mm2px(2.2f * kText));
			float right = gx + gw;
			// SPEED, away from x1: the chosen one, dim and blinking (as
			// Lyrebird's ARMED) until it takes over
			if (f.speed != 0 || f.speedTarget != 0) {
				const char* label = shapes::CycleSpeed::label(f.speedTarget);
				bool waiting = f.speed != f.speedTarget;
				nvgFillColor(vg, shapeui::screenColor(waiting ? shapeui::dividerColor() : shapeui::inkColor()));
				if (!waiting || std::fmod(system::getTime(), 0.5) < 0.25)
					nvgText(vg, right, baseline, label, nullptr);
				// Share Tech Mono advances 0.54 em; one space between
				right -= mm2px((strlen(label) + 1) * 0.54f * 2.2f * kText);
			}
			char bpm[24];
			snprintf(bpm, sizeof(bpm), "%.2f BPM", f.bpm);
			nvgFillColor(vg, shapeui::screenColor(shapeui::mutedColor()));
			nvgText(vg, right, baseline, bpm, nullptr);
		}

		shapeui::TraceStyle dashed;
		dashed.widthMm = 0.22f;
		dashed.limit = kOperatorLimit;
		dashed.dashMm = 0.9f;
		dashed.gapMm = 0.6f;
		dashed.closed = false;
		for (int i = 0; i < 3; i++) {
			if (!f.traceOn[i] || (!f.shown[i] && module && module->hideZeroLevelTraces))
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
		const WeaverFrame& f = frameOf(module);
		const float* values = f.wave[op];
		int wave = clamp(f.waves[op], 0, shapes::kNumWaves - 1);

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
		const WeaverFrame& f = frameOf(module);
		int wave = f.waves[op];
		// The chosen ratio, dim until it takes over
		int ratio = clamp(f.ratioTargets[op], 0, shapes::kNumRatios - 1);
		bool waiting = wave != shapes::Input && ratio != f.ratios[op];
		const char* text = wave == shapes::Input ? "IN" : shapes::ratioLabel(ratio);
		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (!font)
			return;
		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		shapeui::fontSize(vg, mm2px(3.2f));
		nvgFillColor(vg, shapeui::screenColor(waiting ? shapeui::dividerColor() : shapeui::inkColor()));
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

// DELETE: a small red button
struct DeleteButton : app::SvgSwitch {
	DeleteButton() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/DeleteButton_0.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/DeleteButton_1.svg")));
	}
};

// Clicking WAVE steps to the next wave, and ALGORITHM's buttons to the
// previous or next algorithm; right-clicking lists them all below the usual
// parameter menu.
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

struct AlgorithmButton : ChoiceMenuButton<TL1105> {
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
#ifdef METAMODULE
		// A knob, so the wave's name shows as it turns
		addParam(createParamCentered<RoundBlackSnapKnob>(pos, module, paramId));
#else
		WaveButton* b = createLightParamCentered<WaveButton>(pos, module, paramId, lightId);
		b->op = op;
		addParam(b);
#endif
	}

	WeaverWidget(Weaver* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Weaver.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addScreen<WeaverMainDisplay>(module, 1.5, 17.0, 87.74, 37.7);
		addScreen<AlgorithmDisplay>(module, 3.9, 59.1, 16.38, 13.3);
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

		// Algorithm and presets
		addParam(createParamCentered<AlgorithmButton>(mm2px(Vec(28.96, 63.9)), module, Weaver::ALGORITHM_PREV_PARAM));
		addParam(createParamCentered<AlgorithmButton>(mm2px(Vec(35.56, 63.9)), module, Weaver::ALGORITHM_NEXT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.06, 63.9)), module, Weaver::ALGORITHM_INPUT));
#ifdef METAMODULE
		// Plain buttons: process() steps the presets, as PresetButton never gets events here
		addParam(createParamCentered<TL1105>(mm2px(Vec(73.96, 63.9)), module, Weaver::PRESET_NEXT_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(58.76, 63.9)), module, Weaver::PRESET_PREV_PARAM));
#else
		PresetButton* next = createParamCentered<PresetButton>(mm2px(Vec(73.96, 63.9)), module, Weaver::PRESET_NEXT_PARAM);
		next->delta = 1;
		addParam(next);
		PresetButton* prev = createParamCentered<PresetButton>(mm2px(Vec(58.76, 63.9)), module, Weaver::PRESET_PREV_PARAM);
		prev->delta = -1;
		addParam(prev);
#endif
		// SAVE and DELETE are read in process(), and their files written by the file service
		addParam(createParamCentered<VCVButton>(mm2px(Vec(66.36, 63.9)), module, Weaver::PRESET_SAVE_PARAM));
		addParam(createParamCentered<DeleteButton>(mm2px(Vec(84.24, 63.9)), module, Weaver::PRESET_DELETE_PARAM));

		// Clock row
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.575, 88.0)), module, Weaver::BPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.075, 88.0)), module, Weaver::CLOCK_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(36.415, 88.0)), module, Weaver::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(44.915, 88.0)), module, Weaver::RESET_INPUT));

		// SPEED: plain buttons, read in process() like ALGORITHM's, down and up over their trigger jacks
		addParam(createParamCentered<TL1105>(mm2px(Vec(60.385, 87.65)), module, Weaver::SPEED_DOWN_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(69.085, 87.65)), module, Weaver::SPEED_RESET_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(77.785, 87.65)), module, Weaver::SPEED_UP_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(60.385, 96.35)), module, Weaver::SPEED_DOWN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(77.785, 96.35)), module, Weaver::SPEED_UP_INPUT));

		// Outputs, and each operator's output over its trace button
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(12.93, 112.1)), module, Weaver::MAIN_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(28.375, 112.1)), module, Weaver::INV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.49, 112.1)), module, Weaver::EOC_OUTPUT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<OperatorLight<0>>>>(mm2px(Vec(57.59, 118.68)), module, Weaver::A_TRACE_PARAM, Weaver::TRACE_LIGHT + 0));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<OperatorLight<1>>>>(mm2px(Vec(69.81, 118.68)), module, Weaver::B_TRACE_PARAM, Weaver::TRACE_LIGHT + 1));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<OperatorLight<2>>>>(mm2px(Vec(82.03, 118.68)), module, Weaver::C_TRACE_PARAM, Weaver::TRACE_LIGHT + 2));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(57.59, 110.18)), module, Weaver::A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(69.81, 110.18)), module, Weaver::B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(82.03, 110.18)), module, Weaver::C_OUTPUT));
	}

	// MetaModule runs the file service on its own thread
	void step() override {
		ModuleWidget::step();
#ifndef METAMODULE
		if (Weaver* module = getModule<Weaver>())
			module->serviceFiles();
#endif
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
		menu->addChild(createIndexSubmenuItem("Speed and Ratio change", {"Immediately", "At end of cycle"},
			[=]() { return module->speedRatioAtCycleEnd ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set speed and ratio change timing", [=]() { module->speedRatioAtCycleEnd = i == 1; });
			}));
		menu->addChild(createIndexSubmenuItem("Reset", {"On clock pulse", "Immediately"},
			[=]() { return module->resetOnPulse ? 0 : 1; },
			[=](int i) {
				withMenuUndo(module, "set reset timing", [=]() { module->resetOnPulse = i == 0; });
			}));
		menu->addChild(createBoolMenuItem("New random loops on reset", "",
			[=]() { return module->newLoopOnReset; },
			[=](bool on) {
				withMenuUndo(module, "set new random loops on reset", [=]() { module->newLoopOnReset = on; });
			}));
		menu->addChild(createBoolMenuItem("Hide traces when operator level at 0", "",
			[=]() { return module->hideZeroLevelTraces; },
			[=](bool on) {
				withMenuUndo(module, "set hide traces at level 0", [=]() { module->hideZeroLevelTraces = on; });
			}));
#ifdef METAMODULE
		// No per-param right-click menus here, so list the algorithms in this
		// one (the WAVE knobs name their waves)
		std::vector<std::string> algorithmNames;
		for (int a = 0; a < shapes::kNumAlgorithms; a++)
			algorithmNames.push_back(shapes::algorithmName((shapes::Algorithm) a));
		menu->addChild(createIndexSubmenuItem("Algorithm", algorithmNames,
			[=]() { return module->algorithm; },
			[=](int i) {
				withMenuUndo(module, "set algorithm", [=]() { module->algorithm = i; });
			}));
#endif

		menu->addChild(new MenuSeparator);
#ifdef METAMODULE
		// Save as needs the folder listed first, so the file thread opens its browser
		menu->addChild(createMenuItem("Save preset as...", "", [=]() { module->dialogRequest.store(2); }));
		menu->addChild(createMenuItem("Restore factory presets", "", [=]() { module->restoreRequest.store(true); }));
#else
		menu->addChild(createMenuItem("Save preset as...", "", [=]() { module->saveAs(); }));
		menu->addChild(createMenuItem("Restore factory presets", "", [=]() { module->restoreFactory(); }));
#endif
	}
};

Model* modelWeaver = createModel<Weaver, WeaverWidget>("Weaver");
