#include "plugin.hpp"
#include "beads_menu.hpp"
#include "metamodule_fpu.h"

#include "host/controls.h"
#include "host/engine_host.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Retours is the delay mode of Mutable Instruments Beads (the mode Beads
// enters with Size fully clockwise), running on the Beads source
// (src/beads). Interval is Beads' Density knob in that mode, Slice is its
// Freeze, and Clock is its Seed button and input.

// CLOCK_LIGHT flash decay time. The beat flash snaps to full brightness, then
// decays over this long so each pulse spans many video frames and reads evenly.
static constexpr float kClockFlashDecaySeconds = 0.08f;

struct Retours;

struct RetoursQualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};

struct Retours : Module, robotboy_beads::ControlSource {
	enum ParamId {
		SLICE_PARAM,
		QUALITY_PARAM,
		CLOCK_PARAM,
		TIME_PARAM,
		INTERVAL_PARAM,
		PITCH_PARAM,
		SHAPE_PARAM,
		FEEDBACK_PARAM,
		DRY_WET_PARAM,
		TIME_AR_PARAM,
		PITCH_AR_PARAM,
		SHAPE_AR_PARAM,
		FEEDBACK_AR_PARAM,
		DRY_WET_AR_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		SLICE_INPUT,
		CLOCK_INPUT,
		TIME_CV_INPUT,
		INTERVAL_CV_INPUT,
		PITCH_CV_INPUT,
		SHAPE_CV_INPUT,
		FEEDBACK_CV_INPUT,
		DRY_WET_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		QUALITY_R_LIGHT,
		QUALITY_G_LIGHT,
		QUALITY_B_LIGHT,
		SLICE_BUTTON_LIGHT,
		CLOCK_LIGHT,
		LIGHTS_LEN
	};

	robotboy_beads::EngineHost host_;

	// Beads' pot and CV conditioning (cv_reader.cc), run once per engine block.
	robotboy_beads::PotChannel time_pot_, pitch_pot_, shape_pot_, interval_pot_;
	robotboy_beads::PotChannel time_ar_pot_, pitch_ar_pot_, shape_ar_pot_;
	robotboy_beads::CvChannel time_cv_, pitch_cv_, shape_cv_, interval_cv_;
	robotboy_beads::CvChannel feedback_cv_, dry_wet_cv_;

	// Module state
	int quality_state_ = 0;   // 0-3, same encoding/colors as Particules
	bool prev_quality_button_ = false;
	dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on SLICE gate
	dsp::SchmittTrigger clock_gate_;     // same for CLOCK jack
	bool prev_clock_button_ = false;
	bool metamodule_fpu_configured_ = false;

	// Tempo. Beads starts with its Seed button "locked": no tempo, Interval
	// sets the time directly. A tap (or a clock tick) unlocks it, and the
	// tempo then holds, even after the clock is unpatched, until "Clear saved
	// tempo" locks it again. Taps and ticks are latched per sample and
	// consumed once per engine block.
	bool tempo_held_ = false;
	bool tap_pending_ = false;
	bool clock_tick_pending_ = false;
	std::atomic<bool> clear_tempo_requested_{false};
	std::atomic<bool> clear_requested_{false};

	// Input gain (Beads' auto gain by default), shared menu with Particules.
	std::atomic<bool> recalibrate_requested_{false};
	bool auto_gain_ = true;
	bool tape_hiss_ = true;   // Scorched cassette's synthetic hiss
	float manual_gain_db_ = 0.f;
	float input_peak_ = 0.f;

	bool prev_gate_ = false;
	float clock_light_level_ = 0.f;
	int light_divider_ = 0;
	int light_quality_state_ = -1;

	Retours() {
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

		configInput(IN_L_INPUT, "Audio in L");
		configInput(IN_R_INPUT, "Audio in R");
		configInput(INTERVAL_CV_INPUT, "Interval CV");
		configInput(TIME_CV_INPUT, "Time CV");
		configInput(PITCH_CV_INPUT, "Pitch CV");
		configInput(SHAPE_CV_INPUT, "Shape CV");
		configInput(FEEDBACK_CV_INPUT, "Feedback CV");
		configInput(DRY_WET_CV_INPUT, "Dry/wet CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(SLICE_INPUT, "Slice gate");
		configOutput(OUT_L_OUTPUT, "Audio out L");
		configOutput(OUT_R_OUTPUT, "Audio out R");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// SLICE and QUALITY lights are embedded in their button params (tooltips
		// come from configParam); CLOCK_LIGHT is a standalone beat indicator.
		configLight(CLOCK_LIGHT, "Clock");

		using robotboy_beads::PotLaw;
		const float lp = robotboy_beads::kPotLp;
		time_pot_.Init(PotLaw::kNormal, lp, robotboy_beads::kTimeHysteresis);
		interval_pot_.Init(PotLaw::kNormal, lp, 0.f);
		shape_pot_.Init(PotLaw::kNormal, lp, 0.f);
		pitch_pot_.Init(PotLaw::kDetune, lp, 0.f);
		time_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
		pitch_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
		shape_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
		robotboy_beads::InitTimeSizeShapeCv(&time_cv_);
		robotboy_beads::InitTimeSizeShapeCv(&shape_cv_);
		robotboy_beads::InitPitchCv(&pitch_cv_);
		robotboy_beads::InitDensityCv(&interval_cv_);
		robotboy_beads::InitMixCv(&feedback_cv_);
		robotboy_beads::InitMixCv(&dry_wet_cv_);

		// Delay mode only; Retours has no Reverb control.
		if (!host_.Init(beads::Processor::MODE_DELAY, false,
		                APP->engine->getSampleRate())) {
			WARN("Retours: could not allocate the Beads delay memory");
		}
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		host_.SetHostRate(e.sampleRate);
		metamodule_fpu_configured_ = false;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		quality_state_ = 0;
		auto_gain_ = true;
		manual_gain_db_ = 0.f;
		tape_hiss_ = true;
		clear_requested_.store(true);
		clear_tempo_requested_.store(true);   // initialize returns to free-running
		recalibrate_requested_.store(true);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "qualityState", json_integer(quality_state_));
		json_object_set_new(root, "autoGain",     json_boolean(auto_gain_));
		json_object_set_new(root, "manualGainDb", json_real(manual_gain_db_));
		json_object_set_new(root, "tapeHiss",     json_boolean(tape_hiss_));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
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
		// are ignored: Beads has one fixed time-change behaviour.
	}

	static beads::RandomizedParameter Randomized(float base, float attenuverter,
	                                             bool has_cv, float cv) {
		beads::RandomizedParameter r;
		r.base = base;
		r.attenuverter = attenuverter;
		r.has_cv = has_cv;
		r.cv = cv;
		return r;
	}

	// Called by the engine host at the start of every Beads block.
	void ReadControls(robotboy_beads::BlockControls* c) override {
		if (clear_requested_.exchange(false))
			host_.RequestClear();
		if (recalibrate_requested_.exchange(false))
			host_.RecalibrateGain();
		if (clear_tempo_requested_.exchange(false))
			tempo_held_ = false;

		auto attenuverter = [](float x) { return (x + 1.f) * 0.5f; };
		auto volts = [this](int input) { return inputs[input].getVoltage(); };

		const bool time_cv = inputs[TIME_CV_INPUT].isConnected();
		const bool pitch_cv = inputs[PITCH_CV_INPUT].isConnected();
		const bool shape_cv = inputs[SHAPE_CV_INPUT].isConnected();

		c->parameters.time = Randomized(
			time_pot_.Process(params[TIME_PARAM].getValue()),
			time_ar_pot_.Process(attenuverter(params[TIME_AR_PARAM].getValue())),
			time_cv, time_cv_.Process(volts(TIME_CV_INPUT) * 0.2f));
		c->parameters.pitch = Randomized(
			pitch_pot_.Process(params[PITCH_PARAM].getValue()),
			pitch_ar_pot_.Process(attenuverter(params[PITCH_AR_PARAM].getValue())),
			pitch_cv, pitch_cv_.Process(volts(PITCH_CV_INPUT) * 12.f));
		c->parameters.shape = Randomized(
			shape_pot_.Process(params[SHAPE_PARAM].getValue()),
			shape_ar_pot_.Process(attenuverter(params[SHAPE_AR_PARAM].getValue())),
			shape_cv, shape_cv_.Process(volts(SHAPE_CV_INPUT) * 0.2f));
		// In delay mode Beads' Size attenurandomizer sets a bandwidth filter
		// on the repeats; Retours has no control for it, so it stays at noon
		// (filter off). Size itself only chooses the mode, pinned here.
		c->parameters.size = Randomized(1.f, 0.f, false, 0.f);
		c->parameters.monophonic_mode = false;
		c->parameters.pitch_lock = nullptr;

		c->density.base = interval_pot_.Process(params[INTERVAL_PARAM].getValue());
		c->density.cv = inputs[INTERVAL_CV_INPUT].isConnected()
			? interval_cv_.Process(volts(INTERVAL_CV_INPUT) * 0.2f) : 0.f;

		c->mix.feedback = robotboy_beads::MixParameter(params[FEEDBACK_PARAM].getValue(),
			feedback_cv_.Process(volts(FEEDBACK_CV_INPUT) * 0.2f), params[FEEDBACK_AR_PARAM].getValue());
		c->mix.dry_wet = robotboy_beads::MixParameter(params[DRY_WET_PARAM].getValue(),
			dry_wet_cv_.Process(volts(DRY_WET_CV_INPUT) * 0.2f), params[DRY_WET_AR_PARAM].getValue());
		c->mix.reverb_amount = 0.f;
		c->mix.output_gate = false;

		// Clock = Beads' Seed. A tap is the Seed button pressed for one block
		// (the first press from the locked state only unlocks, as on Beads).
		// A patched clock goes to the Seed input; its first tick unlocks too,
		// so the measured tempo survives unpatching.
		const bool clock_patched = inputs[CLOCK_INPUT].isConnected();
		if (clock_tick_pending_ && clock_patched)
			tempo_held_ = true;
		c->seed_patched = clock_patched;
		// While the engine is muted after a reformat it isn't listening, so a
		// tap waits for it.
		if (tap_pending_ && !host_.muted()) {
			c->seed_button = beads::SEED_BUTTON_STATE_ON;
			tempo_held_ = true;
			tap_pending_ = false;
		} else {
			c->seed_button = tempo_held_
				? beads::SEED_BUTTON_STATE_OFF
				: beads::SEED_BUTTON_STATE_LOCKED;
		}
		clock_tick_pending_ = false;

		c->gain_setting = auto_gain_ ? 0 : BeadsManualGainByte(manual_gain_db_);
	}

	void process(const ProcessArgs& args) override {
		if (!metamodule_fpu_configured_) {
			metamodule_fpu_configured_ = true;
			particules::EnableMetaModuleFlushToZero();
		}
		bool freeze_latch = params[SLICE_PARAM].getValue() > 0.5f;
		freeze_gate_.process(inputs[SLICE_INPUT].getVoltage(), 0.1f, 1.f);
		bool frozen = freeze_latch || freeze_gate_.isHigh();

		clock_gate_.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		if (clock_gate_.isHigh())
			clock_tick_pending_ = true;
		bool clock_button = params[CLOCK_PARAM].getValue() > 0.5f;
		if (clock_button && !prev_clock_button_)
			tap_pending_ = true;
		prev_clock_button_ = clock_button;

#ifdef METAMODULE
		// MetaModule: Quality is a 4-position switch, so the mode IS the param.
		quality_state_ = std::clamp((int)(params[QUALITY_PARAM].getValue() + 0.5f), 0, 3);
#else
		// Desktop: momentary button cycles the mode; blocked while sliced.
		bool quality_pressed = params[QUALITY_PARAM].getValue() > 0.5f;
		if (quality_pressed && !prev_quality_button_ && !frozen)
			quality_state_ = (quality_state_ + 1) % 4;
		prev_quality_button_ = quality_pressed;
#endif
		host_.SetQuality(static_cast<robotboy_beads::Quality>(quality_state_));
		host_.SetHissEnabled(tape_hiss_);
		host_.SetInputs(inputs[IN_L_INPUT].isConnected(), inputs[IN_R_INPUT].isConnected());

		const float l = inputs[IN_L_INPUT].getVoltage() * 0.2f;
		const float r = inputs[IN_R_INPUT].getVoltage() * 0.2f;
		if (std::isfinite(l) && std::isfinite(r))
			input_peak_ = std::max(std::max(std::fabs(l), std::fabs(r)),
			                       input_peak_ * (1.f - 2.f * args.sampleTime));

		const robotboy_beads::HostFrameOut out =
			host_.Process({l, r, frozen, clock_gate_.isHigh()}, this);

		bool r_connected = outputs[OUT_R_OUTPUT].isConnected();
		outputs[OUT_L_OUTPUT].setVoltage((r_connected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
		outputs[OUT_R_OUTPUT].setVoltage(out.r * 5.f);

		// Clock light: flashes at the start of each repeat (Beads' tap gate).
		if (out.gate && !prev_gate_)
			clock_light_level_ = 1.f;
		prev_gate_ = out.gate;
		clock_light_level_ = std::max(0.f,
			clock_light_level_ - args.sampleTime / kClockFlashDecaySeconds);

		if (++light_divider_ >= 32) {
			light_divider_ = 0;
			lights[CLOCK_LIGHT].setBrightness(clock_light_level_);
			lights[SLICE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);
			if (quality_state_ != light_quality_state_) {
				light_quality_state_ = quality_state_;
				lights[QUALITY_R_LIGHT].setBrightness(kQualityColors[quality_state_][0]);
				lights[QUALITY_G_LIGHT].setBrightness(kQualityColors[quality_state_][1]);
				lights[QUALITY_B_LIGHT].setBrightness(kQualityColors[quality_state_][2]);
			}
		}
	}
};

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

		appendBeadsGainMenu(menu, module);
		appendBeadsHissMenu(menu, module);

		// --- Clear saved tempo ---
		// A tempo from a tap or a clock holds after the clock is unpatched;
		// this returns to free-running. Greyed out when there is nothing to
		// clear, or while a clock is patched (it would set the tempo again).
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear saved tempo", "",
			[=]() { module->clear_tempo_requested_.store(true); },
			/*disabled=*/!module->tempo_held_ || module->inputs[Retours::CLOCK_INPUT].isConnected()
		));

		// --- Clear Buffer ---
		// Deferred to the audio thread (process()) to avoid racing the DSP;
		// takes effect at the next block boundary.
		menu->addChild(createMenuItem("Clear buffer", "",
			[=]() { module->clear_requested_.store(true); },
			/*disabled=*/module->host_.buffer_empty()
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
		// carried verbatim into these mm2px(Vec(...)) calls.
		// --- Top row: slice jack/button, quality, tap-tempo button, clock jack ---
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.940f, 15.875f)), module, Retours::SLICE_INPUT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(15.940f, 15.875f)), module, Retours::SLICE_PARAM, Retours::SLICE_BUTTON_LIGHT));
#ifdef METAMODULE
		addParam(createParamCentered<QualityMmSwitch>(mm2px(Vec(27.940f, 15.875f)), module, Retours::QUALITY_PARAM));
#else
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(27.940f, 15.875f)), module, Retours::QUALITY_PARAM, Retours::QUALITY_R_LIGHT));
#endif
		addParam(createParamCentered<VCVButton>(mm2px(Vec(39.940f, 15.875f)), module, Retours::CLOCK_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.940f, 15.875f)), module, Retours::CLOCK_INPUT));

		// --- Primary knobs, CV inputs, attenuverters (Interval has no attenuverter;
		// the clock-tick light sits in that column) ---
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.340f, 42.088f)), module, Retours::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(27.940f, 42.088f)), module, Retours::INTERVAL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(44.540f, 42.088f)), module, Retours::PITCH_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.340f, 53.217f)), module, Retours::TIME_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.940f, 53.217f)), module, Retours::INTERVAL_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(44.540f, 53.217f)), module, Retours::PITCH_CV_INPUT));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.340f, 62.219f)), module, Retours::TIME_AR_PARAM));
		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(27.940f, 62.219f)), module, Retours::CLOCK_LIGHT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.540f, 62.219f)), module, Retours::PITCH_AR_PARAM));

		// --- Secondary knobs, CV inputs, attenuverters ---
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.340f, 82.096f)), module, Retours::SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(27.940f, 82.096f)), module, Retours::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(44.540f, 82.096f)), module, Retours::DRY_WET_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.340f, 93.225f)), module, Retours::SHAPE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.940f, 93.225f)), module, Retours::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(44.540f, 93.225f)), module, Retours::DRY_WET_CV_INPUT));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.340f, 102.227f)), module, Retours::SHAPE_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(27.940f, 102.227f)), module, Retours::FEEDBACK_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.540f, 102.227f)), module, Retours::DRY_WET_AR_PARAM));

		// --- Audio I/O ---
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.238f, 114.300f)), module, Retours::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(18.938f, 114.300f)), module, Retours::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(36.942f, 114.300f)), module, Retours::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(46.642f, 114.300f)), module, Retours::OUT_R_OUTPUT));
	}
};


Model* modelRetours = createModel<Retours, RetoursWidget>("Retours");
