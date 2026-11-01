#include "plugin.hpp"
#include "ShapeGenerator.hpp"
#include "shapes/CycleSpeed.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/Recipes.hpp"
#include "shapes/SkylarkEngine.hpp"
#include "shapes/Timebase.hpp"
#include "shapes/TrimMod.hpp"

#include <atomic>
#include <cctype>

// Skylark: a shape modulator. Each cycle (one bar) plays a structured shape
// (a bounce, a ratchet, a fractal line...) that Variation sweeps through its
// family of forms, reshaped by Timing, Lift, Symmetry, Tilt and Drift. SPEED
// runs the cycle at 1/4x to 4x the bar.

static constexpr int kTracePoints = 256;

// What the display shows, drawn up on the UI thread from a snapshot.
struct SkylarkFrame {
	float trace[kTracePoints] = {};  // -1..1 within the output range
	int shape = 0;
	float variation = 0.5f;
	float bpm = 120.f;
	int speed = 0;  // SPEED in octaves, running and chosen
	int speedTarget = 0;
};

// What the audio thread hands the display: the engine is copied each
// publish, so the trace (256 shape evaluations) is worked out on the UI
// thread, not in process().
struct SkylarkSnapshot {
	shapes::SkylarkEngine engine;
	shapes::Treat treatMode = shapes::Treat::Fit;
	shapes::Range range = shapes::Range::Bipolar5;
	float variation = 0.5f;
	float bpm = 120.f;
	int speed = 0;
	int speedTarget = 0;
};

static void buildFrame(SkylarkFrame& f, const SkylarkSnapshot& s) {
	s.engine.renderCycle(f.trace, kTracePoints, s.treatMode, s.range);
	// Volts back to -1..1 for drawing
	for (int i = 0; i < kTracePoints; i++)
		f.trace[i] = (float) shapes::fromVolts(f.trace[i], s.range);
	f.shape = s.engine.activeShape();
	f.variation = s.variation;
	f.bpm = s.bpm;
	f.speed = s.speed;
	f.speedTarget = s.speedTarget;
}

struct Skylark : Module {
	enum ParamId {
		VARIATION_PARAM,
		SHAPE_PARAM,
		SHAPE_PREV_PARAM,
		SHAPE_NEXT_PARAM,
		VARIATION_CV_PARAM,
		TIMING_PARAM,
		LIFT_PARAM,
		SYMMETRY_PARAM,
		TILT_PARAM,
		DRIFT_PARAM,
		TIMING_CV_PARAM,
		LIFT_CV_PARAM,
		SYMMETRY_CV_PARAM,
		TILT_CV_PARAM,
		DRIFT_CV_PARAM,
		BPM_PARAM,
		RESET_PARAM,
		SPEED_DOWN_PARAM,
		SPEED_UP_PARAM,
		SPEED_RESET_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VARIATION_INPUT,
		SHAPE_INPUT,
		TIMING_INPUT,
		LIFT_INPUT,
		SYMMETRY_INPUT,
		TILT_INPUT,
		DRIFT_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		SPEED_DOWN_INPUT,
		SPEED_UP_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MAIN_OUTPUT,
		// INV was removed; the slot keeps EOC's id for old patches
		UNUSED_INV_OUTPUT,
		EOC_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// The six controls with a CV trim. With the jack empty the trim feeds OUT
	// back (clockwise) or adds a random offset each cycle (counter-clockwise).
	// Variation and Drift pick the shape, so their random values jump at the
	// cycle start; the transforms' glide across the cycle. Symmetry and Drift
	// rest at the bottom of their range, so theirs only push upward.
	struct TrimmedControl {
		int param, input, trim;
		float lo, hi, fallback;
		shapes::TrimSpread spread;
		bool glide;
	};
	enum { VARIATION, TIMING, LIFT, SYMMETRY, TILT, DRIFT, NUM_TRIMMED };
	static constexpr TrimmedControl kTrimmed[NUM_TRIMMED] = {
		{VARIATION_PARAM, VARIATION_INPUT, VARIATION_CV_PARAM, 0.f, 1.f, 0.5f, shapes::TrimSpread::Bipolar, false},
		{TIMING_PARAM, TIMING_INPUT, TIMING_CV_PARAM, -1.f, 1.f, -1.f, shapes::TrimSpread::Bipolar, true},
		{LIFT_PARAM, LIFT_INPUT, LIFT_CV_PARAM, -1.f, 1.f, -1.f, shapes::TrimSpread::Bipolar, true},
		{SYMMETRY_PARAM, SYMMETRY_INPUT, SYMMETRY_CV_PARAM, 0.f, 1.f, 0.f, shapes::TrimSpread::Upward, true},
		{TILT_PARAM, TILT_INPUT, TILT_CV_PARAM, -1.f, 1.f, -1.f, shapes::TrimSpread::Bipolar, true},
		{DRIFT_PARAM, DRIFT_INPUT, DRIFT_CV_PARAM, 0.f, 1.f, 0.f, shapes::TrimSpread::Upward, false},
	};

	// Context menu settings
	shapeui::Settings settings;
	// CV and feedback are read once at the start of each cycle
	bool cvPerCycle = false;
	// A new shape waits for the end of the cycle
	bool shapeAtCycleEnd = false;
	int heldShape = 0;

	shapes::Timebase timebase;
	shapes::CycleSpeed speed;
	shapes::SkylarkEngine engine;
	shapes::Slew slew;
	shapes::Trigger eoc;
	dsp::BooleanTrigger prevTrigger, nextTrigger, speedDownTrigger, speedUpTrigger, speedResetTrigger;
	dsp::SchmittTrigger speedDownCv, speedUpCv;
	float sampleRate = 48000.f;
	float appliedSlewMs = -1.f;
	// OUT on the previous sample: the feedback into an empty CV jack
	float lastOut = 0.f;
	float heldCv[NUM_TRIMMED] = {};
	bool started = false;
	// The shape is worked out every kControlInterval samples, and OUT ramps between
	shapes::Ramp ramp;
	bool rampStarted = false;
	int untilEval = 0;
	double evalVariation = 0.5;
	int sincePublish = shapeui::kPublishInterval;

	SkylarkSnapshot snapshots[2];
	std::atomic<int> frontSnapshot{0};
	std::atomic<uint32_t> publishCount{0};
	std::atomic<float> displayPos{0.f};
	// UI thread only: the snapshot being drawn and the frame made from it
	SkylarkSnapshot uiSnapshot;
	SkylarkFrame uiFrame;
	uint32_t uiCount = ~0u;

	Skylark() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(VARIATION_PARAM, 0.f, 1.f, 0.5f, "Variation", "%", 0.f, 100.f);
		std::vector<std::string> names;
		for (int i = 0; i < shapes::kNumRecipes; i++)
			names.push_back(shapes::recipeName(i));
		configSwitch(SHAPE_PARAM, 0.f, shapes::kNumRecipes - 1, 0.f, "Shape", names);
		configButton(SHAPE_PREV_PARAM, "Previous shape");
		configButton(SHAPE_NEXT_PARAM, "Next shape");
		configParam(TIMING_PARAM, -1.f, 1.f, 0.f, "Timing");
		configParam(LIFT_PARAM, -1.f, 1.f, 0.f, "Lift");
		configParam(SYMMETRY_PARAM, 0.f, 1.f, 0.f, "Symmetry", "%", 0.f, 100.f);
		configParam(TILT_PARAM, -1.f, 1.f, 0.f, "Tilt");
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.f, "Drift", "%", 0.f, 100.f);
		static const char* const kTrimmedNames[NUM_TRIMMED] = {"Variation", "Timing", "Lift", "Symmetry", "Tilt", "Drift"};
		for (int i = 0; i < NUM_TRIMMED; i++) {
			ParamQuantity* trim = configParam(kTrimmed[i].trim, -1.f, 1.f, 0.f, string::f("%s CV amount", kTrimmedNames[i]));
			trim->description = "With the jack empty: clockwise feeds OUT back, counter-clockwise adds a random offset each cycle";
			configInput(kTrimmed[i].input, string::f("%s CV", kTrimmedNames[i]));
		}
		shapeui::configBpm(this, BPM_PARAM);
		configButton(RESET_PARAM, "Reset");
		configButton(SPEED_DOWN_PARAM, "Half speed");
		configButton(SPEED_UP_PARAM, "Double speed");
		configButton(SPEED_RESET_PARAM, "Speed x1");
		configInput(SHAPE_INPUT, "Shape CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(SPEED_DOWN_INPUT, "Half speed");
		configInput(SPEED_UP_INPUT, "Double speed");
		configOutput(MAIN_OUTPUT, "Main");
		configOutput(EOC_OUTPUT, "End of cycle");
#ifdef METAMODULE
		// Two build or scan steps an evaluation: doing them all at once was a
		// burst; a new shape lands about 45 ms after a change (Chirp 65)
		engine.setScanSpread(2);
#endif
		setRate(APP->engine->getSampleRate());
	}

	void setRate(float rate) {
		sampleRate = rate;
		timebase.setSampleRate(rate);
		appliedSlewMs = -1.f;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		setRate(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		speed.jumpTo(0);
		settings.reset();
		cvPerCycle = false;
		shapeAtCycleEnd = false;
	}

	float cv(int input) {
		return (float) shapes::sanitize(inputs[input].getVoltage(), 0.0);
	}

	// One of the trimmed controls at t cycles since reset, clamped. A patched
	// CV is attenuverted (10 V spans the knob's range). With the jack empty,
	// clockwise feeds OUT in as if it were patched (Upward controls take OUT's
	// place in its range, bottom to top, instead), and counter-clockwise adds
	// a random offset.
	double control(int i, double t, bool cycleStart) {
		const TrimmedControl& k = kTrimmed[i];
		bool patched = inputs[k.input].isConnected();
		float in = patched ? cv(k.input) : lastOut;
		if (!cvPerCycle || cycleStart)
			heldCv[i] = in;
		double trim = params[k.trim].getValue();
		double span = k.hi - k.lo;
		double v = params[k.param].getValue();
		if (patched || (trim > 0.0 && k.spread == shapes::TrimSpread::Bipolar))
			v += heldCv[i] * 0.2 * trim * span;
		else if (trim > 0.0) {
			double place = std::clamp((shapes::fromVolts(heldCv[i], settings.range) + 1.0) / 2.0, 0.0, 1.0);
			v += place * trim * span;
		}
		else if (trim < 0.0)
			v += -trim * span * shapes::trimRandomOffset((uint32_t) i, t, k.spread, k.glide);
		return std::clamp(shapes::sanitize(v, k.fallback), (double) k.lo, (double) k.hi);
	}

	void stepShape(int delta) {
		int i = (int) std::round(params[SHAPE_PARAM].getValue()) + delta;
		params[SHAPE_PARAM].setValue((float) ((i + shapes::kNumRecipes) % shapes::kNumRecipes));
	}

	void process(const ProcessArgs& args) override {
		shapes::TimebaseInput tin;
		tin.bpm = shapeui::bpmOf(params[BPM_PARAM].getValue());
		tin.clockConnected = inputs[CLOCK_INPUT].isConnected();
		tin.clockVoltage = cv(CLOCK_INPUT);
		tin.resetVoltage = cv(RESET_INPUT);
		tin.resetButton = params[RESET_PARAM].getValue() > 0.5f;
		tin.pulsesPerCycle = settings.pulsesPerCycle;
		shapes::Timebase::Step step = timebase.process(tin);
		// From here on a cycle is SPEED's
		shapes::CycleSpeed::Step sped = speed.process(step.cycle, step.pos, step.reset, step.wrapped);
		// Reset starts a cycle too
		bool cycleStart = sped.wrapped || step.reset || !started;
		started = true;

		// A cycle start is worked out on its own sample, so a shape or
		// Variation held for it lands there
		if (cycleStart || --untilEval <= 0) {
			untilEval = shapes::kControlInterval;
			evaluate(sped.t, cycleStart);
		}

		double volts = slew.process(ramp.next());
		float out = (float) shapes::sanitize(volts, 0.0);
		lastOut = out;
		outputs[MAIN_OUTPUT].setVoltage(out);
		if (sped.wrapped)
			eoc.fire();
		outputs[EOC_OUTPUT].setVoltage(eoc.process(args.sampleTime));

		displayPos.store((float) (sped.t - std::floor(sped.t)), std::memory_order_relaxed);
		if (++sincePublish >= shapeui::kPublishInterval) {
			sincePublish = 0;
			publish(evalVariation);
		}
	}

	// The controls and the shape, at control rate, at t cycles since reset:
	// sets OUT's next ramp
	void evaluate(double t, bool cycleStart) {
		if (prevTrigger.process(params[SHAPE_PREV_PARAM].getValue() > 0.5f))
			stepShape(-1);
		if (nextTrigger.process(params[SHAPE_NEXT_PARAM].getValue() > 0.5f))
			stepShape(1);
		// SPEED's jacks do what their buttons do
		bool down = speedDownTrigger.process(params[SPEED_DOWN_PARAM].getValue() > 0.5f);
		down |= speedDownCv.process(cv(SPEED_DOWN_INPUT), 0.1f, 1.f);
		bool up = speedUpTrigger.process(params[SPEED_UP_PARAM].getValue() > 0.5f);
		up |= speedUpCv.process(cv(SPEED_UP_INPUT), 0.1f, 1.f);
		if (down)
			speed.setTarget(speed.targetOctaves() - 1);
		if (up)
			speed.setTarget(speed.targetOctaves() + 1);
		if (speedResetTrigger.process(params[SPEED_RESET_PARAM].getValue() > 0.5f))
			speed.setTarget(0);
		if (settings.slewMs != appliedSlewMs) {
			slew.set(settings.slewMs, sampleRate);
			appliedSlewMs = settings.slewMs;
		}

		shapes::SkylarkControls c;
		// 10 V of Shape CV spans all the recipes; huge voltages are clamped
		// first so the rounding can't overflow
		double shapeCv = clamp(cv(SHAPE_INPUT), -100.f, 100.f);
		int shape = (int) std::round(params[SHAPE_PARAM].getValue()) + (int) std::round(shapeCv / 10.0 * shapes::kNumRecipes);
		shape = ((shape % shapes::kNumRecipes) + shapes::kNumRecipes) % shapes::kNumRecipes;
		if (!shapeAtCycleEnd || cycleStart)
			heldShape = shape;
		c.shape = heldShape;
		c.variation = control(VARIATION, t, cycleStart);
		c.timing = control(TIMING, t, cycleStart);
		c.lift = control(LIFT, t, cycleStart);
		c.symmetry = control(SYMMETRY, t, cycleStart);
		c.tilt = control(TILT, t, cycleStart);
		c.drift = control(DRIFT, t, cycleStart);
		engine.setControls(c);
		evalVariation = c.variation;

		double cycle = std::floor(t);
		double raw = engine.raw(t - cycle, (int64_t) cycle);
		double y = shapes::treat(raw, settings.treatMode, engine.peak());
		// The peak is rescanned now and then while the transforms apply at
		// every evaluation, so Fit can briefly overshoot: keep it in range
		if (settings.treatMode == shapes::Treat::Fit)
			y = std::clamp(y, -1.0, 1.0);
		double volts = shapes::sanitize(shapes::toVolts(y, settings.range), 0.0);
		if (rampStarted)
			ramp.target(volts, shapes::kControlInterval);
		else {
			ramp.jump(volts);
			rampStarted = true;
		}
	}

	void publish(double variation) {
		int back = 1 - frontSnapshot.load(std::memory_order_relaxed);
		SkylarkSnapshot& s = snapshots[back];
		s.engine = engine;
		s.treatMode = settings.treatMode;
		s.range = settings.range;
		s.variation = (float) variation;
		s.bpm = (float) timebase.tempoBpm();
		s.speed = speed.octaves();
		s.speedTarget = speed.targetOctaves();
		frontSnapshot.store(back, std::memory_order_release);
		publishCount.fetch_add(1, std::memory_order_release);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		settings.toJson(rootJ);
		json_object_set_new(rootJ, "cvPerCycle", json_boolean(cvPerCycle));
		json_object_set_new(rootJ, "shapeAtCycleEnd", json_boolean(shapeAtCycleEnd));
		json_object_set_new(rootJ, "speed", json_integer(speed.targetOctaves()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		settings.fromJson(rootJ);
		// Before all six controls shared it, the option was Variation's alone
		json_t* j = json_object_get(rootJ, "cvPerCycle");
		if (!j)
			j = json_object_get(rootJ, "variationPerCycle");
		cvPerCycle = j && json_is_true(j);
		j = json_object_get(rootJ, "shapeAtCycleEnd");
		shapeAtCycleEnd = j && json_is_true(j);
		j = json_object_get(rootJ, "speed");
		speed.jumpTo(j && json_is_integer(j) ? (int) json_integer_value(j) : 0);
	}
};

// One cycle of the shaped output (after range and treatment, before slew),
// with a playhead and a header naming the shape, Variation and tempo, with
// SPEED beside it.
struct SkylarkDisplay : shapeui::Screen {
	Skylark* module = nullptr;
	// The module browser's picture: Bounce at default settings, built once
	shapes::SkylarkEngine previewEngine;
	SkylarkFrame preview;
	bool havePreview = false;

	SkylarkDisplay() {
		// The plates' corner radius
		radiusMm = 2.f;
	}

	// The module's latest frame, drawn up once per publish. UI thread only.
	const SkylarkFrame& frame() {
		if (module) {
			uint32_t count = module->publishCount.load(std::memory_order_acquire);
			if (count != module->uiCount) {
				module->uiSnapshot = module->snapshots[module->frontSnapshot.load(std::memory_order_acquire)];
				// A publish since then may have been writing the slot copied (a
				// shape from one build and tables from another can crash the
				// render): keep the last frame and try again next time
				std::atomic_thread_fence(std::memory_order_acquire);
				if (module->publishCount.load(std::memory_order_relaxed) == count) {
					buildFrame(module->uiFrame, module->uiSnapshot);
					module->uiCount = count;
				}
			}
			return module->uiFrame;
		}
		if (!havePreview) {
			previewEngine.raw(0.0, 0);
			previewEngine.renderCycle(preview.trace, kTracePoints, shapes::Treat::Fit, shapes::Range::Bipolar5);
			for (float& v : preview.trace)
				v *= 0.2f;
			havePreview = true;
		}
		return preview;
	}

	// At MetaModule's text size the shape's name doesn't fit beside the
	// settings, so it moves to a band below the grid (mm)
#ifdef METAMODULE
	static constexpr float kNameBand = 4.f;
#else
	static constexpr float kNameBand = 0.f;
#endif
	static constexpr float kText = shapeui::kTextScale;

	void drawScreen(NVGcontext* vg) override {
		const SkylarkFrame& f = frame();
		float pos = module ? module->displayPos.load(std::memory_order_relaxed) : 0.62f;

		const float pad = mm2px(2.f);
		const float gx = pad, gy = mm2px(7.f), gw = box.size.x - 2 * pad, gh = box.size.y - mm2px(9.f + kNameBand);
		shapeui::drawGrid(vg, gx, gy, gw, gh);

		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (font) {
			char title[40];
			const char* name = shapes::recipeName(f.shape);
			size_t n = 0;
			for (; name[n] && n < sizeof(title) - 1; n++)
				title[n] = (char) std::toupper((unsigned char) name[n]);
			title[n] = 0;
			char bpm[24], var[16];
			snprintf(bpm, sizeof(bpm), "%.2f BPM", f.bpm);
			snprintf(var, sizeof(var), "VAR %d%%", (int) std::round(f.variation * 100.f));

			nvgFontFaceId(vg, font->handle);
			shapeui::fontSize(vg, mm2px(2.6f * kText));
			nvgFillColor(vg, shapeui::screenColor(shapeui::inkColor()));
			if (kNameBand > 0.f) {
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
				nvgText(vg, gx + gw / 2, box.size.y - mm2px(1.6f), title, nullptr);
			}
			else {
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
				nvgText(vg, gx, mm2px(4.6f), title, nullptr);
			}
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			shapeui::fontSize(vg, mm2px(2.2f * kText));
			float end = gx + gw;
			// SPEED, away from x1: the chosen one, dim and blinking (as
			// Lyrebird's ARMED) until it takes over
			if (f.speed != 0 || f.speedTarget != 0) {
				const char* label = shapes::CycleSpeed::label(f.speedTarget);
				bool waiting = f.speed != f.speedTarget;
#ifdef METAMODULE
				NVGcolor active = shapeui::playTextColor();
#else
				NVGcolor active = shapeui::inkColor();
#endif
				nvgFillColor(vg, shapeui::screenColor(waiting ? shapeui::dividerColor() : active));
				if (!waiting || std::fmod(system::getTime(), 0.5) < 0.25)
					nvgText(vg, end, mm2px(4.6f), label, nullptr);
				// Share Tech Mono advances 0.54 em; one space between
				end -= mm2px((strlen(label) + 1) * 0.54f * 2.2f * kText);
			}
			nvgFillColor(vg, shapeui::screenColor(shapeui::timeTextColor()));
			nvgText(vg, end, mm2px(4.6f), bpm, nullptr);
			// Two spaces before the tempo
			end -= mm2px((strlen(bpm) + 2) * 0.54f * 2.2f * kText);
			nvgFillColor(vg, shapeui::screenColor(shapeui::infoTextColor()));
			nvgText(vg, end, mm2px(4.6f), var, nullptr);
		}

		shapeui::TraceStyle style;
		style.color = shapeui::signalColor();
		// Open with a lead-in, so a jump at the loop point shows at the start,
		// where it's heard
		style.closed = false;
		style.leadIn = true;
		shapeui::drawTrace(vg, f.trace, kTracePoints, gx, gy, gw, gh, style);
		if (!std::isfinite(pos))
			pos = 0.f;
		shapeui::drawPlayhead(vg, gx, gy, gw, gh, pos, shapeui::traceAt(f.trace, kTracePoints, pos, false));
	}
};

struct SkylarkWidget : ModuleWidget {
	SkylarkWidget(Skylark* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Skylark.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		SkylarkDisplay* display = createWidget<SkylarkDisplay>(mm2px(Vec(1.5, 17.0)));
		display->box.size = mm2px(Vec(73.43, 44.5));
		display->module = module;
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(88.56, 32.791)), module, Skylark::VARIATION_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(103.71, 32.791)), module, Skylark::SHAPE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(88.56, 44.753)), module, Skylark::VARIATION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(103.71, 44.753)), module, Skylark::SHAPE_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(88.56, 53.844)), module, Skylark::VARIATION_CV_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(100.41, 53.844)), module, Skylark::SHAPE_PREV_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(107.01, 53.844)), module, Skylark::SHAPE_NEXT_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.884, 76.868)), module, Skylark::TIMING_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(35.652, 76.868)), module, Skylark::LIFT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(58.42, 76.868)), module, Skylark::SYMMETRY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(81.188, 76.868)), module, Skylark::TILT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(103.956, 76.868)), module, Skylark::DRIFT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.884, 85.779)), module, Skylark::TIMING_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(35.652, 85.779)), module, Skylark::LIFT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(58.42, 85.779)), module, Skylark::SYMMETRY_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(81.188, 85.779)), module, Skylark::TILT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(103.956, 85.779)), module, Skylark::DRIFT_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.884, 94.312)), module, Skylark::TIMING_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(35.652, 94.312)), module, Skylark::LIFT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58.42, 94.312)), module, Skylark::SYMMETRY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(81.188, 94.312)), module, Skylark::TILT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(103.956, 94.312)), module, Skylark::DRIFT_INPUT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.261, 110.6)), module, Skylark::BPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.761, 110.6)), module, Skylark::CLOCK_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(31.788, 110.6)), module, Skylark::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.288, 110.6)), module, Skylark::RESET_INPUT));
		// SPEED: plain buttons, read in process() like the shape buttons, over their trigger jacks
		addParam(createParamCentered<TL1105>(mm2px(Vec(54.814, 107.692)), module, Skylark::SPEED_DOWN_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(63.514, 107.692)), module, Skylark::SPEED_RESET_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(72.214, 107.692)), module, Skylark::SPEED_UP_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(53.514, 115.892)), module, Skylark::SPEED_DOWN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(73.514, 115.892)), module, Skylark::SPEED_UP_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(92.702, 110.6)), module, Skylark::EOC_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(106.029, 110.6)), module, Skylark::MAIN_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Skylark* module = getModule<Skylark>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		shapeui::appendSettingsMenu(menu, module, &module->settings);
		menu->addChild(createIndexSubmenuItem("CV and feedback", {"Continuous", "Sample once per cycle"},
			[=]() { return module->cvPerCycle ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set CV and feedback mode", [=]() { module->cvPerCycle = i == 1; });
			}));
		menu->addChild(createIndexSubmenuItem("Shape changes", {"Immediately", "At end of cycle"},
			[=]() { return module->shapeAtCycleEnd ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set shape change timing", [=]() { module->shapeAtCycleEnd = i == 1; });
			}));
	}
};

Model* modelSkylark = createModel<Skylark, SkylarkWidget>("Skylark");
