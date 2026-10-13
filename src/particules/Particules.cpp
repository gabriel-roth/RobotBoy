#include "Particules.hpp"
#include "beads_menu.hpp"
#include "lucarne/Lucarne.hpp"
#include "particules_scales.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Particules: the panel, saved settings and engine mode. The audio is done
// by the active engine adapter: ParticulesBeads (the Beads source) or the
// classic v2.1 engine.

// Output mute after a live engine switch, as beads.cc's codec_transition.
static constexpr float kModeSwitchMuteSeconds = 0.25f;

struct QualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};

Particules::Particules() {
	config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	// configSwitch does NOT disable randomization (only configButton does),
	// so FREEZE needs the explicit flag or Ctrl+R randomly latches freeze.
	configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"})
		->randomizeEnabled = false;
	configButton(SEED_PARAM, "Seed (hold 4 s to lock)");
#ifdef METAMODULE
	// MetaModule: a real 4-position switch so the firmware shows the mode
	// name (the adapter can't label a momentary button). Desktop keeps the
	// momentary cycle button below.
	configSwitch(QUALITY_PARAM, 0.f, 3.f, 0.f, "Quality",
		{"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"})
		->randomizeEnabled = false;
#else
	configButton<QualityParamQuantity>(QUALITY_PARAM, "Quality");
#endif
	configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density");
	configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Time");
	configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
	configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback");
	configParam(FEEDBACK_AMT_PARAM, -1.f, 1.f, 0.f, "Feedback CV amount");
	configParam(SIZE_PARAM, -1.f, 1.f, 0.f, "Size");
	configParam(SHAPE_PARAM, 0.f, 1.f, 0.5f, "Shape");
	configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/Wet");
	configParam(DRY_WET_AMT_PARAM, -1.f, 1.f, 0.f, "Dry/wet CV amount");
	configParam(REVERB_PARAM, 0.f, 1.f, 0.f, "Reverb");
	configParam(REVERB_AMT_PARAM, -1.f, 1.f, 0.f, "Reverb CV amount");
	configParam(TIME_AR_PARAM, -1.f, 1.f, 0.f, "Time AR");
	configParam(SIZE_AR_PARAM, -1.f, 1.f, 0.f, "Size AR");
	configParam(SHAPE_AR_PARAM, -1.f, 1.f, 0.f, "Shape AR");
	configParam(PITCH_AR_PARAM, -1.f, 1.f, 0.f, "Pitch AR");

	configInput(FEEDBACK_INPUT, "Feedback CV");
	configInput(DRY_WET_INPUT, "Dry/wet CV");
	configInput(REVERB_INPUT, "Reverb CV");
	configInput(TIME_INPUT, "Time CV");
	configInput(SIZE_INPUT, "Size CV");
	configInput(SHAPE_INPUT, "Shape CV");
	configInput(PITCH_INPUT, "Pitch CV (V/oct)");
	configInput(DENSITY_INPUT, "Density CV");
	configInput(IN_L_INPUT, "Audio in L");
	configInput(IN_R_INPUT, "Audio in R");
	configInput(FREEZE_INPUT, "Freeze gate");
	configInput(SEED_INPUT, "Seed/clock");
	configOutput(OUT_L_OUTPUT, "Audio out L");
	configOutput(OUT_R_OUTPUT, "Audio out R");
	configOutput(GATE_OUTPUT, "Grain gate");
	configBypass(IN_L_INPUT, OUT_L_OUTPUT);
	configBypass(IN_R_INPUT, OUT_R_OUTPUT);
	// configLight() is intentionally omitted: the lights are embedded in
	// button params (FREEZE, QUALITY), so their tooltips come from configParam.
	// Adding separate light labels would show a second overlapping tooltip.

	sample_rate_ = APP->engine->getSampleRate();
	classic_.store(false);
	engine_ = MakeParticulesBeads(*this);
	grain_feed_.Reset();   // the old engine's buffer is gone
	if (!engine_ || !engine_->ok()) {
		// WARN is defined identically by both hosts' logger.hpp.
		WARN("Particules: could not allocate the Beads delay memory");
	}
}

void Particules::requestMode(bool classic) {   // UI thread
	pending_mode_.store(kModeLive + (classic ? 1 : 0), std::memory_order_release);
}

void Particules::applyPendingMode(float sample_rate) {   // audio thread
	// Load, switch, store classic_, and only then clear the request, so the
	// UI never reads neither mode and a newer request is not lost.
	int pending = pending_mode_.load(std::memory_order_acquire);
	if (pending < 0) return;
	const bool want = (pending & 1) != 0;
	const bool forced = pending >= kModeForced;
	// A live switch is refused while frozen, like Quality. A load or undo
	// is not: the module must run the mode its JSON says. A refused or
	// redundant request is still cleared.
	if ((engine_ && want == classicMode()) || (engine_ && !forced && engine_->frozen())) {
		pending_mode_.compare_exchange_strong(pending, kModeNone, std::memory_order_acq_rel);
		return;
	}
	classic_.store(want, std::memory_order_relaxed);
	sample_rate_ = sample_rate;
	engine_.reset();   // free first: peak RAM is the larger engine, not both
	engine_ = want ? MakeParticulesClassic(*this) : MakeParticulesBeads(*this);
	if (!engine_ || !engine_->ok())
		WARN("Particules: could not allocate the %s engine", want ? "classic" : "Beads");
	scale_dirty_.store(true, std::memory_order_release);
	// A patch loaded straight into a mode switches before the first frame:
	// nothing has played, so there is nothing to mute.
	mute_seconds_ = processed_any_ ? kModeSwitchMuteSeconds : 0.f;
	pending_mode_.compare_exchange_strong(pending, kModeNone, std::memory_order_acq_rel);
}

void Particules::process(const ProcessArgs& args) {
	applyPendingMode(args.sampleRate);
	processed_any_ = true;
	// Every 256 samples: is a Lucarne attached on the right? dynamic_cast,
	// not model ==: MetaModule SDK < 2.4 leaves model unset on adapter modules.
	if (++expander_check_ >= 256) {
		expander_check_ = 0;
		grain_feed_.attached.store(dynamic_cast<Lucarne*>(rightExpander.module) != nullptr,
		                           std::memory_order_relaxed);
		grain_feed_.classic.store(classicMode(), std::memory_order_relaxed);
	}
	if (engine_) {
		engine_->process(args);
		ui_frozen_.store(engine_->frozen(), std::memory_order_relaxed);
		ui_clear_disabled_.store(engine_->clearDisabled(), std::memory_order_relaxed);
	} else {
		mute_seconds_ = std::max(mute_seconds_, args.sampleTime);   // no engine: silence
		ui_frozen_.store(false, std::memory_order_relaxed);
		ui_clear_disabled_.store(true, std::memory_order_relaxed);
	}
	if (mute_seconds_ > 0.f) {
		outputs[OUT_L_OUTPUT].setVoltage(0.f);
		outputs[OUT_R_OUTPUT].setVoltage(0.f);
		outputs[GATE_OUTPUT].setVoltage(0.f);
		mute_seconds_ -= args.sampleTime;
	}
}

void Particules::onSampleRateChange(const SampleRateChangeEvent& e) {
	sample_rate_ = e.sampleRate;
	if (engine_) engine_->onSampleRateChange(e.sampleRate);
}

void Particules::onReset(const ResetEvent& e) {
	Module::onReset(e);
	// Resets the settings, not the mode.
	quality_state_       = 0;
	seed_locked_         = true;
	classic_seed_mode_   = 0;
	pitch_scale_         = 0;
	pitch_root_          = 0;
	scale_dirty_.store(true, std::memory_order_release);
	tape_hiss_ = true;
	auto_gain_           = true;
	manual_gain_db_      = 0.f;
	recalibrate_requested_.store(true);
	clear_requested_.store(true);
	if (engine_) engine_->onReset();   // runs under Rack's engine lock
}

void Particules::onPortChange(const PortChangeEvent& e) {
	if (engine_) engine_->onPortChange(e);
}

json_t* Particules::dataToJson() {
	json_t* root = json_object();
	// A switch not yet applied is saved as the mode, so the menu's undo
	// snapshot records it.
	json_object_set_new(root, "classic",      json_boolean(displayedClassicMode()));
	json_object_set_new(root, "qualityState", json_integer(quality_state_));
	json_object_set_new(root, "seedState",    json_integer(seed_locked_ ? 0 : 1));
	json_object_set_new(root, "classicSeedMode", json_integer(classic_seed_mode_));
	json_object_set_new(root, "autoGain",     json_boolean(auto_gain_));
	json_object_set_new(root, "manualGainDb", json_real(manual_gain_db_));
	json_object_set_new(root, "pitchScale",      json_integer(pitch_scale_));
	json_object_set_new(root, "pitchRoot",       json_integer(pitch_root_));
	json_object_set_new(root, "tapeHiss",     json_boolean(tape_hiss_));
	return root;
}

void Particules::dataFromJson(json_t* root) {
	json_t* j;
	json_t* classic_j = json_object_get(root, "classic");
	const bool classic = robotboy::DetectClassic(
		classic_j != nullptr, json_is_true(classic_j),
		json_object_get(root, "tapeHiss") != nullptr,
		robotboy::ClassicFingerprint::kAbsentTapeHiss);

	// Engine-only settings go back to their defaults first, so a key that is
	// absent means "default" whatever the instance held before.
	classic_seed_mode_ = 0;
	seed_locked_ = true;
	tape_hiss_ = true;

	if ((j = json_object_get(root, "qualityState")))
		quality_state_ = clamp((int)json_integer_value(j), 0, 3);
	if (!classic_j && classic) {
		// A pre-2.2 patch: its "seedState" was v2.1's Seed CV mode
		// (0 = Triggers, 1 = Gates); the Beads seed keeps its default.
		if ((j = json_object_get(root, "seedState")))
			classic_seed_mode_ = clamp((int)json_integer_value(j), 0, 1);
		seed_locked_ = true;
	} else {
		if ((j = json_object_get(root, "classicSeedMode")))
			classic_seed_mode_ = clamp((int)json_integer_value(j), 0, 1);
		if ((j = json_object_get(root, "seedState")))
			seed_locked_ = json_integer_value(j) == 0;
	}
	if ((j = json_object_get(root, "autoGain")))
		auto_gain_ = json_boolean_value(j);
	if ((j = json_object_get(root, "manualGainDb")))
		manual_gain_db_ = clamp((float)json_real_value(j), 0.f, 32.f);
	if ((j = json_object_get(root, "pitchScale")))
		pitch_scale_ = clamp((int)json_integer_value(j), 0, 7);
	else if ((j = json_object_get(root, "pitchLock")))   // pre-scale patches
		pitch_scale_ = clamp((int)json_integer_value(j), 0, 2);
	// "grainTriggerOut" (the old Out-R grain-trigger option) is ignored:
	// the grain gate now has its own GATE output.
	if ((j = json_object_get(root, "pitchRoot")))
		pitch_root_ = clamp((int)json_integer_value(j), 0, 11);
	scale_dirty_.store(true, std::memory_order_release);
	if ((j = json_object_get(root, "tapeHiss")))
		tape_hiss_ = json_boolean_value(j);

	// The engine swap happens at the next process(), on the audio thread,
	// forced: a load or undo applies even while frozen.
	pending_mode_.store(kModeForced + (classic ? 1 : 0), std::memory_order_release);
}

std::string QualityParamQuantity::getDisplayValueString() {
	Particules* m = dynamic_cast<Particules*>(module);
	if (!m) return "";
	static const char* kNames[] = {"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"};
	return kNames[m->quality_state_];
}

#ifdef METAMODULE
// MetaModule-only Quality control. The VCV→MM adapter turns a non-momentary
// SvgSwitch with >=3 frames plus a configSwitch into a labeled FlipSwitch, so
// MM shows the mode name in the Adjust popup and the per-mode colour comes from
// the frame image (metamodule/assets/quality_*.png, reached via the .svg->.png
// asset rule). The desktop build keeps its momentary RGB bezel instead — see the
// #ifndef METAMODULE branch in ParticulesWidget and Particules(). The referenced
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

struct ParticulesWidget : ModuleWidget {
	void appendContextMenu(Menu* menu) override {
		Particules* module = dynamic_cast<Particules*>(this->module);
		if (!module) return;

		menu->addChild(new MenuSeparator);

		// --- Classic mode: the v2.1 engine ---
		// Refused while frozen, like Quality: a switch would drop the frozen
		// recording.
		{
			const bool frozen = module->ui_frozen_.load(std::memory_order_relaxed);
			menu->addChild(createBoolMenuItem("Classic mode",
				frozen ? "Unfreeze to switch" : "",
				[=]() { return module->displayedClassicMode(); },
				[=](bool on) {
					withMenuUndo(module, on ? "enable classic mode" : "disable classic mode",
						[=]() { module->requestMode(on); });
				},
				/*disabled=*/frozen
			));
		}

		// The engine's own items, for the mode the toggle shows. Built from
		// module state only: the UI thread never touches engine_.
		if (module->displayedClassicMode())
			AppendParticulesClassicMenu(module, menu);
		else
			AppendParticulesBeadsMenu(module, menu);

		// --- Pitch Lock ---
		menu->addChild(createIndexSubmenuItem("Lock pitch",
			{"Off", "Octaves", "Octaves + 5ths", "Chromatic", "Major", "Minor",
			 "Major pentatonic", "Minor pentatonic"},
			[=]() { return module->pitch_scale_; },
			[=](int val) {
				withMenuUndo(module, "change pitch lock", [=]() {
					module->pitch_scale_ = val;
					module->scale_dirty_.store(true, std::memory_order_release);
				});
			}
		));

		// --- Scale root (scale modes only) ---
		{
			auto* rootItem = createIndexSubmenuItem("Root",
				{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
				[=]() { return module->pitch_root_; },
				[=](int val) {
					withMenuUndo(module, "change scale root", [=]() {
						module->pitch_root_ = val;
						module->scale_dirty_.store(true, std::memory_order_release);
					});
				}
			);
			// Disabled state is computed at menu-open; picking a scale and
			// then Root needs a menu re-open on VCV. Acceptable.
			rootItem->disabled = module->pitch_scale_ < particules::kPitchChromatic;
			menu->addChild(rootItem);
		}

		// --- Clear Buffer ---
		// Deferred to the audio thread (process()) to avoid racing the DSP;
		// takes effect at the next block boundary.
		menu->addChild(createMenuItem("Clear buffer", "",
			[=]() { module->clear_requested_.store(true); },
			/*disabled=*/module->ui_clear_disabled_.load(std::memory_order_relaxed)
		));

	}

	ParticulesWidget(Particules* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Particules.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(20.4, 15.875)), module, Particules::FREEZE_PARAM, Particules::FREEZE_BUTTON_LIGHT));
#ifdef METAMODULE
		// MetaModule: 4-position colored switch (adapter renders it as a labeled
		// FlipSwitch). Desktop keeps the momentary RGB bezel below.
		addParam(createParamCentered<QualityMmSwitch>(mm2px(Vec(38.1, 15.875)), module, Particules::QUALITY_PARAM));
#else
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(38.1, 15.875)), module, Particules::QUALITY_PARAM, Particules::QUALITY_R_LIGHT));
#endif
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 42.088)), module, Particules::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 42.088)), module, Particules::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 42.088)), module, Particules::DENSITY_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 62.746)), module, Particules::TIME_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 62.746)), module, Particules::PITCH_AR_PARAM));

		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(29.6, 62.746)), module, Particules::GRAIN_LIGHT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 42.088)), module, Particules::SIZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 82.096)), module, Particules::SHAPE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 62.746)), module, Particules::SIZE_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 101.696)), module, Particules::SHAPE_AR_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 82.096)), module, Particules::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 82.096)), module, Particules::DRY_WET_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 82.096)), module, Particules::REVERB_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(29.6, 101.696)), module, Particules::FEEDBACK_AMT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 101.696)), module, Particules::DRY_WET_AMT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 101.696)), module, Particules::REVERB_AMT_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.9, 15.875)), module, Particules::FREEZE_INPUT));
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(55.8, 15.875)), module, Particules::SEED_PARAM, Particules::SEED_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(64.3, 15.875)), module, Particules::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 53.746)), module, Particules::TIME_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 53.746)), module, Particules::PITCH_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 53.746)), module, Particules::DENSITY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 53.746)), module, Particules::SIZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 92.696)), module, Particules::SHAPE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 92.696)), module, Particules::FEEDBACK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 92.696)), module, Particules::DRY_WET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 92.696)), module, Particules::REVERB_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.25, 114.3)), module, Particules::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.95, 114.3)), module, Particules::IN_R_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.25, 114.3)), module, Particules::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(60.95, 114.3)), module, Particules::OUT_R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.1f, 114.3f)), module, Particules::GATE_OUTPUT));
	}
};


Model* modelParticules = createModel<Particules, ParticulesWidget>("Particules");
