#include "plugin.hpp"
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

static const float kSlewMs[] = {0.f, 2.f, 5.f, 20.f};
static const int kPulseChoices[] = {1, 2, 4, 8, 16, 24, 32, 48, 96};

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
		REVERSE_PARAM,
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
		REVERSE_INPUT,
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
	shapes::Range range = shapes::Range::Bipolar5;
	shapes::Treat treatMode = shapes::Treat::Fit;
	float slewMs = 0.f;
	int pulsesPerCycle = 4;
	float bpmFine = 0.f;
	bool variationPerCycle = false;

	shapes::Timebase timebase;
	shapes::SkylarkEngine engine;
	shapes::Slew slew;
	shapes::Trigger eoc;
	dsp::BooleanTrigger prevTrigger, nextTrigger;
	dsp::SchmittTrigger reverseGate;
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
		ParamQuantity* bpm = configParam(BPM_PARAM, 20.f, 300.f, 120.f, "Tempo", " BPM");
		bpm->snapEnabled = true;
		configButton(RESET_PARAM, "Reset");
		configButton(REVERSE_PARAM, "Reverse");
		configInput(VARIATION_INPUT, "Variation CV (OUT when unpatched)");
		configInput(SHAPE_INPUT, "Shape CV");
		configInput(TIMING_INPUT, "Timing CV");
		configInput(LIFT_INPUT, "Lift CV");
		configInput(SYMMETRY_INPUT, "Symmetry CV");
		configInput(TILT_INPUT, "Tilt CV");
		configInput(DRIFT_INPUT, "Drift CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(REVERSE_INPUT, "Reverse gate");
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
		range = shapes::Range::Bipolar5;
		treatMode = shapes::Treat::Fit;
		slewMs = 0.f;
		pulsesPerCycle = 4;
		bpmFine = 0.f;
		variationPerCycle = false;
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
		if (slewMs != appliedSlewMs) {
			slew.set(slewMs, sampleRate);
			appliedSlewMs = slewMs;
		}

		shapes::TimebaseInput tin;
		tin.bpm = params[BPM_PARAM].getValue() + bpmFine;
		tin.clockConnected = inputs[CLOCK_INPUT].isConnected();
		tin.clockVoltage = cv(CLOCK_INPUT);
		tin.resetVoltage = cv(RESET_INPUT);
		tin.resetButton = params[RESET_PARAM].getValue() > 0.5f;
		tin.pulsesPerCycle = pulsesPerCycle;
		shapes::Timebase::Step step = timebase.process(tin);
		// Reset starts a cycle too, so a pending shape change lands there
		bool cycleStart = step.wrapped || step.reset || !started;
		started = true;

		float variationCv = inputs[VARIATION_INPUT].isConnected() ? cv(VARIATION_INPUT) : lastOut;
		if (!variationPerCycle || cycleStart)
			heldVariationCv = variationCv;
		reverseGate.process(cv(REVERSE_INPUT), 0.1f, 1.f);

		shapes::SkylarkControls c;
		// 10 V of Shape CV spans all the recipes; huge voltages are clamped
		// first so the rounding can't overflow
		double shapeCv = clamp(cv(SHAPE_INPUT), -100.f, 100.f);
		int shape = (int) std::round(params[SHAPE_PARAM].getValue()) + (int) std::round(shapeCv / 10.0 * shapes::kNumRecipes);
		c.shape = ((shape % shapes::kNumRecipes) + shapes::kNumRecipes) % shapes::kNumRecipes;
		double variation = params[VARIATION_PARAM].getValue() + heldVariationCv * 0.2 * params[VARIATION_CV_PARAM].getValue();
		c.variation = std::clamp(shapes::sanitize(variation, 0.5), 0.0, 1.0);
		c.timing = control(TIMING_PARAM, TIMING_INPUT, TIMING_CV_PARAM, -1.f, 1.f);
		c.lift = control(LIFT_PARAM, LIFT_INPUT, LIFT_CV_PARAM, -1.f, 1.f);
		c.symmetry = control(SYMMETRY_PARAM, SYMMETRY_INPUT, SYMMETRY_CV_PARAM, 0.f, 1.f);
		c.tilt = control(TILT_PARAM, TILT_INPUT, TILT_CV_PARAM, -1.f, 1.f);
		c.drift = control(DRIFT_PARAM, DRIFT_INPUT, DRIFT_CV_PARAM, 0.f, 1.f);
		c.reverse = params[REVERSE_PARAM].getValue() > 0.5f || reverseGate.isHigh();
		engine.setControls(c);

		double raw = engine.raw(step.pos, step.cycle, cycleStart);
		double volts = shapes::toVolts(shapes::treat(raw, treatMode, engine.peak()), range);
		volts = slew.process(shapes::sanitize(volts, 0.0));
		float out = (float) shapes::sanitize(volts, 0.0);
		lastOut = out;
		outputs[MAIN_OUTPUT].setVoltage(out);
		outputs[INV_OUTPUT].setVoltage((float) shapes::sanitize(shapes::invertVolts(out, range), 0.0));
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
		engine.renderCycle(f.trace, kTracePoints, treatMode, range);
		// Volts back to -1..1 for drawing
		float offset = range == shapes::Range::Bipolar5 ? 0.f : 1.f;
		for (int i = 0; i < kTracePoints; i++)
			f.trace[i] = f.trace[i] * 0.2f - offset;
		f.shape = engine.activeShape();
		f.variation = (float) variation;
		f.bpm = (float) timebase.tempoBpm();
		frontFrame.store(back, std::memory_order_release);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "range", json_integer((int) range));
		json_object_set_new(rootJ, "treat", json_integer((int) treatMode));
		json_object_set_new(rootJ, "slewMs", json_real(slewMs));
		json_object_set_new(rootJ, "pulsesPerCycle", json_integer(pulsesPerCycle));
		json_object_set_new(rootJ, "bpmFine", json_real(bpmFine));
		json_object_set_new(rootJ, "variationPerCycle", json_boolean(variationPerCycle));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// Missing or unknown values fall back to the defaults
		json_t* j = json_object_get(rootJ, "range");
		range = j && json_integer_value(j) == 1 ? shapes::Range::Unipolar10 : shapes::Range::Bipolar5;
		j = json_object_get(rootJ, "treat");
		int t = j ? (int) json_integer_value(j) : 0;
		treatMode = t >= 0 && t <= (int) shapes::Treat::Wrap ? (shapes::Treat) t : shapes::Treat::Fit;
		j = json_object_get(rootJ, "slewMs");
		float ms = j ? (float) json_number_value(j) : 0.f;
		slewMs = 0.f;
		for (float choice : kSlewMs)
			if (ms == choice)
				slewMs = choice;
		j = json_object_get(rootJ, "pulsesPerCycle");
		int ppc = j ? (int) json_integer_value(j) : 4;
		pulsesPerCycle = 4;
		for (int choice : kPulseChoices)
			if (ppc == choice)
				pulsesPerCycle = choice;
		j = json_object_get(rootJ, "bpmFine");
		float fine = j ? (float) json_number_value(j) : 0.f;
		bpmFine = std::isfinite(fine) ? clamp(std::round(fine * 100.f) / 100.f, -0.5f, 0.5f) : 0.f;
		j = json_object_get(rootJ, "variationPerCycle");
		variationPerCycle = j && json_is_true(j);
	}
};

// One cycle of the shaped output (after range and treatment, before slew),
// with a playhead and a header naming the shape, Variation and tempo.
struct SkylarkDisplay : LedDisplay {
	Skylark* module = nullptr;
	// The module browser's picture: Bounce at default settings, built once
	shapes::SkylarkEngine previewEngine;
	SkylarkFrame preview;
	bool havePreview = false;

	const SkylarkFrame& frame() {
		if (module)
			return module->frames[module->frontFrame.load(std::memory_order_acquire)];
		if (!havePreview) {
			previewEngine.raw(0.0, 0, true);
			previewEngine.renderCycle(preview.trace, kTracePoints, shapes::Treat::Fit, shapes::Range::Bipolar5);
			for (float& v : preview.trace)
				v *= 0.2f;
			havePreview = true;
		}
		return preview;
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.6f));
		nvgFillColor(args.vg, nvgRGB(0x0c, 0x0e, 0x10));
		nvgFill(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1)
			drawContents(args.vg);
		Widget::drawLayer(args, layer);
	}

	void drawContents(NVGcontext* vg) {
		// Copied so a publish mid-draw can't tear the trace
		SkylarkFrame f = frame();
		float pos = module ? module->displayPos.load(std::memory_order_relaxed) : 0.62f;
		if (!std::isfinite(pos))
			pos = 0.f;
		pos = clamp(pos, 0.f, 1.f);

		const float pad = mm2px(2.f);
		const float gx = pad, gy = mm2px(7.f), gw = box.size.x - 2 * pad, gh = box.size.y - mm2px(9.f);

		nvgScissor(vg, 0, 0, box.size.x, box.size.y);
		for (int i = 0; i <= 4; i++) {
			float y = gy + gh * i / 4.f;
			nvgBeginPath(vg);
			nvgMoveTo(vg, gx, y);
			nvgLineTo(vg, gx + gw, y);
			nvgStrokeWidth(vg, mm2px(i == 2 ? 0.25f : 0.12f));
			nvgStrokeColor(vg, nvgRGB(0x1d, 0x22, 0x27));
			nvgStroke(vg);
		}

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
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
			nvgFillColor(vg, nvgRGB(0xe6, 0xe8, 0xeb));
			nvgText(vg, gx, mm2px(4.6f), title, nullptr);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			nvgFontSize(vg, mm2px(2.2f));
			nvgFillColor(vg, nvgRGB(0x98, 0xa1, 0xab));
			nvgText(vg, gx + gw, mm2px(4.6f), right, nullptr);
		}

		auto yAt = [&](float v) {
			return gy + gh * 0.5f - clamp(v, -1.f, 1.f) * gh * 0.5f;
		};
		// The trace closes the loop: its last point is the next cycle's first
		nvgBeginPath(vg);
		for (int i = 0; i <= kTracePoints; i++) {
			float x = gx + gw * i / kTracePoints;
			float y = yAt(f.trace[i % kTracePoints]);
			if (i == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		nvgLineJoin(vg, NVG_ROUND);
		nvgStrokeWidth(vg, mm2px(0.45f));
		nvgStrokeColor(vg, nvgRGB(0xff, 0x8a, 0x3d));
		nvgStroke(vg);

		float hx = gx + gw * pos;
		float k = pos * kTracePoints;
		int i0 = std::min((int) k, kTracePoints - 1);
		float hv = f.trace[i0] + (f.trace[(i0 + 1) % kTracePoints] - f.trace[i0]) * (k - i0);
		nvgBeginPath(vg);
		nvgMoveTo(vg, hx, gy);
		nvgLineTo(vg, hx, gy + gh);
		nvgStrokeWidth(vg, mm2px(0.15f));
		nvgStrokeColor(vg, nvgRGBA(0xe6, 0xe8, 0xeb, 153));
		nvgStroke(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, hx, yAt(hv), mm2px(0.7f));
		nvgFillColor(vg, nvgRGB(0xff, 0x8a, 0x3d));
		nvgFill(vg);
		nvgResetScissor(vg);
	}
};

// BPM fine as a menu slider. A drag (or double-click reset) is one undo step.
// The slider moves an unrounded value so slow drags still creep along; the
// module gets it rounded to 0.01 BPM.
struct BpmFineQuantity : Quantity {
	Skylark* module;
	float raw;
	explicit BpmFineQuantity(Skylark* m) : module(m), raw(m->bpmFine) {}
	void setValue(float v) override {
		raw = clamp(v, -0.5f, 0.5f);
		module->bpmFine = std::round(raw * 100.f) / 100.f;
	}
	float getValue() override {
		return raw;
	}
	float getMinValue() override {
		return -0.5f;
	}
	float getMaxValue() override {
		return 0.5f;
	}
	float getDefaultValue() override {
		return 0.f;
	}
	int getDisplayPrecision() override {
		return 2;
	}
	std::string getLabel() override {
		return "BPM fine";
	}
	std::string getUnit() override {
		return " BPM";
	}
	std::string getDisplayValueString() override {
		return string::f("%+.2f", module->bpmFine);
	}
};

struct BpmFineSlider : ui::Slider {
	Skylark* module;
	json_t* oldModuleJ = nullptr;
	float oldFine = 0.f;

	explicit BpmFineSlider(Skylark* m) : module(m) {
		quantity = new BpmFineQuantity(m);
		box.size.x = 200.f;
	}
	~BpmFineSlider() override {
		if (oldModuleJ)
			json_decref(oldModuleJ);
		delete quantity;
	}
	void onDragStart(const DragStartEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && !oldModuleJ) {
			oldModuleJ = APP->engine->moduleToJson(module);
			oldFine = module->bpmFine;
		}
		ui::Slider::onDragStart(e);
	}
	void onDragEnd(const DragEndEvent& e) override {
		ui::Slider::onDragEnd(e);
		if (!oldModuleJ)
			return;
		// A click without a drag changes nothing; leave no undo step
		if (module->bpmFine == oldFine) {
			json_decref(oldModuleJ);
			oldModuleJ = nullptr;
			return;
		}
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "set BPM fine";
		h->moduleId = module->id;
		h->oldModuleJ = oldModuleJ;
		h->newModuleJ = APP->engine->moduleToJson(module);
		APP->history->push(h);
		oldModuleJ = nullptr;
	}
	void onDoubleClick(const DoubleClickEvent& e) override {
		static_cast<BpmFineQuantity*>(quantity)->raw = 0.f;
		if (module->bpmFine == 0.f)
			return;
		Skylark* m = module;
		withMenuUndo(m, "reset BPM fine", [m]() { m->bpmFine = 0.f; });
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
		display->box.size = mm2px(Vec(66.0, 49.5));
		display->module = module;
		addChild(display);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(80.51, 34.35)), module, Skylark::VARIATION_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(99.25, 34.35)), module, Skylark::SHAPE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(80.51, 47.85)), module, Skylark::VARIATION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(99.25, 47.85)), module, Skylark::SHAPE_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(80.51, 57.85)), module, Skylark::VARIATION_CV_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(95.95, 57.85)), module, Skylark::SHAPE_PREV_PARAM));
		addParam(createParamCentered<TL1105>(mm2px(Vec(102.55, 57.85)), module, Skylark::SHAPE_NEXT_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.38, 80.6)), module, Skylark::TIMING_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(34.13, 80.6)), module, Skylark::LIFT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(55.88, 80.6)), module, Skylark::SYMMETRY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(77.63, 80.6)), module, Skylark::TILT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(99.38, 80.6)), module, Skylark::DRIFT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.38, 89.5)), module, Skylark::TIMING_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(34.13, 89.5)), module, Skylark::LIFT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(55.88, 89.5)), module, Skylark::SYMMETRY_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(77.63, 89.5)), module, Skylark::TILT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(99.38, 89.5)), module, Skylark::DRIFT_CV_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.38, 98.0)), module, Skylark::TIMING_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(34.13, 98.0)), module, Skylark::LIFT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(55.88, 98.0)), module, Skylark::SYMMETRY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(77.63, 98.0)), module, Skylark::TILT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(99.38, 98.0)), module, Skylark::DRIFT_INPUT));

		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(9.18, 112.6)), module, Skylark::BPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.68, 112.6)), module, Skylark::CLOCK_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(29.64, 112.6)), module, Skylark::RESET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(38.14, 112.6)), module, Skylark::RESET_INPUT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(48.09, 112.6)), module, Skylark::REVERSE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(56.59, 112.6)), module, Skylark::REVERSE_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(74.68, 112.6)), module, Skylark::EOC_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(87.88, 112.6)), module, Skylark::INV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(101.08, 112.6)), module, Skylark::MAIN_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Skylark* module = getModule<Skylark>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Output range", {"+-5 V", "0-10 V"},
			[=]() { return (int) module->range; },
			[=](int i) {
				withMenuUndo(module, "set output range", [=]() { module->range = (shapes::Range) i; });
			}));
		menu->addChild(createIndexSubmenuItem("When out of range", {"Fit", "Clip", "Soft clip", "Fold", "Wrap"},
			[=]() { return (int) module->treatMode; },
			[=](int i) {
				withMenuUndo(module, "set out-of-range mode", [=]() { module->treatMode = (shapes::Treat) i; });
			}));
		menu->addChild(createIndexSubmenuItem("Slew", {"Off", "2 ms", "5 ms", "20 ms"},
			[=]() {
				for (int i = 0; i < 4; i++)
					if (module->slewMs == kSlewMs[i])
						return i;
				return 0;
			},
			[=](int i) {
				withMenuUndo(module, "set slew", [=]() { module->slewMs = kSlewMs[i]; });
			}));
		menu->addChild(createIndexSubmenuItem("Clock pulses per cycle",
			{"1", "2", "4", "8", "16", "24", "32", "48", "96"},
			[=]() {
				for (int i = 0; i < 9; i++)
					if (module->pulsesPerCycle == kPulseChoices[i])
						return i;
				return 2;
			},
			[=](int i) {
				withMenuUndo(module, "set clock pulses per cycle", [=]() { module->pulsesPerCycle = kPulseChoices[i]; });
			}));
		menu->addChild(new BpmFineSlider(module));
		menu->addChild(createIndexSubmenuItem("Variation CV", {"Continuous", "Sample once per cycle"},
			[=]() { return module->variationPerCycle ? 1 : 0; },
			[=](int i) {
				withMenuUndo(module, "set Variation CV mode", [=]() { module->variationPerCycle = i == 1; });
			}));
	}
};

Model* modelSkylark = createModel<Skylark, SkylarkWidget>("Skylark");
