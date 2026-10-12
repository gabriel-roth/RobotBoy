#include "Retours.hpp"
#include "beads_menu.hpp"

#include "host/controls.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Retours: the panel, saved settings and engine mode. The audio is done by
// the active engine adapter: RetoursBeads (the delay mode of Mutable
// Instruments Beads) or the classic v2.1 engine (RetoursClassic).

// Output mute after a live engine switch, as beads.cc's codec_transition.
static constexpr float kModeSwitchMuteSeconds = 0.25f;

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

	sample_rate_ = APP->engine->getSampleRate();
	classic_.store(false);
	engine_ = MakeRetoursBeads(*this);
	if (!engine_ || !engine_->ok()) {
		// WARN is defined identically by both hosts' logger.hpp.
		WARN("Retours: could not allocate the Beads delay memory");
	}
}

void Retours::requestMode(bool classic) {   // UI thread
	pending_mode_.store(kModeLive + (classic ? 1 : 0), std::memory_order_release);
}

void Retours::applyPendingMode(float sample_rate) {   // audio thread
	const int pending = pending_mode_.exchange(kModeNone, std::memory_order_acq_rel);
	if (pending < 0) return;
	const bool want = (pending & 1) != 0;
	const bool forced = pending >= kModeForced;
	if (engine_ && want == classicMode()) return;
	// A live switch is refused while sliced, like Quality. A load or undo
	// is not: the module must run the mode its JSON says.
	if (engine_ && !forced && engine_->frozen()) return;
	classic_.store(want, std::memory_order_relaxed);
	sample_rate_ = sample_rate;
	engine_.reset();   // free first: peak RAM is the larger engine, not both
	engine_ = want ? MakeRetoursClassic(*this) : MakeRetoursBeads(*this);
	if (!engine_ || !engine_->ok())
		WARN("Retours: could not allocate the %s engine", want ? "classic" : "Beads");
	// A patch loaded straight into a mode switches before the first frame:
	// nothing has played, so there is nothing to mute.
	mute_seconds_ = processed_any_ ? kModeSwitchMuteSeconds : 0.f;
}

void Retours::process(const ProcessArgs& args) {
	applyPendingMode(args.sampleRate);
	processed_any_ = true;
	if (engine_) {
		engine_->process(args);
		ui_frozen_.store(engine_->frozen(), std::memory_order_relaxed);
		ui_clear_disabled_.store(engine_->clearDisabled(), std::memory_order_relaxed);
		ui_clear_tempo_disabled_.store(engine_->clearTempoDisabled(), std::memory_order_relaxed);
	} else {
		mute_seconds_ = std::max(mute_seconds_, args.sampleTime);   // no engine: silence
		ui_frozen_.store(false, std::memory_order_relaxed);
		ui_clear_disabled_.store(true, std::memory_order_relaxed);
		ui_clear_tempo_disabled_.store(true, std::memory_order_relaxed);
	}
	if (mute_seconds_ > 0.f) {
		outputs[OUT_L_OUTPUT].setVoltage(0.f);
		outputs[OUT_R_OUTPUT].setVoltage(0.f);
		outputs[GATE_OUTPUT].setVoltage(0.f);
		mute_seconds_ -= args.sampleTime;
	}
}

void Retours::onSampleRateChange(const SampleRateChangeEvent& e) {
	sample_rate_ = e.sampleRate;
	if (engine_) engine_->onSampleRateChange(e.sampleRate);
}

void Retours::onReset(const ResetEvent& e) {
	Module::onReset(e);
	// Resets the settings, not the mode.
	quality_state_ = 0;
	auto_gain_ = true;
	manual_gain_db_ = 0.f;
	tape_hiss_ = true;
	time_change_mode_ = 0;
	clear_requested_.store(true);
	clear_tempo_requested_.store(true);   // initialize returns to free-running
	recalibrate_requested_.store(true);
	if (engine_) engine_->onReset();   // runs under Rack's engine lock
}

json_t* Retours::dataToJson() {
	json_t* root = json_object();
	// A switch not yet applied is saved as the mode, so the menu's undo
	// snapshot records it.
	json_object_set_new(root, "classic",        json_boolean(displayedClassicMode()));
	json_object_set_new(root, "qualityState",   json_integer(quality_state_));
	json_object_set_new(root, "autoGain",       json_boolean(auto_gain_));
	json_object_set_new(root, "manualGainDb",   json_real(manual_gain_db_));
	json_object_set_new(root, "tapeHiss",       json_boolean(tape_hiss_));
	json_object_set_new(root, "timeChangeMode", json_integer(time_change_mode_));
	return root;
}

void Retours::dataFromJson(json_t* root) {
	json_t* j;
	json_t* classic_j = json_object_get(root, "classic");
	const bool classic = robotboy::DetectClassic(
		classic_j != nullptr, json_is_true(classic_j),
		json_object_get(root, "tapeHiss") != nullptr,
		robotboy::ClassicFingerprint::kAbsentTapeHiss);

	if ((j = json_object_get(root, "qualityState")))
		quality_state_ = clamp((int)json_integer_value(j), 0, 3);
	if ((j = json_object_get(root, "autoGain")))
		auto_gain_ = json_boolean_value(j);
	if ((j = json_object_get(root, "manualGainDb")))
		manual_gain_db_ = clamp((float)json_real_value(j), 0.f, 32.f);
	if ((j = json_object_get(root, "tapeHiss")))
		tape_hiss_ = json_boolean_value(j);
	// Classic mode's time-change response, v2.1's key and meaning
	// (retours_delay_dsp::TimeChangeMode: 0 = Tape, 1 = Crossfade).
	if ((j = json_object_get(root, "timeChangeMode")))
		time_change_mode_ = clamp((int)json_integer_value(j), 0, 1);
	else
		time_change_mode_ = 0;
	// "inputTrimDb"/"slewSeconds"/"envelopePreFeedback" from older patches
	// are ignored, as in v2.1.

	// The engine swap happens at the next process(), on the audio thread,
	// forced: a load or undo applies even while sliced.
	pending_mode_.store(kModeForced + (classic ? 1 : 0), std::memory_order_release);
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

		// --- Classic mode: the v2.1 engine ---
		// Refused while sliced, like Quality: a switch would drop the frozen
		// recording.
		{
			const bool frozen = module->ui_frozen_.load(std::memory_order_relaxed);
			menu->addChild(createBoolMenuItem("Classic mode",
				frozen ? "Turn off Slice to switch" : "",
				[=]() { return module->displayedClassicMode(); },
				[=](bool on) {
					withMenuUndo(module, on ? "enable classic mode" : "disable classic mode",
						[=]() { module->requestMode(on); });
				},
				/*disabled=*/frozen
			));
		}

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

		// The engine's own items, for the mode the toggle shows. Built from
		// module state only: the UI thread never touches engine_.
		if (module->displayedClassicMode())
			AppendRetoursClassicMenu(module, menu);
		else
			AppendRetoursBeadsMenu(module, menu);

		// --- Clear saved tempo ---
		// A tempo from a tap or a clock holds after the clock is unpatched;
		// this returns to free-running. Greyed out when there is nothing to
		// clear (and in Beads mode while a clock is patched, which would set
		// the tempo again).
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
