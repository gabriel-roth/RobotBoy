#include "Retours.hpp"
#include "beads_menu.hpp"
#include "lucarne/Lucarne.hpp"

#include "host/controls.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Retours: the panel and saved settings. The audio is done by the engine
// adapter, RetoursBeads (the delay mode of Mutable Instruments Beads).

// Filter knob tooltip: off at noon (Beads' dead band), otherwise how far
// into the low-pass or high-pass side the knob is.
struct FilterParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		const float amount = robotboy_beads::AttenuverterLaw(getValue());
		if (amount == 0.f) return "Off";
		return string::f("%s %.0f%%", amount < 0.f ? "Low-pass" : "High-pass",
			std::fabs(amount) * 100.f);
	}
};

struct RetoursQualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};

Retours::Retours() {
	config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	// configSwitch does NOT disable randomization (only configButton does),
	// so SLICE needs the explicit flag or Ctrl+R randomly latches freeze.
	configSwitch(SLICE_PARAM, 0.f, 1.f, 0.f, "Slice", {"Off", "On"})
		->randomizeEnabled = false;
#ifdef METAMODULE
	// MetaModule: a real 4-position switch so the firmware shows the mode
	// name (the adapter can't label a momentary button). Desktop keeps the
	// momentary cycle button below.
	configSwitch(QUALITY_PARAM, 0.f, 3.f, 0.f, "Quality",
		{"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"})
		->randomizeEnabled = false;
#else
	configButton<RetoursQualityParamQuantity>(QUALITY_PARAM, "Quality");
#endif
	configParam(INTERVAL_PARAM, 0.f, 1.f, 0.35f, "Interval");
	configParam(TIME_PARAM, 0.f, 1.f, 0.f, "Time");
	configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
	configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "Shape");
	configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback");
	configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/wet");
	configParam(TIME_AR_PARAM, -1.f, 1.f, 0.f, "Time AR");
	configParam(PITCH_AR_PARAM, -1.f, 1.f, 0.f, "Pitch AR");
	configParam(SHAPE_AR_PARAM, -1.f, 1.f, 0.f, "Shape AR");
	configButton(CLOCK_PARAM, "Tap tempo");
	configParam(FEEDBACK_AR_PARAM, -1.f, 1.f, 0.f, "Feedback CV amount");
	configParam(DRY_WET_AR_PARAM, -1.f, 1.f, 0.f, "Dry/wet CV amount");
	configParam<FilterParamQuantity>(FILTER_PARAM, 0.f, 1.f, 0.5f, "Filter");
	configParam(FILTER_AR_PARAM, -1.f, 1.f, 0.f, "Filter CV amount");
	configParam(REVERB_PARAM, 0.f, 1.f, 0.f, "Reverb");
	configParam(REVERB_AR_PARAM, -1.f, 1.f, 0.f, "Reverb CV amount");

	configInput(IN_L_INPUT, "Audio in L");
	configInput(IN_R_INPUT, "Audio in R");
	configInput(INTERVAL_CV_INPUT, "Interval CV");
	configInput(TIME_CV_INPUT, "Time CV");
	configInput(PITCH_CV_INPUT, "Pitch CV");
	configInput(SHAPE_CV_INPUT, "Shape CV");
	configInput(FEEDBACK_CV_INPUT, "Feedback CV");
	configInput(DRY_WET_CV_INPUT, "Dry/wet CV");
	configInput(FILTER_CV_INPUT, "Filter CV");
	configInput(REVERB_CV_INPUT, "Reverb CV");
	configInput(CLOCK_INPUT, "Clock");
	configInput(SLICE_INPUT, "Slice gate");
	configOutput(OUT_L_OUTPUT, "Audio out L");
	configOutput(OUT_R_OUTPUT, "Audio out R");
	configOutput(GATE_OUTPUT, "Repeat gate");
	configBypass(IN_L_INPUT, OUT_L_OUTPUT);
	configBypass(IN_R_INPUT, OUT_R_OUTPUT);
	// SLICE and QUALITY lights are embedded in their button params (tooltips
	// come from configParam); CLOCK_LIGHT is a standalone beat indicator.
	configLight(CLOCK_LIGHT, "Clock");

	engine_ = MakeRetoursBeads(*this, APP->engine->getSampleRate());
	grain_feed_.Reset();
	if (!engine_ || !engine_->ok()) {
		// WARN is defined identically by both hosts' logger.hpp.
		WARN("Retours: could not allocate the Beads delay memory");
	}
}

void Retours::process(const ProcessArgs& args) {
	// Every 256 samples: is a Lucarne attached on the right? dynamic_cast,
	// not model ==: MetaModule SDK < 2.4 leaves model unset on adapter modules.
	if (++expander_check_ >= 256) {
		expander_check_ = 0;
		grain_feed_.attached.store(dynamic_cast<Lucarne*>(rightExpander.module) != nullptr,
		                           std::memory_order_relaxed);
	}
	if (engine_) {
		engine_->process(args);
		ui_clear_disabled_.store(engine_->clearDisabled(), std::memory_order_relaxed);
		ui_clear_tempo_disabled_.store(engine_->clearTempoDisabled(), std::memory_order_relaxed);
	} else {   // allocation failed: silence
		outputs[OUT_L_OUTPUT].setVoltage(0.f);
		outputs[OUT_R_OUTPUT].setVoltage(0.f);
		outputs[GATE_OUTPUT].setVoltage(0.f);
	}
}

void Retours::onSampleRateChange(const SampleRateChangeEvent& e) {
	if (engine_) engine_->onSampleRateChange(e.sampleRate);
}

void Retours::onReset(const ResetEvent& e) {
	Module::onReset(e);
	quality_state_ = 0;
	auto_gain_ = true;
	manual_gain_db_ = 0.f;
	tape_hiss_ = true;
	clear_requested_.store(true);
	clear_tempo_requested_.store(true);   // initialize returns to free-running
	recalibrate_requested_.store(true);
	if (engine_) engine_->onReset();   // runs under Rack's engine lock
}

json_t* Retours::dataToJson() {
	json_t* root = json_object();
	json_object_set_new(root, "qualityState",   json_integer(quality_state_));
	json_object_set_new(root, "autoGain",       json_boolean(auto_gain_));
	json_object_set_new(root, "manualGainDb",   json_real(manual_gain_db_));
	json_object_set_new(root, "tapeHiss",       json_boolean(tape_hiss_));
	return root;
}

void Retours::dataFromJson(json_t* root) {
	json_t* j;
	// Settings that are absent from the patch go back to their defaults.
	auto_gain_ = true;
	manual_gain_db_ = 0.f;
	tape_hiss_ = true;

	if ((j = json_object_get(root, "qualityState")))
		quality_state_ = clamp((int)json_integer_value(j), 0, 3);
	if ((j = json_object_get(root, "autoGain")))
		auto_gain_ = json_boolean_value(j);
	if ((j = json_object_get(root, "manualGainDb")))
		manual_gain_db_ = clamp((float)json_real_value(j), 0.f, 32.f);
	if ((j = json_object_get(root, "tapeHiss")))
		tape_hiss_ = json_boolean_value(j);
	// "timeChangeMode" (and older "inputTrimDb"/"slewSeconds"/
	// "envelopePreFeedback") from patches made before the Beads engine
	// are ignored: Beads has one fixed time-change behavior.
}

std::string RetoursQualityParamQuantity::getDisplayValueString() {
	Retours* m = dynamic_cast<Retours*>(module);
	if (!m) return "";
	static const char* kNames[] = {"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"};
	return kNames[m->quality_state_];
}

#ifdef METAMODULE
// MetaModule-only Quality control. The VCV→MM adapter turns a non-momentary
// SvgSwitch with >=3 frames plus a configSwitch into a labeled FlipSwitch, so
// MM shows the mode name in the Adjust popup and the per-mode colour comes from
// the frame image (metamodule/assets/quality_*.png, reached via the .svg->.png
// asset rule). The desktop build keeps its momentary RGB bezel instead — see
// the #ifndef METAMODULE branch in RetoursWidget and Retours(). The referenced
// .svg files never need to exist: this widget is compiled only for METAMODULE,
// where asset::plugin() rewrites the path to the bundled .png.
struct QualityMmSwitch : SvgSwitch {
	QualityMmSwitch() {
		momentary = false;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_bright.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_cold.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_sunny.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_scorched.svg")));
	}
};
#endif

struct RetoursWidget : ModuleWidget {
	void appendContextMenu(Menu* menu) override {
		Retours* module = dynamic_cast<Retours*>(this->module);
		if (!module) return;

		menu->addChild(new MenuSeparator);

		// --- Quality ---
		// VCV only: on MetaModule Quality is a front-panel switch, which
		// process() reads every sample, so a menu choice would be overwritten.
#ifndef METAMODULE
		menu->addChild(createIndexSubmenuItem("Quality",
			{"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"},
			[=]() { return module->quality_state_; },
			[=](int val) {
				withMenuUndo(module, "change quality",
					[=]() { module->quality_state_ = val; });
			}
		));
#endif

		AppendRetoursBeadsMenu(module, menu);

		// --- Clear saved tempo ---
		// A tempo from a tap or a clock holds after the clock is unpatched;
		// this returns to free-running. Greyed out when there is nothing to
		// clear (and while a clock is patched, which would set the tempo
		// again).
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear saved tempo", "",
			[=]() { module->clear_tempo_requested_.store(true); },
			/*disabled=*/module->ui_clear_tempo_disabled_.load(std::memory_order_relaxed)
		));

		// --- Clear Buffer ---
		// Deferred to the audio thread (process()) to avoid racing the DSP;
		// takes effect at the next block boundary.
		menu->addChild(createMenuItem("Clear buffer", "",
			[=]() { module->clear_requested_.store(true); },
			/*disabled=*/module->ui_clear_disabled_.load(std::memory_order_relaxed)
		));
	}

	RetoursWidget(Retours* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Retours.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Panel layout: mm centers of the components in res/Retours.svg,
		// carried verbatim from panel-specs/retours.yaml (Particules' grid).
		// --- Top row: slice jack/button, quality, tap-tempo button, clock jack ---
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.9f, 15.875f)), module, Retours::SLICE_INPUT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(20.4f, 15.875f)), module, Retours::SLICE_PARAM, Retours::SLICE_BUTTON_LIGHT));
#ifdef METAMODULE
		addParam(createParamCentered<QualityMmSwitch>(mm2px(Vec(38.1f, 15.875f)), module, Retours::QUALITY_PARAM));
#else
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(38.1f, 15.875f)), module, Retours::QUALITY_PARAM, Retours::QUALITY_R_LIGHT));
#endif
		addParam(createParamCentered<VCVButton>(mm2px(Vec(55.8f, 15.875f)), module, Retours::CLOCK_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(64.3f, 15.875f)), module, Retours::CLOCK_INPUT));

		// --- Row 1: Time / Interval / Pitch / Filter (Interval has no
		// trimpot; the clock light sits in that slot) ---
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6f, 42.088f)), module, Retours::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6f, 42.088f)), module, Retours::INTERVAL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6f, 42.088f)), module, Retours::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6f, 42.088f)), module, Retours::FILTER_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6f, 53.746f)), module, Retours::TIME_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6f, 53.746f)), module, Retours::INTERVAL_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6f, 53.746f)), module, Retours::PITCH_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6f, 53.746f)), module, Retours::FILTER_CV_INPUT));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6f, 62.746f)), module, Retours::TIME_AR_PARAM));
		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(29.6f, 62.746f)), module, Retours::CLOCK_LIGHT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6f, 62.746f)), module, Retours::PITCH_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6f, 62.746f)), module, Retours::FILTER_AR_PARAM));

		// --- Row 2: Shape / Feedback / Reverb / Dry-wet ---
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6f, 82.096f)), module, Retours::SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6f, 82.096f)), module, Retours::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6f, 82.096f)), module, Retours::REVERB_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6f, 82.096f)), module, Retours::DRY_WET_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6f, 92.696f)), module, Retours::SHAPE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6f, 92.696f)), module, Retours::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6f, 92.696f)), module, Retours::REVERB_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6f, 92.696f)), module, Retours::DRY_WET_CV_INPUT));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6f, 101.696f)), module, Retours::SHAPE_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(29.6f, 101.696f)), module, Retours::FEEDBACK_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6f, 101.696f)), module, Retours::REVERB_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6f, 101.696f)), module, Retours::DRY_WET_AR_PARAM));

		// --- Audio I/O ---
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.25f, 114.3f)), module, Retours::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.95f, 114.3f)), module, Retours::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.25f, 114.3f)), module, Retours::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(60.95f, 114.3f)), module, Retours::OUT_R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.1f, 114.3f)), module, Retours::GATE_OUTPUT));
	}
};


Model* modelRetours = createModel<Retours, RetoursWidget>("Retours");
