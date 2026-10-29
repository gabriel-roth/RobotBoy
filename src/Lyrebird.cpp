#include "plugin.hpp"
#include "ShapeGenerator.hpp"
#include "shapes/LyrebirdEngine.hpp"
#include "shapes/LyrebirdPaths.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/ScreenMessage.hpp"
#include "shapes/TakeFile.hpp"
#include "shapes/Timebase.hpp"

// On MetaModule only for the WAV filter; its file browser is the firmware's
#include <osdialog.h>
#ifdef METAMODULE
#include "filesystem/async_filebrowser.hh"
#include "filesystem/dirent.h"
#include "threads/async_thread.hh"
#endif

#include <atomic>
#include <cstdlib>

// Lyrebird: a shape modulator that records a knob gesture or a CV and plays
// it back in time with the clock, looped or one-shot, with punch-in.

static constexpr int kTracePoints = 256;
// Samples between display updates.
static constexpr float kMessageSeconds = 3.f;
// Footer characters a message can use before it runs into the source
static constexpr size_t kMessageChars = 18;
// How long the footer shows the LENGTH setting after it's turned
static constexpr double kLengthShowSeconds = 2.0;

// Take generations, unique across every Lyrebird, so a history step can't
// match a different (or deleted and restored) module's takes
static std::atomic<uint32_t> takeGenCounter{0};

#ifdef METAMODULE
static constexpr bool kOnMetaModule = true;
#else
static constexpr bool kOnMetaModule = false;
#endif

#ifdef METAMODULE
// MetaModule's opendir() drops a path's volume ("sdc:/x" opens "x" on
// whichever drive FatFS used last), so a card that isn't there can look as
// if it is. Repeating the volume gets it through to FatFS. A firmware that
// keeps the volume would reject the repeated form, so that form is used only
// once a repeated volume root has opened.
static std::atomic<bool> volumeRepeatWorks{false};

static DIR* openFolder(const std::string& path) {
	if (!volumeRepeatWorks.load()) {
		for (const char* root : {"sdc:/", "usb:/"}) {
			if (DIR* d = opendir(shapes::withVolumeRepeated(root).c_str())) {
				closedir(d);
				volumeRepeatWorks.store(true);
				break;
			}
		}
	}
	return opendir((volumeRepeatWorks.load() ? shapes::withVolumeRepeated(path) : path).c_str());
}
#endif

// Whether a folder exists. MetaModule's stat() calls everything a file (and
// fails on volume roots), so system::isDirectory is always false there:
// open it as a folder instead.
static bool isFolder(const std::string& path) {
#ifdef METAMODULE
	DIR* d = openFolder(path);
	if (d)
		closedir(d);
	return d != nullptr;
#else
	return system::isDirectory(path);
#endif
}

// The screen's brighter version of the SOURCE plate's magenta
static NVGcolor recColor() {
	return nvgRGB(0xff, 0x5f, 0x95);
}

// "1/4 BAR", "1 BAR", "4 BARS"
static std::string barsText(double bars) {
	if (bars < 1.0 - 1e-9) {
		int d = (int) std::lround(1.0 / bars);
		return string::f("1/%d BAR", d);
	}
	int b = (int) std::lround(bars);
	return string::f("%d %s", b, b == 1 ? "BAR" : "BARS");
}

// x2, x1/4, or x1.41 for a speed between powers of two
static std::string ratioText(double r) {
	if (!(r > 0.0) || !std::isfinite(r))
		return "x1";
	if (r >= 1.0 && std::fabs(r - std::round(r)) < 1e-6)
		return string::f("x%d", (int) std::lround(r));
	if (r < 1.0 && std::fabs(1.0 / r - std::round(1.0 / r)) < 1e-6)
		return string::f("x1/%d", (int) std::lround(1.0 / r));
	return string::f("x%.2f", r);
}

// What the display shows, written by the audio thread.
struct LyrebirdFrame {
	float raw[kTracePoints] = {};  // volts, as stored
	float out[kTracePoints] = {};  // volts, as OUT plays it
	float playhead = 0.f;
	// The span being written, from..to in the direction of play
	bool span = false;
	float spanFrom = 0.f, spanTo = 0.f;
	shapes::Status status = shapes::Status::Idle;
	bool empty = true;
	bool sync = true;  // how the take plays (the Time menu)
	bool loop = true;
	bool reverse = false;
	bool cv = false;  // the source in use
	bool unipolar = false;  // knob range 0-10 V
	float bars = 1.f;  // as played in Sync
	int lengthIndex = 2;  // the LENGTH knob
	float seconds = 0.f;
	float bpm = 120.f;
	float ratio = 1.f;
	float smoothMs = 0.f;
	char name[64] = "";
	bool modified = false;
};

struct Lyrebird : Module, MenuUndoFilter {
	enum ParamId {
		GESTURE_PARAM,
		RECORD_PARAM,
		PUNCH_PARAM,
		LENGTH_PARAM,
		SPEED_PARAM,
		SMOOTH_PARAM,
		REVERSE_PARAM,
		BPM_PARAM,
		TRIG_PARAM,
		FILE_PREV_PARAM,
		SAVE_PARAM,
		FILE_NEXT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		RECORD_INPUT,
		PUNCH_INPUT,
		CV_INPUT,
		SPEED_INPUT,
		SMOOTH_INPUT,
		REVERSE_INPUT,
		CLOCK_INPUT,
		TRIG_INPUT,
		FILE_PREV_INPUT,
		FILE_NEXT_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		EOC_OUTPUT,
		MAIN_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RECORD_LIGHT,
		PUNCH_LIGHT,
		REVERSE_LIGHT,
		LIGHTS_LEN
	};

	enum class KnobRange { Bipolar5, Unipolar10 };

	// Context menu settings, saved with the patch
	KnobRange knobRange = KnobRange::Bipolar5;
	shapes::RecordButtons recordButtons = shapes::RecordButtons::Toggle;
	shapes::RecordButtons reverseButton = shapes::RecordButtons::Hold;
	shapes::StartOn startOn = shapes::StartOn::Press;
	shapes::IdleOutput idleOutput = shapes::IdleOutput::Last;
	int pulsesPerCycle = 4;
	bool sync = true;  // Time: Sync / Free
	bool loop = true;  // Play: Loop / Once
	bool reversed = false;  // REVERSE's latched state when its button toggles
	// The folder of the last file opened or saved; empty until one is
	// chosen, which means the default folder. The file service writes it
	// and the UI reads it, on different threads on MetaModule.
	shapes::SharedPath<1023> folder;

	shapes::Timebase timebase;
	shapes::LyrebirdEngine engine;
	shapes::Trigger eoc;
	dsp::SchmittTrigger recGate, punchGate, reverseGate, prevGate, nextGate;
	dsp::BooleanTrigger prevButton, saveButton, nextButton, reversePress;
	float blinkPhase = 0.f;
	int sincePublish = shapeui::kPublishInterval;
	shapes::Status lastStatus = shapes::Status::Idle;
	float spanFrom = 0.f;

	LyrebirdFrame frames[2];
	std::atomic<int> frontFrame{0};
	std::atomic<float> displayPos{0.f};

	// Audio thread to UI: a Free take reached the limit; REC or PUNCH started writing
	std::atomic<bool> limitHit{false};
	std::atomic<uint32_t> writesStarted{0};
	// Recording or punching now, for the UI thread
	std::atomic<bool> writingNow{false};
	// Audio thread to the file service: file button presses, since files
	// are the service's to touch
	std::atomic<int> fileStepRequest{0}, saveRequest{0};

	// Names the current pair of takes (current and undo slot), so a history
	// step only swaps them when they are still the pair it left, and a save
	// only names the take it saved. Written by the file service and the UI.
	std::atomic<uint32_t> takeGen{newGen()};
	static uint32_t newGen() {
		return ++takeGenCounter;
	}

	// Footer message: the file service writes it, the display reads it
	shapes::ScreenMessage<kMessageChars> message;

	// A save waiting for its snapshot, file service only. An empty path means
	// the next free name in saveDir, picked once the snapshot is ready.
	bool saving = false;
	std::string savePath, saveDir;
	uint32_t saveGen = 0;
	// File service only: the writes started that it has seen
	uint32_t seenStarts = 0;

	// A patch take for the file service to load: one that arrived while a
	// load was still pending, or on MetaModule any patch take
	std::atomic<json_t*> deferredTakeJ{nullptr};

#ifdef METAMODULE
	// File service only: the finished takes it has seen
	uint32_t seenRecs = 0, seenPunches = 0;
	// Menu to file service: open the Save as browser (2). Open... opens its
	// browser from the menu, since that browser touches the GUI at once.
	std::atomic<int> dialogRequest{0};
	// File service to menu: the default folder it last found, so the menu
	// can start the Open browser there without touching the card itself
	shapes::SharedPath<255> lastDefault;
	bool defaultLooked = false;
	// File browser (GUI) to file service: the path chosen, in a single slot.
	// 0 empty, 1 being written, 2 ready.
	std::atomic<int> dialogState{0};
	std::string dialogPath;
	bool dialogSave = false;
	// Runs the file service whether or not the module is on screen
	MetaModule::AsyncThread fileThread{this, [this]() { serviceFiles(); }};
#endif

	struct GestureQuantity : ParamQuantity {
		bool unipolar() {
			Lyrebird* m = dynamic_cast<Lyrebird*>(module);
			return m && m->knobRange == KnobRange::Unipolar10;
		}
		float getDisplayValue() override {
			float v = getValue();
			return unipolar() ? v * 10.f : (v - 0.5f) * 10.f;
		}
		void setDisplayValue(float d) override {
			setValue(unipolar() ? d / 10.f : d / 10.f + 0.5f);
		}
	};

	// Shown as the ratio it plays at: snapped to powers of two in Sync
	struct SpeedQuantity : ParamQuantity {
		bool sync() {
			Lyrebird* m = dynamic_cast<Lyrebird*>(module);
			return !m || m->sync;
		}
		std::string getDisplayValueString() override {
			double oct = getValue();
			double r = sync() ? shapes::LyrebirdEngine::syncRatio(oct) : shapes::LyrebirdEngine::freeRatio(oct);
			return ratioText(r);
		}
		void setDisplayValueString(std::string s) override {
			size_t i = 0;
			while (i < s.size() && (s[i] == 'x' || s[i] == 'X' || s[i] == ' '))
				i++;
			s = s.substr(i);
			double r;
			size_t slash = s.find('/');
			if (slash != std::string::npos) {
				double a = std::atof(s.substr(0, slash).c_str());
				double b = std::atof(s.substr(slash + 1).c_str());
				r = b != 0.0 ? a / b : 0.0;
			}
			else
				r = std::atof(s.c_str());
			if (r > 0.0 && std::isfinite(r))
				setValue((float) std::log2(r));
		}
	};

	// Full window width in ms: 1000 x v^3, fine at the short end
	struct SmoothQuantity : ParamQuantity {
		float getDisplayValue() override {
			float v = getValue();
			return 1000.f * v * v * v;
		}
		void setDisplayValue(float d) override {
			setValue(std::cbrt(std::max(d, 0.f) / 1000.f));
		}
	};

	Lyrebird() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<GestureQuantity>(GESTURE_PARAM, 0.f, 1.f, 0.5f, "Gesture", " V");
		configButton(RECORD_PARAM, "Record");
		configButton(PUNCH_PARAM, "Punch in");
		configSwitch(LENGTH_PARAM, 0.f, 6.f, 2.f, "Length",
			{"1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"});
		configParam<SpeedQuantity>(SPEED_PARAM, -2.f, 2.f, 0.f, "Speed");
		configParam<SmoothQuantity>(SMOOTH_PARAM, 0.f, 1.f, 0.f, "Smooth", " ms");
		configButton(REVERSE_PARAM, "Reverse");
		shapeui::configBpm(this, BPM_PARAM);
		configButton(TRIG_PARAM, "Trigger");
		configButton(FILE_PREV_PARAM, "Previous file");
		configButton(SAVE_PARAM, "Save");
		configButton(FILE_NEXT_PARAM, "Next file");
		configInput(RECORD_INPUT, "Record gate");
		configInput(PUNCH_INPUT, "Punch gate");
		configInput(CV_INPUT, "CV (recorded instead of Gesture when connected)");
		configInput(SPEED_INPUT, "Speed CV (1 V/oct)");
		configInput(SMOOTH_INPUT, "Smooth CV");
		configInput(REVERSE_INPUT, "Reverse gate");
		configInput(CLOCK_INPUT, "Clock");
		configInput(TRIG_INPUT, "Trigger");
		configInput(FILE_PREV_INPUT, "Previous file trigger");
		configInput(FILE_NEXT_INPUT, "Next file trigger");
		configOutput(EOC_OUTPUT, "End of cycle");
		configOutput(MAIN_OUTPUT, "Main");
		configLight(RECORD_LIGHT, "Recording");
		configLight(PUNCH_LIGHT, "Punching");
		configLight(REVERSE_LIGHT, "Reversed");
		setRate(APP->engine->getSampleRate());
#ifdef METAMODULE
		seenRecs = engine.recsFinished();
		seenPunches = engine.punchesFinished();
		fileThread.start();
#endif
	}

	~Lyrebird() {
#ifdef METAMODULE
		fileThread.stop();
#endif
		dropDeferredTake();
	}

	void setRate(float rate) {
		timebase.setSampleRate(rate);
		engine.setSampleRate(rate);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		setRate(e.sampleRate);
	}

	// Controls and settings go back to their defaults; the take is content, so it stays
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		knobRange = KnobRange::Bipolar5;
		recordButtons = shapes::RecordButtons::Toggle;
		reverseButton = shapes::RecordButtons::Hold;
		reversed = false;
		startOn = shapes::StartOn::Press;
		idleOutput = shapes::IdleOutput::Last;
		pulsesPerCycle = 4;
		sync = true;
		loop = true;
	}

	float cv(int input) {
		return (float) shapes::sanitize(inputs[input].getVoltage(), 0.0);
	}

	bool gate(dsp::SchmittTrigger& t, int input) {
		t.process(cv(input), 0.1f, 1.f);
		return t.isHigh();
	}

	double knobVolts() {
		float v = params[GESTURE_PARAM].getValue();
		return knobRange == KnobRange::Unipolar10 ? v * 10.0 : (v - 0.5) * 10.0;
	}

	// Menu undo snapshots leave the take out: undoing a settings change
	// restores the settings, never an older take
	void filterMenuUndoJson(json_t* moduleJ) override {
		json_t* dataJ = json_object_get(moduleJ, "data");
		if (!dataJ)
			return;
		json_object_del(dataJ, "take");
		json_object_set_new(dataJ, "keepTake", json_true());
	}

	void process(const ProcessArgs& args) override {
		// A cable in CV IN records it instead of GESTURE
		bool useCv = inputs[CV_INPUT].isConnected();

		shapes::TimebaseInput tin;
		tin.bpm = shapeui::bpmOf(params[BPM_PARAM].getValue());
		tin.clockConnected = inputs[CLOCK_INPUT].isConnected();
		tin.clockVoltage = cv(CLOCK_INPUT);
		tin.resetVoltage = cv(TRIG_INPUT);
		tin.resetButton = params[TRIG_PARAM].getValue() > 0.5f;
		tin.pulsesPerCycle = pulsesPerCycle;
		shapes::Timebase::Step tb = timebase.process(tin);

		shapes::LyrebirdControls c;
		c.source = useCv ? clamp(cv(CV_INPUT), -10.f, 10.f) : knobVolts();
		c.bpm = timebase.tempoBpm();
		c.sourceIsKnob = !useCv;
		c.sync = sync;
		c.loop = loop;
		c.lengthIndex = clamp((int) std::round(params[LENGTH_PARAM].getValue()), 0, 6);
		c.speedOct = params[SPEED_PARAM].getValue() + cv(SPEED_INPUT);
		float smooth = clamp(params[SMOOTH_PARAM].getValue() + cv(SMOOTH_INPUT) / 10.f, 0.f, 1.f);
		c.smoothMs = 1000.0 * smooth * smooth * smooth;
		// Like RECORD in Toggle: each press or rising gate flips it
		bool reverseLevel = params[REVERSE_PARAM].getValue() > 0.5f || gate(reverseGate, REVERSE_INPUT);
		if (reversePress.process(reverseLevel) && reverseButton == shapes::RecordButtons::Toggle)
			reversed = !reversed;
		c.reverse = reverseButton == shapes::RecordButtons::Toggle ? reversed : reverseLevel;
		c.rec = params[RECORD_PARAM].getValue() > 0.5f || gate(recGate, RECORD_INPUT);
		c.punch = params[PUNCH_PARAM].getValue() > 0.5f || gate(punchGate, PUNCH_INPUT);
		c.buttons = recordButtons;
		c.startOn = startOn;
		c.idle = idleOutput;

		shapes::LyrebirdStep step = engine.process(c, tb);
		if (step.restartTransport)
			timebase.reset();

		// Each jack does what the button above it does
		bool prevTrig = prevGate.process(cv(FILE_PREV_INPUT), 0.1f, 1.f);
		bool nextTrig = nextGate.process(cv(FILE_NEXT_INPUT), 0.1f, 1.f);
		if (prevButton.process(params[FILE_PREV_PARAM].getValue() > 0.5f) || prevTrig)
			fileStepRequest.fetch_sub(1);
		if (nextButton.process(params[FILE_NEXT_PARAM].getValue() > 0.5f) || nextTrig)
			fileStepRequest.fetch_add(1);
		if (saveButton.process(params[SAVE_PARAM].getValue() > 0.5f))
			saveRequest.fetch_add(1);

		outputs[MAIN_OUTPUT].setVoltage((float) shapes::sanitize(step.volts, 0.0));
		if (step.eoc)
			eoc.fire();
		outputs[EOC_OUTPUT].setVoltage(eoc.process(args.sampleTime));

		using shapes::Status;
		if (step.hitLimit)
			limitHit.store(true);
		bool writing = step.status == Status::Recording || step.status == Status::Punching;
		// Before the start count, so the UI never sees a start without it
		writingNow.store(writing);
		// REC can take over straight from a punch, which is a new write too
		if (writing && step.status != lastStatus) {
			spanFrom = (float) step.playhead;
			writesStarted.fetch_add(1);
		}
		lastStatus = step.status;

		// Solid while writing, blinking at 2 Hz while armed
		blinkPhase += args.sampleTime * 2.f;
		if (blinkPhase >= 1.f)
			blinkPhase -= 1.f;
		float blink = blinkPhase < 0.5f ? 1.f : 0.f;
		lights[RECORD_LIGHT].setBrightness(step.status == Status::Recording ? 1.f : step.status == Status::Armed ? blink : 0.f);
		lights[REVERSE_LIGHT].setBrightness(c.reverse ? 1.f : 0.f);
		lights[PUNCH_LIGHT].setBrightness(step.status == Status::Punching ? 1.f : step.status == Status::PunchArmed ? blink : 0.f);

		displayPos.store((float) step.playhead, std::memory_order_relaxed);
		if (++sincePublish >= shapeui::kPublishInterval) {
			sincePublish = 0;
			publish(c, step, useCv);
		}
	}

	void publish(const shapes::LyrebirdControls& c, const shapes::LyrebirdStep& step, bool useCv) {
		int back = 1 - frontFrame.load(std::memory_order_relaxed);
		LyrebirdFrame& f = frames[back];
		engine.renderTrace(c, f.raw, f.out, kTracePoints);
		const shapes::Take& t = engine.take();
		f.playhead = (float) step.playhead;
		f.status = step.status;
		f.span = step.status == shapes::Status::Recording || step.status == shapes::Status::Punching;
		if (step.status == shapes::Status::Recording && t.mode == shapes::TakeMode::Free) {
			// A Free take grows as it's written: all of it is new
			f.spanFrom = 0.f;
			f.spanTo = 1.f;
		}
		else {
			f.spanFrom = spanFrom;
			f.spanTo = (float) step.playhead;
		}
		f.empty = t.empty();
		f.sync = c.sync;
		f.loop = c.loop;
		f.reverse = c.reverse;
		f.cv = useCv;
		f.unipolar = knobRange == KnobRange::Unipolar10;
		f.bars = (float) shapes::LyrebirdEngine::effectiveBars(t);
		f.lengthIndex = c.lengthIndex;
		f.seconds = (float) t.seconds;
		f.bpm = (float) timebase.tempoBpm();
		f.ratio = (float) engine.speedRatio(c);
		f.smoothMs = (float) c.smoothMs;
		memcpy(f.name, t.name, sizeof f.name);
		f.name[sizeof f.name - 1] = 0;
		f.modified = t.modified;
		frontFrame.store(back, std::memory_order_release);
	}

	void showMessage(const std::string& text) {
		message.set(text, system::getTime() + kMessageSeconds);
	}

	// MetaModule can't create folders: the first of these that exists, looked
	// up each time so a card put in later is found. "" when there's none.
	static std::string defaultFolder() {
#ifdef METAMODULE
		return shapes::pickFolder(shapes::kMetaModuleFolders, isFolder);
#else
		return asset::user("RobotBoy/Lyrebird");
#endif
	}
#ifdef METAMODULE
	// File service only: the saved folder if it's still there (the card may
	// have been swapped), else the default; "" when there's neither
	std::string fileFolder() {
		std::string f = folder.get();
		if (!f.empty() && isFolder(f))
			return f;
		std::string d = defaultFolder();
		lastDefault.set(d);
		defaultLooked = true;
		return d;
	}
	// GUI context, no file access: where the Open browser starts ("" lets
	// the browser pick)
	std::string browseFolder() const {
		std::string f = folder.get();
		return f.empty() ? lastDefault.get() : f;
	}
#else
	std::string currentFolder() const {
		std::string f = folder.get();
		return f.empty() ? defaultFolder() : f;
	}
#endif

	void serviceFiles();
#ifdef METAMODULE
	// GUI context: the file browser's choice, for the file service
	void postDialogPath(const char* path, bool save) {
		int expected = 0;
		if (!dialogState.compare_exchange_strong(expected, 1)) {
			showMessage("BUSY");
			return;
		}
		dialogPath = path;
		dialogSave = save;
		dialogState.store(2, std::memory_order_release);
	}
#endif

	// The current take, for the UI to name. The engine owns it as non-const;
	// its name and modified flag are read tear-tolerantly by the audio thread.
	shapes::Take& currentTake() {
		return const_cast<shapes::Take&>(engine.take());
	}

	json_t* takeToJson(const shapes::Take& t) {
		json_t* takeJ = json_object();
		json_object_set_new(takeJ, "mode", json_string(t.mode == shapes::TakeMode::Free ? "free" : "sync"));
		json_object_set_new(takeJ, "bars", json_real(t.bars));
		json_object_set_new(takeJ, "seconds", json_real(t.seconds));
		json_object_set_new(takeJ, "bpm", json_real(t.bpm));
		json_object_set_new(takeJ, "name", json_string(t.name));
		json_object_set_new(takeJ, "modified", json_boolean(t.modified));
		json_object_set_new(takeJ, "length", json_integer(t.length));
		json_object_set_new(takeJ, "data", json_string(string::toBase64(shapes::packSamples16(t)).c_str()));
		return takeJ;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "knobRange", json_integer((int) knobRange));
		json_object_set_new(rootJ, "recordButtons", json_integer((int) recordButtons));
		json_object_set_new(rootJ, "reverseButton", json_integer((int) reverseButton));
		json_object_set_new(rootJ, "reversed", json_boolean(reversed));
		json_object_set_new(rootJ, "startOn", json_integer((int) startOn));
		json_object_set_new(rootJ, "idleOutput", json_integer((int) idleOutput));
		json_object_set_new(rootJ, "pulsesPerCycle", json_integer(pulsesPerCycle));
		json_object_set_new(rootJ, "sync", json_boolean(sync));
		json_object_set_new(rootJ, "loop", json_boolean(loop));
		json_object_set_new(rootJ, "folder", json_string(folder.get().c_str()));
		// Read while the audio thread may be writing it: at worst a sample or
		// two of a take being recorded is saved half-updated
		const shapes::Take& t = engine.take();
		if (!t.empty())
			json_object_set_new(rootJ, "take", takeToJson(t));
		return rootJ;
	}

	static int intOf(json_t* rootJ, const char* key, int lo, int hi, int fallback) {
		json_t* j = json_object_get(rootJ, key);
		if (!j || !json_is_integer(j))
			return fallback;
		int v = (int) json_integer_value(j);
		return v >= lo && v <= hi ? v : fallback;
	}

	static double numberOf(json_t* rootJ, const char* key, double fallback) {
		json_t* j = json_object_get(rootJ, key);
		if (!j || !json_is_number(j))
			return fallback;
		double v = json_number_value(j);
		return std::isfinite(v) ? v : fallback;
	}

	void dataFromJson(json_t* rootJ) override {
		knobRange = (KnobRange) intOf(rootJ, "knobRange", 0, 1, 0);
		recordButtons = (shapes::RecordButtons) intOf(rootJ, "recordButtons", 0, 1, 0);
		reverseButton = (shapes::RecordButtons) intOf(rootJ, "reverseButton", 0, 1, 1);
		startOn = (shapes::StartOn) intOf(rootJ, "startOn", 0, 1, 0);
		idleOutput = (shapes::IdleOutput) intOf(rootJ, "idleOutput", 0, 2, 0);
		json_t* j = json_object_get(rootJ, "pulsesPerCycle");
		pulsesPerCycle = shapeui::validPulses(j ? (int) json_integer_value(j) : 4);
		j = json_object_get(rootJ, "sync");
		sync = !j || !json_is_false(j);
		j = json_object_get(rootJ, "loop");
		loop = !j || !json_is_false(j);
		j = json_object_get(rootJ, "reversed");
		reversed = j && json_is_true(j);
		j = json_object_get(rootJ, "folder");
		// The other platform's folder means nothing here: the default instead.
		// If the file thread reads it mid-set, it gets "" (the default folder) for that pass.
		folder.set(shapes::usableFolder(j && json_is_string(j) ? json_string_value(j) : "", kOnMetaModule));
		// A menu undo step: settings only
		j = json_object_get(rootJ, "keepTake");
		if (j && json_is_true(j))
			return;

		json_t* takeJ = json_object_get(rootJ, "take");
#ifdef METAMODULE
		// The file thread is the only one to fill spare(): it loads the take
		deferTake(takeJ);
#else
		dropDeferredTake();
		if (engine.loadPending()) {
			// The engine hasn't taken the last load yet: try again once it has
			deferTake(takeJ);
			return;
		}
		loadTakeJson(takeJ);
#endif
	}

	// Hands a copy of takeJ (nullptr: no take) to the file service, replacing
	// any it hasn't loaded yet. An empty object stands in for "no take".
	void deferTake(json_t* takeJ) {
		if (json_t* oldJ = deferredTakeJ.exchange(takeJ ? json_deep_copy(takeJ) : json_object()))
			json_decref(oldJ);
	}
	void dropDeferredTake() {
		if (json_t* oldJ = deferredTakeJ.exchange(nullptr))
			json_decref(oldJ);
	}

	// Parses a patch take into spare(); false when it's missing or unusable
	bool parseTake(json_t* takeJ, shapes::Take& t) {
		t.clear();
		if (!takeJ || !json_is_object(takeJ))
			return false;
		json_t* dataJ = json_object_get(takeJ, "data");
		if (!dataJ || !json_is_string(dataJ))
			return false;
		std::vector<uint8_t> bytes;
		try {
			bytes = string::fromBase64(json_string_value(dataJ));
		}
		catch (std::exception&) {
			return false;
		}
		int length = intOf(takeJ, "length", 1, shapes::kMaxTakeSamples, -1);
		if (length < 0 || bytes.size() != 2 * (size_t) length)
			return false;
		if (!shapes::unpackSamples16(bytes.data(), bytes.size(), t))
			return false;
		json_t* modeJ = json_object_get(takeJ, "mode");
		std::string mode = modeJ && json_is_string(modeJ) ? json_string_value(modeJ) : "";
		if (mode != "sync" && mode != "free") {
			t.clear();
			return false;
		}
		t.mode = mode == "free" ? shapes::TakeMode::Free : shapes::TakeMode::Sync;
		t.bpm = std::clamp(numberOf(takeJ, "bpm", 120.0), 1.0, 1000.0);
		if (t.mode == shapes::TakeMode::Sync) {
			double bars = numberOf(takeJ, "bars", 1.0);
			t.bars = 1.0;
			for (double b : shapes::kLengthBars)
				if (std::fabs(bars - b) < 1e-9)
					t.bars = b;
			t.seconds = t.bars * 240.0 / t.bpm;
		}
		else {
			double s = numberOf(takeJ, "seconds", 0.0);
			t.seconds = s > 0.0 ? s : (double) t.length / shapes::kTakeRate;
			t.bars = shapes::LyrebirdEngine::effectiveBars(t);
		}
		json_t* nameJ = json_object_get(takeJ, "name");
		const char* name = nameJ && json_is_string(nameJ) ? json_string_value(nameJ) : "";
		snprintf(t.name, sizeof t.name, "%s", name);
		json_t* modJ = json_object_get(takeJ, "modified");
		t.modified = modJ && json_is_true(modJ);
		return true;
	}

	// True when the current take already is what this JSON holds, as it is when
	// an undo step restores the module's JSON around a menu change
	bool sameAsCurrent(json_t* takeJ) {
		const shapes::Take& cur = engine.take();
		if (!takeJ)
			return cur.empty();
		if (cur.empty())
			return false;
		json_t* curJ = takeToJson(cur);
		bool same = json_equal(curJ, takeJ);
		json_decref(curJ);
		return same;
	}

	// File service (UI thread on VCV), with no load pending. Patch loads
	// aren't undoable takes.
	void loadTakeJson(json_t* takeJ) {
		if (sameAsCurrent(takeJ))
			return;
		shapes::Take& t = engine.spare();
		// A bad or missing take loads as an empty one
		parseTake(takeJ, t);
		engine.requestLoad();
		// A new pair of takes: history steps from before it no longer apply
		takeGen = newGen();
	}

	// File service: a patch take that had to wait for the engine
	void applyDeferredTake() {
		if (!deferredTakeJ.load() || engine.loadPending())
			return;
		json_t* takeJ = deferredTakeJ.exchange(nullptr);
		if (!takeJ)
			return;
		// An empty object stood in for "no take"
		loadTakeJson(json_object_size(takeJ) ? takeJ : nullptr);
		json_decref(takeJ);
	}
};

#ifndef METAMODULE
// Swaps the take with the one in the undo slot; doing it again swaps back. It
// only acts when the takes are the pair it expects: the engine keeps one
// level of undo, so steps further back in the history can't be replayed.
struct TakeSwapAction : history::ModuleAction {
	uint32_t after = 0;  // the pair once the action is done
	uint32_t undone = 0;  // the pair once it's undone
	void swapFrom(uint32_t from, uint32_t to) {
		Lyrebird* m = dynamic_cast<Lyrebird*>(APP->engine->getModule(moduleId));
		if (!m || m->takeGen != from)
			return;
		m->engine.requestUndo();
		m->takeGen = to;
	}
	void undo() override {
		swapFrom(after, undone);
	}
	void redo() override {
		swapFrom(undone, after);
	}
};

// Call once the pair has changed; undone names the pair an undo swaps back to
static void pushTakeSwap(Lyrebird* module, const char* name, uint32_t undone) {
	TakeSwapAction* h = new TakeSwapAction;
	h->name = name;
	h->moduleId = module->id;
	h->undone = undone;
	h->after = module->takeGen = module->newGen();
	APP->history->push(h);
}
#endif

// File work, all in the file service (on VCV, the UI thread)

// The file names in dir, or none when it can't be read
static std::vector<std::string> fileNames(const std::string& dir) {
	std::vector<std::string> names;
	try {
#ifdef METAMODULE
		// Listed through openFolder, which reaches the right drive
		if (DIR* d = openFolder(dir)) {
			while (dirent* e = readdir(d)) {
				if (!e->d_name[0])
					break;
				names.push_back(e->d_name);
			}
			closedir(d);
		}
#else
		for (const std::string& path : system::getEntries(dir))
			names.push_back(system::getFilename(path));
#endif
	}
	catch (std::exception&) {
	}
	return names;
}

#ifdef METAMODULE
// Opens the firmware's file browser; the path chosen reaches the file service
// through postDialogPath
static void fileDialog(Lyrebird* m, osdialog_file_action action, const std::string& dir, const char* name) {
	osdialog_filters* filters = osdialog_filters_parse("WAV:wav,WAV");
	bool save = action == OSDIALOG_SAVE;
	async_osdialog_file(action, dir.c_str(), name, filters, [m, save](char* path) {
		if (!path)
			return;
		m->postDialogPath(path, save);
		std::free(path);
	});
	osdialog_filters_free(filters);
}
#else
// The path chosen in a file dialog, or "" when it's cancelled
static std::string fileDialog(osdialog_file_action action, const std::string& dir, const char* name) {
	osdialog_filters* filters = osdialog_filters_parse("WAV:wav,WAV");
	char* path = osdialog_file(action, dir.c_str(), name, filters);
	osdialog_filters_free(filters);
	if (!path)
		return "";
	std::string s = path;
	std::free(path);
	return s;
}

static bool ensureDirectory(const std::string& dir) {
	try {
		if (!system::isDirectory(dir))
			system::createDirectories(dir);
		return system::isDirectory(dir);
	}
	catch (std::exception&) {
		return false;
	}
}
#endif

// Asks for a copy of the take; finishSave writes it once the engine has made it
static void startSave(Lyrebird* m, const std::string& path, const std::string& dir) {
	if (m->saving)
		return;
	if (m->engine.take().empty()) {
		m->showMessage("NOTHING TO SAVE");
		return;
	}
	m->saving = true;
	m->savePath = path;
	m->saveDir = dir;
	m->saveGen = m->takeGen;
	m->engine.requestSnapshot();
}

// Saves to a path chosen in the Save as dialog
static void saveTo(Lyrebird* m, std::string path) {
	if (string::lowercase(system::getExtension(path)) != ".wav")
		path += ".wav";
	startSave(m, path, system::getDirectory(path));
}

static void saveAs(Lyrebird* m) {
	if (m->saving)
		return;
#ifdef METAMODULE
	std::string dir = m->fileFolder();
#else
	std::string dir = m->currentFolder();
#endif
	std::string name = shapes::nextTakeName(fileNames(dir));
#ifdef METAMODULE
	fileDialog(m, OSDIALOG_SAVE, dir, name.c_str());
#else
	std::string path = fileDialog(OSDIALOG_SAVE, dir, name.c_str());
	if (!path.empty())
		saveTo(m, path);
#endif
}

// SAVE: no dialog, the next free name in the current folder
static void quickSave(Lyrebird* m) {
	if (m->saving)
		return;
#ifdef METAMODULE
	// Nothing to save to (no card, say), and no making a folder here
	std::string dir = m->fileFolder();
	bool usable = !dir.empty();
#else
	std::string dir = m->currentFolder();
	bool usable = ensureDirectory(dir);
#endif
	if (usable)
		startSave(m, "", dir);
	else
		saveAs(m);
}

static void finishSave(Lyrebird* m) {
	if (!m->saving || !m->engine.snapshotReady())
		return;
	m->saving = false;
	const shapes::Take& snap = m->engine.snapshot();
	std::string path = m->savePath.empty() ? system::join(m->saveDir, shapes::nextTakeName(fileNames(m->saveDir)))
	                                       : m->savePath;
	bool ok = shapes::writeTakeWav(path, snap);
	uint32_t version = snap.version;
	m->engine.releaseSnapshot();
	if (!ok) {
		m->showMessage("SAVE FAILED");
		return;
	}
	std::string stem = system::getStem(path);
	if (!m->savePath.empty())
		m->folder.set(shapes::usableFolder(system::getDirectory(path), kOnMetaModule));
	// Named only if it's still the take that was saved
	shapes::Take& t = m->currentTake();
	if (m->takeGen == m->saveGen && t.version == version) {
		snprintf(t.name, sizeof t.name, "%s", stem.c_str());
		t.modified = false;
	}
	m->showMessage("SAVED " + stem);
}

// Loads a file as the current take, as an undoable step
static void loadFile(Lyrebird* m, const std::string& path) {
	if (m->engine.loadPending()) {
		m->showMessage("BUSY");
		return;
	}
	shapes::LoadResult r;
	try {
		r = shapes::readTakeWav(path, m->engine.spare());
	}
	catch (std::exception&) {
		// Out of memory, say: the spare take isn't in use, so nothing is lost
		r.error = shapes::LoadError::CantOpen;
	}
	switch (r.error) {
		case shapes::LoadError::CantOpen: m->showMessage("CANT OPEN"); return;
		case shapes::LoadError::NotWav: m->showMessage("NOT A WAV"); return;
		case shapes::LoadError::Unsupported: m->showMessage("UNSUPPORTED"); return;
		case shapes::LoadError::Empty: m->showMessage("EMPTY FILE"); return;
		case shapes::LoadError::None: break;
	}
	m->engine.requestLoad();
#ifdef METAMODULE
	m->takeGen = m->newGen();
#else
	pushTakeSwap(m, "load take", m->newGen());
#endif
	m->folder.set(shapes::usableFolder(system::getDirectory(path), kOnMetaModule));
	m->showMessage(r.truncated ? "TRUNCATED TO 3 MIN" : "LOADED " + system::getStem(path));
}

#ifndef METAMODULE
static void openFile(Lyrebird* m) {
	std::string path = fileDialog(OSDIALOG_OPEN, m->currentFolder(), nullptr);
	if (!path.empty())
		loadFile(m, path);
}
#endif

// Previous / next .wav in the current folder, by name, wrapping
static void stepFile(Lyrebird* m, int delta) {
#ifdef METAMODULE
	std::string dir = m->fileFolder();
	if (dir.empty()) {
#else
	std::string dir = m->currentFolder();
	if (dir.empty() || !isFolder(dir)) {
#endif
#ifdef METAMODULE
		// A trigger mustn't pop up a browser, maybe with the module off screen
		m->showMessage("NO FOLDER");
#else
		openFile(m);
#endif
		return;
	}
	std::vector<std::string> names = shapes::sortedWavNames(fileNames(dir));
	if (names.empty()) {
		m->showMessage("NO FILES");
		return;
	}
	// The current take's file, whatever the case of its name on disk
	std::string current = string::lowercase(std::string(m->engine.take().name) + ".wav");
	for (const std::string& n : names) {
		if (string::lowercase(n) == current) {
			current = n;
			break;
		}
	}
	int i = shapes::stepName(names, current, delta);
	if (i >= 0)
		loadFile(m, system::join(dir, names[i]));
}

// All the file work: VCV's widget calls it each frame, after its history
// bookkeeping; MetaModule's file thread calls it whether or not the module
// is on screen
void Lyrebird::serviceFiles() {
	applyDeferredTake();
#ifdef METAMODULE
	// No history here, but each finished take is a new pair all the same
	for (uint32_t n = engine.punchesFinished(); seenPunches != n; seenPunches++)
		takeGen = newGen();
	for (uint32_t n = engine.recsFinished(); seenRecs != n; seenRecs++)
		takeGen = newGen();
#endif
	// A write in progress is a new pair already: the last finished take's
	// step mustn't swap under it. Checked after the finished takes, since
	// REC taking over a punch finishes one and starts the other at once;
	// a write that started and finished since the last check is covered
	// by its own step.
	uint32_t starts = writesStarted.load();
	if (starts != seenStarts) {
		seenStarts = starts;
		if (writingNow.load()) {
			takeGen = newGen();
			// A save finishing just before may have marked this take
			// unmodified as the write began
			currentTake().modified = true;
		}
	}
	if (limitHit.exchange(false))
		showMessage("LIMIT 3 MIN");

	finishSave(this);
#ifdef METAMODULE
	if (dialogState.load(std::memory_order_acquire) == 2) {
		std::string path = dialogPath;
		bool save = dialogSave;
		dialogState.store(0, std::memory_order_release);
		if (save)
			saveTo(this, path);
		else
			loadFile(this, path);
	}
	if (dialogRequest.exchange(0) == 2)
		saveAs(this);
	// Once, so the Open browser has somewhere to start
	if (!defaultLooked) {
		lastDefault.set(defaultFolder());
		defaultLooked = true;
	}
#endif
	if (saveRequest.exchange(0) > 0)
		quickSave(this);
	// Presses on both cancel out
	int delta = fileStepRequest.exchange(0);
	if (delta != 0)
		stepFile(this, delta);
}

// Draws the finite runs of values[i0..i1] (of n spanning the box); NaN marks
// what isn't recorded yet.
static void drawRuns(NVGcontext* vg, const float* values, int n, int i0, int i1, float x, float y, float w, float h,
                     const shapeui::TraceStyle& style) {
	int i = i0;
	while (i <= i1) {
		while (i <= i1 && !std::isfinite(values[i]))
			i++;
		int a = i;
		while (i <= i1 && std::isfinite(values[i]))
			i++;
		int b = i - 1;
		if (b > a)
			shapeui::drawTrace(vg, values + a, b - a + 1, x + w * a / (n - 1), y, w * (b - a) / (n - 1), h, style);
	}
}

// The trace at pos (0..1) as traceAt reads it, but skipping NaN points: next to
// the write head the next point isn't recorded yet, so this falls back to the
// nearest recorded one at or before pos, then after it
static float finiteAt(const float* values, int n, float pos) {
	float k = clamp(std::isfinite(pos) ? pos : 0.f, 0.f, 1.f) * (n - 1);
	int i0 = std::min((int) k, n - 2);
	float a = values[i0], b = values[i0 + 1];
	if (std::isfinite(a) && std::isfinite(b))
		return a + (b - a) * (k - i0);
	if (std::isfinite(a))
		return a;
	if (std::isfinite(b) && k - i0 >= 0.5f)
		return b;
	for (int i = i0; i >= 0; i--)
		if (std::isfinite(values[i]))
			return values[i];
	for (int i = i0 + 1; i < n; i++)
		if (std::isfinite(values[i]))
			return values[i];
	return 0.f;
}

// The whole take, raw (dim) and as played (white), with bar lines in Sync,
// the span being written in magenta, and a header and footer of its state.
struct LyrebirdDisplay : shapeui::Screen {
	Lyrebird* module = nullptr;
	LyrebirdFrame preview;
	bool havePreview = false;
	// UI thread: the LENGTH knob as last drawn, to show it for a while once turned
	int lastLengthIndex = -1;
	double lengthShownUntil = 0.0;

	LyrebirdDisplay() {
		// The plates' corner radius
		radiusMm = 2.f;
	}

	// The module browser's picture: a slow rise, a wobble and a dip over 4 bars
	const LyrebirdFrame& previewFrame() {
		if (!havePreview) {
			for (int i = 0; i < kTracePoints; i++) {
				double p = (double) i / (kTracePoints - 1);
				double y = -0.55 + 1.2 * (1 - std::cos(M_PI * std::min(p / 0.45, 1.0))) / 2;
				y += 0.18 * std::sin(2 * M_PI * 7 * p) * std::exp(-std::pow((p - 0.55) / 0.12, 2));
				y -= 0.9 * std::exp(-std::pow((p - 0.8) / 0.06, 2));
				y += 0.04 * std::sin(97 * p) + 0.03 * std::sin(211 * p + 1);
				preview.raw[i] = (float) (5.0 * y);
			}
			for (int i = 0; i < kTracePoints; i++) {
				float sum = 0.f;
				int n = 0;
				for (int k = -4; k <= 4; k++, n++)
					sum += preview.raw[clamp(i + k, 0, kTracePoints - 1)];
				preview.out[i] = sum / n;
			}
			preview.empty = false;
			preview.bars = 4.f;
			preview.seconds = 8.f;
			preview.modified = true;
			preview.smoothMs = 40.f;
			preview.playhead = 0.62f;
			havePreview = true;
		}
		return preview;
	}

	void drawScreen(NVGcontext* vg) override {
		// Copied so a publish mid-draw can't tear the trace
		LyrebirdFrame f = module ? module->frames[module->frontFrame.load(std::memory_order_acquire)] : previewFrame();
		float pos = module ? module->displayPos.load(std::memory_order_relaxed) : f.playhead;
		if (!std::isfinite(pos))
			pos = 0.f;
		double now = system::getTime();
		bool blinkOn = std::fmod(now, 0.5) < 0.25;

		const float pad = mm2px(2.f);
		const float gx = pad, gy = mm2px(7.f), gw = box.size.x - 2 * pad, gh = box.size.y - mm2px(13.f);
		shapeui::drawGrid(vg, gx, gy, gw, gh);

		// Bar lines, where a take played in Sync has more than one bar
		if (!f.empty && f.sync && f.bars > 1.5f && f.bars <= 16.5f) {
			int bars = (int) std::lround(f.bars);
			for (int b = 1; b < bars; b++) {
				float bx = gx + gw * b / bars;
				nvgBeginPath(vg);
				nvgMoveTo(vg, bx, gy);
				nvgLineTo(vg, bx, gy + gh);
				nvgStrokeWidth(vg, mm2px(0.12f));
				nvgStrokeColor(vg, shapeui::screenColor(shapeui::gridColor()));
				nvgStroke(vg);
			}
		}

		// Vertical scale: the knob range, widened to whatever the take reaches
		float lo = f.unipolar ? 0.f : -5.f, hi = f.unipolar ? 10.f : 5.f;
		if (!f.empty) {
			for (int i = 0; i < kTracePoints; i++) {
				for (float v : {f.raw[i], f.out[i]}) {
					if (std::isfinite(v)) {
						lo = std::min(lo, v);
						hi = std::max(hi, v);
					}
				}
			}
		}
		float mid = 0.5f * (lo + hi), half = std::max(0.5f * (hi - lo), 0.01f);
		float raw[kTracePoints], out[kTracePoints];
		for (int i = 0; i < kTracePoints; i++) {
			raw[i] = (f.raw[i] - mid) / half;
			out[i] = (f.out[i] - mid) / half;
		}

		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (font)
			drawText(vg, font, f, gx, gw, blinkOn, now);

		bool span = module && f.span;
		if (!f.empty) {
			// The span being written, as up to two pieces where it wraps
			float pieces[2][2];
			int nPieces = 0;
			if (span) {
				float a = clamp(f.spanFrom, 0.f, 1.f), b = clamp(f.spanTo, 0.f, 1.f);
				if (f.reverse && f.status == shapes::Status::Punching)
					std::swap(a, b);
				if (b >= a) {
					pieces[nPieces][0] = a;
					pieces[nPieces++][1] = b;
				}
				else {
					pieces[nPieces][0] = a;
					pieces[nPieces++][1] = 1.f;
					pieces[nPieces][0] = 0.f;
					pieces[nPieces++][1] = b;
				}
				for (int k = 0; k < nPieces; k++) {
					nvgBeginPath(vg);
					nvgRect(vg, gx + gw * pieces[k][0], gy, gw * (pieces[k][1] - pieces[k][0]), gh);
					nvgFillColor(vg, shapeui::screenColor(nvgTransRGBAf(recColor(), 0.12f)));
					nvgFill(vg);
				}
			}

			shapeui::TraceStyle dim;
			dim.color = nvgTransRGBAf(shapeui::mutedColor(), 0.45f);
			dim.widthMm = 0.2f;
			dim.closed = false;
			drawRuns(vg, raw, kTracePoints, 0, kTracePoints - 1, gx, gy, gw, gh, dim);
			shapeui::TraceStyle main;
			main.color = shapeui::signalColor();
			main.closed = false;
			drawRuns(vg, out, kTracePoints, 0, kTracePoints - 1, gx, gy, gw, gh, main);

			shapeui::TraceStyle rec = main;
			rec.color = recColor();
			for (int k = 0; k < nPieces; k++) {
				int i0 = clamp((int) std::floor(pieces[k][0] * (kTracePoints - 1)), 0, kTracePoints - 1);
				int i1 = clamp((int) std::ceil(pieces[k][1] * (kTracePoints - 1)), 0, kTracePoints - 1);
				drawRuns(vg, raw, kTracePoints, i0, i1, gx, gy, gw, gh, rec);
			}
		}
		else {
			// No take: OUT passes the source through, drawn as a level
			shapeui::TraceStyle main;
			main.color = shapeui::signalColor();
			main.closed = false;
			shapeui::drawTrace(vg, out, kTracePoints, gx, gy, gw, gh, main);
		}

		float value = finiteAt(span ? raw : out, kTracePoints, pos);
		shapeui::drawPlayhead(vg, gx, gy, gw, gh, pos, value);
		if (span) {
			// The dot turns magenta while writing
			nvgBeginPath(vg);
			nvgCircle(vg, gx + gw * clamp(pos, 0.f, 1.f), gy + gh * 0.5f - clamp(value, -1.f, 1.f) * gh * 0.5f, mm2px(0.7f));
			nvgFillColor(vg, shapeui::screenColor(recColor()));
			nvgFill(vg);
		}
	}

	void drawText(NVGcontext* vg, std::shared_ptr<window::Font> font, const LyrebirdFrame& f, float gx, float gw,
	              bool blinkOn, double now) {
		std::string title;
		if (f.empty)
			title = "EMPTY";
		else {
			title = shapes::screenText(f.name, 18);
			if (title.empty())
				title = "NEW TAKE";
			if (f.modified)
				title += "*";
		}
		std::string smooth = f.smoothMs < 0.05f ? "OFF"
		                     : f.smoothMs < 10.f ? string::f("%.1fms", f.smoothMs)
		                                         : string::f("%dms", (int) std::lround(f.smoothMs));
		std::string play = string::f("%s  %s  SMOOTH %s", f.loop ? "LOOP" : "ONCE", ratioText(f.ratio).c_str(), smooth.c_str());

		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		shapeui::fontSize(vg, mm2px(2.6f));
		nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
		nvgText(vg, gx, mm2px(4.6f), title.c_str(), nullptr);
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		shapeui::fontSize(vg, mm2px(2.2f));
		nvgFillColor(vg, shapeui::screenColor(shapeui::mutedColor()));
		nvgText(vg, gx + gw, mm2px(4.6f), play.c_str(), nullptr);

		// Footer: status or a message, the source, and the take's length and tempo
		float fy = box.size.y - mm2px(2.f);
		using shapes::Status;
		std::string status;
		NVGcolor statusColor = recColor();
		if (f.status == Status::Recording)
			status = "REC";
		else if (f.status == Status::Punching)
			status = "PUNCH";
		else if (f.status == Status::Armed || f.status == Status::PunchArmed)
			status = blinkOn ? "ARMED" : "";
		else if (module) {
			char text[kMessageChars + 1];
			if (module->message.get(text, now)) {
				status = text;
				statusColor = shapeui::inkColor();
			}
		}
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const float footerPx = mm2px(2.2f);
		shapeui::fontSize(vg, footerPx);
		if (!status.empty()) {
			nvgFillColor(vg, shapeui::screenColor(statusColor));
			nvgText(vg, gx, fy, status.c_str(), nullptr);
		}
		// Messages are longer than a status, so the source moves along for them
		float sourceX = gx + mm2px(9.5f);
		if (!status.empty())
			sourceX = std::max(sourceX, gx + shapeui::textWidth(footerPx, status.size()) + mm2px(1.5f));
		nvgFillColor(vg, shapeui::screenColor(shapeui::mutedColor()));
		nvgText(vg, sourceX, fy, f.cv ? "CV" : "KNOB", nullptr);

		// LENGTH shows only here: the setting with no take or just after it's
		// turned, otherwise the take's length as it plays
		int lengthIndex = clamp(f.lengthIndex, 0, 6);
		if (lengthIndex != lastLengthIndex) {
			if (lastLengthIndex >= 0)
				lengthShownUntil = now + kLengthShowSeconds;
			lastLengthIndex = lengthIndex;
		}
		std::string length;
		if (f.sync) {
			bool setting = f.empty || now < lengthShownUntil;
			length = barsText(setting ? shapes::kLengthBars[lengthIndex] : f.bars) + string::f("  %.2f BPM", f.bpm);
		}
		else
			length = f.empty ? "FREE" : string::f("%.1f S", f.seconds);
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		nvgText(vg, gx + gw, fy, length.c_str(), nullptr);
	}
};

// RECORD and PUNCH light in the display's record colour
struct RecordLight : GrayModuleLightWidget {
	RecordLight() {
		addBaseColor(recColor());
	}
};

struct LyrebirdWidget : ModuleWidget {
#ifndef METAMODULE
	uint32_t seenRecs = 0, seenPunches = 0;
#endif

	LyrebirdWidget(Lyrebird* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lyrebird.svg")));
#ifndef METAMODULE
		if (module) {
			seenRecs = module->engine.recsFinished();
			seenPunches = module->engine.punchesFinished();
		}
#endif

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		LyrebirdDisplay* display = createWidget<LyrebirdDisplay>(mm2px(Vec(1.5, 17.0)));
		display->box.size = mm2px(Vec(62.88, 47.16));
		display->module = module;
		addChild(display);

		// PERFORM plate
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.917, 77.66)), module, Lyrebird::BPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.417, 77.66)), module, Lyrebird::CLOCK_INPUT));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(46.107, 77.66)), module, Lyrebird::LENGTH_PARAM));
		addParam(createLightParamCentered<VCVLightBezel<RecordLight>>(mm2px(Vec(15.317, 94.15)), module, Lyrebird::RECORD_PARAM, Lyrebird::RECORD_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.817, 94.15)), module, Lyrebird::RECORD_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<RecordLight>>(mm2px(Vec(41.649, 94.15)), module, Lyrebird::PUNCH_PARAM, Lyrebird::PUNCH_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(50.149, 94.15)), module, Lyrebird::PUNCH_INPUT));

		// SOURCE plate
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(19.775, 110.038)), module, Lyrebird::GESTURE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.107, 110.038)), module, Lyrebird::CV_INPUT));

		// PLAYBACK plate
		addParam(createParamCentered<TL1105>(mm2px(Vec(77.08, 28.275)), module, Lyrebird::FILE_PREV_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(85.78, 28.275)), module, Lyrebird::SAVE_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(94.48, 28.275)), module, Lyrebird::FILE_NEXT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(77.08, 38.275)), module, Lyrebird::FILE_PREV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(94.48, 38.275)), module, Lyrebird::FILE_NEXT_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(77.08, 55.315)), module, Lyrebird::SPEED_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(94.48, 55.315)), module, Lyrebird::SMOOTH_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(77.08, 67.315)), module, Lyrebird::SPEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(94.48, 67.315)), module, Lyrebird::SMOOTH_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(77.08, 82.605)), module, Lyrebird::TRIG_PARAM));
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(94.48, 82.605)), module, Lyrebird::REVERSE_PARAM, Lyrebird::REVERSE_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(77.08, 92.605)), module, Lyrebird::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(94.48, 92.605)), module, Lyrebird::REVERSE_INPUT));

		// Output plate
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(78.18, 111.105)), module, Lyrebird::EOC_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(92.045, 111.105)), module, Lyrebird::MAIN_OUTPUT));
	}

	// MetaModule has no history, and runs the file service on its own thread
	void step() override {
		ModuleWidget::step();
#ifndef METAMODULE
		Lyrebird* module = getModule<Lyrebird>();
		if (!module)
			return;
		// Each finished take is an undo step, so Ctrl+Z brings back the one
		// before. Undoing it leaves a pair that never existed before, so it gets
		// a new name too. The service then checks for a write in progress.
		for (uint32_t n = module->engine.punchesFinished(); seenPunches != n; seenPunches++)
			pushTakeSwap(module, "punch in", module->newGen());
		for (uint32_t n = module->engine.recsFinished(); seenRecs != n; seenRecs++)
			pushTakeSwap(module, "record take", module->newGen());
		module->serviceFiles();
#endif
	}

	void appendContextMenu(Menu* menu) override {
		Lyrebird* module = getModule<Lyrebird>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Time", {"Sync", "Free"},
			[=]() { return module->sync ? 0 : 1; },
			[=](int i) {
				withMenuUndo(module, "set time", [=]() { module->sync = i == 0; });
			}));
		menu->addChild(createIndexSubmenuItem("Play", {"Loop", "Once"},
			[=]() { return module->loop ? 0 : 1; },
			[=](int i) {
				withMenuUndo(module, "set play", [=]() { module->loop = i == 0; });
			}));
		menu->addChild(createIndexSubmenuItem("Knob range", {"+-5 V", "0-10 V"},
			[=]() { return (int) module->knobRange; },
			[=](int i) {
				withMenuUndo(module, "set knob range", [=]() { module->knobRange = (Lyrebird::KnobRange) i; });
			}));
		menu->addChild(createIndexSubmenuItem("Record buttons", {"Toggle", "Hold"},
			[=]() { return (int) module->recordButtons; },
			[=](int i) {
				withMenuUndo(module, "set record buttons", [=]() { module->recordButtons = (shapes::RecordButtons) i; });
			}));
		menu->addChild(createIndexSubmenuItem("Reverse button", {"Toggle", "Hold"},
			[=]() { return (int) module->reverseButton; },
			[=](int i) {
				// Switching starts forward, rather than in a state left over from Toggle
				if (i == (int) module->reverseButton)
					return;
				withMenuUndo(module, "set reverse button", [=]() {
					module->reverseButton = (shapes::RecordButtons) i;
					module->reversed = false;
				});
			}));
		menu->addChild(createIndexSubmenuItem("Start recording", {"On press", "On first movement"},
			[=]() { return (int) module->startOn; },
			[=](int i) {
				withMenuUndo(module, "set recording start", [=]() { module->startOn = (shapes::StartOn) i; });
			}));
		menu->addChild(createIndexSubmenuItem("Idle output", {"Last value", "First value", "0 V"},
			[=]() { return (int) module->idleOutput; },
			[=](int i) {
				withMenuUndo(module, "set idle output", [=]() { module->idleOutput = (shapes::IdleOutput) i; });
			}));
		menu->addChild(shapeui::createPulsesMenuItem(module, &module->pulsesPerCycle));

		menu->addChild(new MenuSeparator);
#ifdef METAMODULE
		// The browser touches the GUI as it opens, so Open... opens it here;
		// the path chosen goes to the file thread, which does the file work.
		// Save as needs the folder listed first, so the file thread opens it.
		menu->addChild(createMenuItem("Open...", "", [=]() {
			fileDialog(module, OSDIALOG_OPEN, module->browseFolder(), nullptr);
		}));
		menu->addChild(createMenuItem("Save as...", "", [=]() { module->dialogRequest.store(2); }));
		menu->addChild(createMenuItem("Undo last take", "", [=]() {
			module->engine.requestUndo();
			// A new pair, so a save in flight doesn't name the take swapped in
			module->takeGen = module->newGen();
		}));
#else
		menu->addChild(createMenuItem("Open...", "", [=]() { openFile(module); }));
		menu->addChild(createMenuItem("Save as...", "", [=]() { saveAs(module); }));
		menu->addChild(createMenuItem("Undo last take", "", [=]() {
			// Undoing this swaps straight back to the pair before it
			uint32_t before = module->takeGen;
			module->engine.requestUndo();
			pushTakeSwap(module, "undo last take", before);
		}));
#endif
	}
};

Model* modelLyrebird = createModel<Lyrebird, LyrebirdWidget>("Lyrebird");
