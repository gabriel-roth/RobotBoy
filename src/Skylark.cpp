#include "plugin.hpp"
#include "ShapeGenerator.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/Recipes.hpp"
#include "shapes/SkylarkEngine.hpp"
#include "shapes/Timebase.hpp"

#include <atomic>
#include <cctype>

// Skylark: a shape modulator. Each cycle (one bar) plays a structured shape
// (a bounce, a ratchet, a fractal line...) that Variation sweeps through its
// family of forms, reshaped by Timing, Lift, Symmetry, Tilt and Drift.

static constexpr int kTracePoints = 256;
// Samples between display updates.
static constexpr int kPublishInterval = 512;

// What the display shows, written by the audio thread.
struct SkylarkFrame {
	float trace[kTracePoints] = {};  // -1..1 within the output range
	int shape = 0;
	float variation = 0.5f;
	float bpm = 120.f;
};

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
		INPUTS_LEN
	};
	enum OutputId {
		MAIN_OUTPUT,
		INV_OUTPUT,
		EOC_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// Context menu settings
	shapeui::Settings settings;
	bool variationPerCycle = false;
	// A new shape waits for the end of the cycle
	bool shapeAtCycleEnd = false;
	int heldShape = 0;

	shapes::Timebase timebase;
	shapes::SkylarkEngine engine;
	shapes::Slew slew;
	shapes::Trigger eoc;
	dsp::BooleanTrigger prevTrigger, nextTrigger;
	float sampleRate = 48000.f;
	float appliedSlewMs = -1.f;
	// OUT on the previous sample: Variation's feedback when its jack is empty
	float lastOut = 0.f;
	float heldVariationCv = 0.f;
	bool started = false;
	int sincePublish = kPublishInterval;

	SkylarkFrame frames[2];
	std::atomic<int> frontFrame{0};
	std::atomic<float> displayPos{0.f};

	Skylark() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(VARIATION_PARAM, 0.f, 1.f, 0.5f, "Variation", "%", 0.f, 100.f);
		std::vector<std::string> names;
		for (int i = 0; i < shapes::kNumRecipes; i++)
			names.push_back(shapes::recipeName(i));
		configSwitch(SHAPE_PARAM, 0.f, shapes::kNumRecipes - 1, 0.f, "Shape", names);
		configButton(SHAPE_PREV_PARAM, "Previous shape");
		configButton(SHAPE_NEXT_PARAM, "Next shape");
		configParam(VARIATION_CV_PARAM, -1.f, 1.f, 0.f, "Variation CV amount");
		configParam(TIMING_PARAM, -1.f, 1.f, 0.f, "Timing");
		configParam(LIFT_PARAM, -1.f, 1.f, 0.f, "Lift");
		configParam(SYMMETRY_PARAM, 0.f, 1.f, 0.f, "Symmetry", "%", 0.f, 100.f);
		configParam(TILT_PARAM, -1.f, 1.f, 0.f, "Tilt");
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.f, "Drift", "%", 0.f, 100.f);
		configParam(TIMING_CV_PARAM, -1.f, 1.f, 0.f, "Timing CV amount");
		configParam(LIFT_CV_PARAM, -1.f, 1.f, 0.f, "Lift CV amount");
		configParam(SYMMETRY_CV_PARAM, -1.f, 1.f, 0.f, "Symmetry CV amount");
		configParam(TILT_CV_PARAM, -1.f, 1.f, 0.f, "Tilt CV amount");
		configParam(DRIFT_CV_PARAM, -1.f, 1.f, 0.f, "Drift CV amount");
		shapeui::configBpm(this, BPM_PARAM);
		configButton(RESET_PARAM, "Reset");
		configInput(VARIATION_INPUT, "Variation CV (OUT when unpatched)");
		configInput(SHAPE_INPUT, "Shape CV");
		configInput(TIMING_INPUT, "Timing CV");
		configInput(LIFT_INPUT, "Lift CV");
		configInput(SYMMETRY_INPUT, "Symmetry CV");
		configInput(TILT_INPUT, "Tilt CV");
		configInput(DRIFT_INPUT, "Drift CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configOutput(MAIN_OUTPUT, "Main");
		configOutput(INV_OUTPUT, "Inverted");
		configOutput(EOC_OUTPUT, "End of cycle");
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
		settings.reset();
		variationPerCycle = false;
		shapeAtCycleEnd = false;
	}

	float cv(int input) {
		return (float) shapes::sanitize(inputs[input].getVoltage(), 0.0);
	}

	// Knob plus attenuverted CV (10 V spans the knob's range), clamped.
	double control(int param, int input, int trim, float lo, float hi) {
		double v = params[param].getValue() + cv(input) * 0.2 * params[trim].getValue() * (hi - lo);
		return std::clamp(shapes::sanitize(v, lo), (double) lo, (double) hi);
	}

	void stepShape(int delta) {
		int i = (int) std::round(params[SHAPE_PARAM].getValue()) + delta;
		params[SHAPE_PARAM].setValue((float) ((i + shapes::kNumRecipes) % shapes::kNumRecipes));
	}

	void process(const ProcessArgs& args) override {
		if (prevTrigger.process(params[SHAPE_PREV_PARAM].getValue() > 0.5f))
			stepShape(-1);
		if (nextTrigger.process(params[SHAPE_NEXT_PARAM].getValue() > 0.5f))
			stepShape(1);
		if (settings.slewMs != appliedSlewMs) {
			slew.set(settings.slewMs, sampleRate);
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
		// Reset starts a cycle too
		bool cycleStart = step.wrapped || step.reset || !started;
		started = true;

		float variationCv = inputs[VARIATION_INPUT].isConnected() ? cv(VARIATION_INPUT) : lastOut;
		if (!variationPerCycle || cycleStart)
			heldVariationCv = variationCv;

		shapes::SkylarkControls c;
		// 10 V of Shape CV spans all the recipes; huge voltages are clamped
		// first so the rounding can't overflow
		double shapeCv = clamp(cv(SHAPE_INPUT), -100.f, 100.f);
		int shape = (int) std::round(params[SHAPE_PARAM].getValue()) + (int) std::round(shapeCv / 10.0 * shapes::kNumRecipes);
		shape = ((shape % shapes::kNumRecipes) + shapes::kNumRecipes) % shapes::kNumRecipes;
		if (!shapeAtCycleEnd || cycleStart)
			heldShape = shape;
		c.shape = heldShape;
		double variation = params[VARIATION_PARAM].getValue() + heldVariationCv * 0.2 * params[VARIATION_CV_PARAM].getValue();
		c.variation = std::clamp(shapes::sanitize(variation, 0.5), 0.0, 1.0);
		c.timing = control(TIMING_PARAM, TIMING_INPUT, TIMING_CV_PARAM, -1.f, 1.f);
		c.lift = control(LIFT_PARAM, LIFT_INPUT, LIFT_CV_PARAM, -1.f, 1.f);
		c.symmetry = control(SYMMETRY_PARAM, SYMMETRY_INPUT, SYMMETRY_CV_PARAM, 0.f, 1.f);
		c.tilt = control(TILT_PARAM, TILT_INPUT, TILT_CV_PARAM, -1.f, 1.f);
		c.drift = control(DRIFT_PARAM, DRIFT_INPUT, DRIFT_CV_PARAM, 0.f, 1.f);
		engine.setControls(c);

		double raw = engine.raw(step.pos, step.cycle);
		double y = shapes::treat(raw, settings.treatMode, engine.peak());
		// The peak is rescanned every 64 samples while the transforms apply
		// every sample, so Fit can briefly overshoot: keep it in range
		if (settings.treatMode == shapes::Treat::Fit)
			y = std::clamp(y, -1.0, 1.0);
		double volts = shapes::toVolts(y, settings.range);
		volts = slew.process(shapes::sanitize(volts, 0.0));
		float out = (float) shapes::sanitize(volts, 0.0);
		lastOut = out;
		outputs[MAIN_OUTPUT].setVoltage(out);
		outputs[INV_OUTPUT].setVoltage((float) shapes::sanitize(shapes::invertVolts(out, settings.range), 0.0));
		if (step.wrapped)
			eoc.fire();
		outputs[EOC_OUTPUT].setVoltage(eoc.process(args.sampleTime));

		displayPos.store((float) step.pos, std::memory_order_relaxed);
		if (++sincePublish >= kPublishInterval) {
			sincePublish = 0;
			publish(c.variation);
		}
	}

	void publish(double variation) {
		int back = 1 - frontFrame.load(std::memory_order_relaxed);
		SkylarkFrame& f = frames[back];
		engine.renderCycle(f.trace, kTracePoints, settings.treatMode, settings.range);
		// Volts back to -1..1 for drawing
		float offset = settings.range == shapes::Range::Bipolar5 ? 0.f : 1.f;
		for (int i = 0; i < kTracePoints; i++)
			f.trace[i] = f.trace[i] * 0.2f - offset;
		f.shape = engine.activeShape();
		f.variation = (float) variation;
		f.bpm = (float) timebase.tempoBpm();
		frontFrame.store(back, std::memory_order_release);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		settings.toJson(rootJ);
		json_object_set_new(rootJ, "variationPerCycle", json_boolean(variationPerCycle));
		json_object_set_new(rootJ, "shapeAtCycleEnd", json_boolean(shapeAtCycleEnd));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		settings.fromJson(rootJ);
		json_t* j = json_object_get(rootJ, "variationPerCycle");
		variationPerCycle = j && json_is_true(j);
		j = json_object_get(rootJ, "shapeAtCycleEnd");
		shapeAtCycleEnd = j && json_is_true(j);
	}
};

// One cycle of the shaped output (after range and treatment, before slew),
// with a playhead and a header naming the shape, Variation and tempo.
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

	const SkylarkFrame& frame() {
		if (module)
			return module->frames[module->frontFrame.load(std::memory_order_acquire)];
		if (!havePreview) {
			previewEngine.raw(0.0, 0);
			previewEngine.renderCycle(preview.trace, kTracePoints, shapes::Treat::Fit, shapes::Range::Bipolar5);
			for (float& v : preview.trace)
				v *= 0.2f;
			havePreview = true;
		}
		return preview;
	}

	void drawScreen(NVGcontext* vg) override {
		// Copied so a publish mid-draw can't tear the trace
		SkylarkFrame f = frame();
		float pos = module ? module->displayPos.load(std::memory_order_relaxed) : 0.62f;

		const float pad = mm2px(2.f);
		const float gx = pad, gy = mm2px(7.f), gw = box.size.x - 2 * pad, gh = box.size.y - mm2px(9.f);
		shapeui::drawGrid(vg, gx, gy, gw, gh);

		std::shared_ptr<window::Font> font = shapeui::loadFont();
		if (font) {
			char title[40];
			const char* name = shapes::recipeName(f.shape);
			size_t n = 0;
			for (; name[n] && n < sizeof(title) - 1; n++)
				title[n] = (char) std::toupper((unsigned char) name[n]);
			title[n] = 0;
			char right[40];
			snprintf(right, sizeof(right), "VAR %d%%  %.2f BPM", (int) std::round(f.variation * 100.f), f.bpm);

			nvgFontFaceId(vg, font->handle);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
			nvgFontSize(vg, mm2px(2.6f));
			nvgFillColor(vg, shapeui::inkColor());
			nvgText(vg, gx, mm2px(4.6f), title, nullptr);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			nvgFontSize(vg, mm2px(2.2f));
			nvgFillColor(vg, shapeui::mutedColor());
			nvgText(vg, gx + gw, mm2px(4.6f), right, nullptr);
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
		display->box.size = mm2px(Vec(62.0, 46.5));
		display->module = module;
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(75.225, 33.5)), module, Skylark::VARIATION_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(90.375, 33.5)), module, Skylark::SHAPE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(75.225, 46)), module, Skylark::VARIATION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(90.375, 46)), module, Skylark::SHAPE_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(75.225, 55.5)), module, Skylark::VARIATION_CV_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(87.075, 55.5)), module, Skylark::SHAPE_PREV_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(93.675, 55.5)), module, Skylark::SHAPE_NEXT_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.36, 79.6)), module, Skylark::TIMING_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(31.08, 79.6)), module, Skylark::LIFT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.8, 79.6)), module, Skylark::SYMMETRY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(70.52, 79.6)), module, Skylark::TILT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(90.24, 79.6)), module, Skylark::DRIFT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.36, 89)), module, Skylark::TIMING_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(31.08, 89)), module, Skylark::LIFT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(50.8, 89)), module, Skylark::SYMMETRY_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(70.52, 89)), module, Skylark::TILT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(90.24, 89)), module, Skylark::DRIFT_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.36, 98)), module, Skylark::TIMING_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(31.08, 98)), module, Skylark::LIFT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(50.8, 98)), module, Skylark::SYMMETRY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(70.52, 98)), module, Skylark::TILT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(90.24, 98)), module, Skylark::DRIFT_INPUT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.78, 112.6)), module, Skylark::BPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(21.28, 112.6)), module, Skylark::CLOCK_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(32.83, 112.6)), module, Skylark::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(41.33, 112.6)), module, Skylark::RESET_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(61.865, 112.6)), module, Skylark::EOC_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(75.945, 112.6)), module, Skylark::INV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(90.025, 112.6)), module, Skylark::MAIN_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Skylark* module = getModule<Skylark>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		shapeui::appendSettingsMenu(menu, module, &module->settings);
		menu->addChild(createIndexSubmenuItem("Variation CV", {"Continuous", "Sample once per cycle"},
			[=]() { return module->variationPerCycle ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set Variation CV mode", [=]() { module->variationPerCycle = i == 1; });
			}));
		menu->addChild(createIndexSubmenuItem("Shape changes", {"Immediately", "At end of cycle"},
			[=]() { return module->shapeAtCycleEnd ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set shape change timing", [=]() { module->shapeAtCycleEnd = i == 1; });
			}));
	}
};

Model* modelSkylark = createModel<Skylark, SkylarkWidget>("Skylark");
