#include "plugin.hpp"
#include "beads_menu.hpp"
#include "particules_scales.h"
#include "metamodule_fpu.h"

#include "host/controls.h"
#include "host/engine_host.h"
#include "host/pitch_lock.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Particules is the granular half of Mutable Instruments Beads, running on
// the Beads source (src/beads). This file maps the panel onto what the
// Beads firmware reads from its pots and jacks each block (beads.cc).

// Seconds for the grain LED to fade after a flash.
static constexpr float kGrainLedDecaySeconds = 0.08f;

struct Particules;

struct QualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};

struct Particules : Module, robotboy_beads::ControlSource {
	enum ParamId {
		FREEZE_PARAM,
		QUALITY_PARAM,
		TIME_PARAM,
		DENSITY_PARAM,
		PITCH_PARAM,
		SIZE_PARAM,
		SHAPE_PARAM,
		FEEDBACK_PARAM,
		REVERB_PARAM,
		DRY_WET_PARAM,
		TIME_AR_PARAM,
		PITCH_AR_PARAM,
		SIZE_AR_PARAM,
		SHAPE_AR_PARAM,
		FEEDBACK_AMT_PARAM,
		REVERB_AMT_PARAM,
		DRY_WET_AMT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		FREEZE_INPUT,
		SEED_INPUT,
		TIME_INPUT,
		DENSITY_INPUT,
		PITCH_INPUT,
		SIZE_INPUT,
		SHAPE_INPUT,
		FEEDBACK_INPUT,
		REVERB_INPUT,
		DRY_WET_INPUT,
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
		FREEZE_BUTTON_LIGHT,
		GRAIN_LIGHT,
		LIGHTS_LEN
	};

	robotboy_beads::EngineHost host_;
	robotboy_beads::ScalePitchLock pitch_lock_;

	// Beads' pot and CV conditioning (cv_reader.cc), run once per engine block.
	robotboy_beads::PotChannel time_pot_, size_pot_, shape_pot_, pitch_pot_, density_pot_;
	robotboy_beads::PotChannel time_ar_pot_, size_ar_pot_, shape_ar_pot_, pitch_ar_pot_;
	robotboy_beads::CvChannel time_cv_, size_cv_, shape_cv_, pitch_cv_, density_cv_;

	// Module state
	int quality_state_ = 0;   // 0-3: Bright, Cold, Sunny, Scorched
	int seed_state_ = 0;      // 0=Triggers, 1=Gates
	bool prev_quality_button_ = false;
	dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on FREEZE gate
	dsp::SchmittTrigger seed_gate_;     // same for SEED
	// Lock pitch: 0 = off, 1-2 = octaves (+ fifths), 3-7 = scales (see
	// particules_scales.h); the root applies to the scale modes only.
	int pitch_scale_ = 0;
	int pitch_root_ = 0;
	// Menu/load/reset mutate this state on the UI thread; the audio thread
	// applies it at the next engine block.
	std::atomic<bool> scale_dirty_{true};
	std::atomic<bool> clear_requested_{false};
	std::atomic<bool> recalibrate_requested_{false};
	bool grain_trigger_out_ = false;
	bool auto_gain_ = true;
	float manual_gain_db_ = 0.f;
	bool metamodule_fpu_configured_ = false;

	// Grain LED and the input-level readout.
	bool prev_gate_ = false;
	float grain_led_ = 0.f;
	float input_peak_ = 0.f;
	int light_divider_ = 0;
	int light_quality_state_ = -1;

	Particules() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// configSwitch does NOT disable randomization (only configButton does),
		// so FREEZE needs the explicit flag or Ctrl+R randomly latches freeze.
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"})
			->randomizeEnabled = false;
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
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// configLight() is intentionally omitted: the lights are embedded in
		// button params (FREEZE, QUALITY), so their tooltips come from configParam.
		// Adding separate light labels would show a second overlapping tooltip.

		InitControlChannels();
		if (!host_.Init(beads::Processor::MODE_GRANULAR, true,
		                APP->engine->getSampleRate())) {
			// WARN is defined identically by both hosts' logger.hpp.
			WARN("Particules: could not allocate the Beads delay memory");
		}
	}

	void InitControlChannels() {
		using robotboy_beads::PotLaw;
		const float lp = robotboy_beads::kPotLp;
		time_pot_.Init(PotLaw::kNormal, lp, robotboy_beads::kTimeHysteresis);
		size_pot_.Init(PotLaw::kNormal, lp, 0.f);
		shape_pot_.Init(PotLaw::kNormal, lp, 0.f);
		density_pot_.Init(PotLaw::kNormal, lp, 0.f);
		pitch_pot_.Init(PotLaw::kDetune, lp, 0.f);
		time_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
		size_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
		shape_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
		pitch_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
		robotboy_beads::InitTimeSizeShapeCv(&time_cv_);
		robotboy_beads::InitTimeSizeShapeCv(&size_cv_);
		robotboy_beads::InitTimeSizeShapeCv(&shape_cv_);
		robotboy_beads::InitPitchCv(&pitch_cv_);
		robotboy_beads::InitDensityCv(&density_cv_);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		host_.SetHostRate(e.sampleRate);
		// Re-arm so a possibly-new audio thread gets FTZ configured.
		metamodule_fpu_configured_ = false;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		quality_state_       = 0;
		seed_state_          = 0;
		pitch_scale_         = 0;
		pitch_root_          = 0;
		scale_dirty_.store(true, std::memory_order_release);
		grain_trigger_out_   = false;
		auto_gain_           = true;
		manual_gain_db_      = 0.f;
		recalibrate_requested_.store(true);
		clear_requested_.store(true);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "qualityState", json_integer(quality_state_));
		json_object_set_new(root, "seedState",    json_integer(seed_state_));
		json_object_set_new(root, "autoGain",     json_boolean(auto_gain_));
		json_object_set_new(root, "manualGainDb", json_real(manual_gain_db_));
		json_object_set_new(root, "pitchScale",      json_integer(pitch_scale_));
		json_object_set_new(root, "pitchRoot",       json_integer(pitch_root_));
		json_object_set_new(root, "grainTriggerOut", json_boolean(grain_trigger_out_));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "qualityState")))
			quality_state_ = clamp((int)json_integer_value(j), 0, 3);
		if ((j = json_object_get(root, "seedState")))
			seed_state_ = clamp((int)json_integer_value(j), 0, 1);
		if ((j = json_object_get(root, "autoGain")))
			auto_gain_ = json_boolean_value(j);
		if ((j = json_object_get(root, "manualGainDb")))
			manual_gain_db_ = clamp((float)json_real_value(j), 0.f, 32.f);
		if ((j = json_object_get(root, "pitchScale")))
			pitch_scale_ = clamp((int)json_integer_value(j), 0, 7);
		else if ((j = json_object_get(root, "pitchLock")))   // pre-scale patches
			pitch_scale_ = clamp((int)json_integer_value(j), 0, 2);
		if ((j = json_object_get(root, "pitchRoot")))
			pitch_root_ = clamp((int)json_integer_value(j), 0, 11);
		scale_dirty_.store(true, std::memory_order_release);
		if ((j = json_object_get(root, "grainTriggerOut")))
			grain_trigger_out_ = json_boolean_value(j);
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
		if (scale_dirty_.exchange(false, std::memory_order_acquire))
			pitch_lock_.Set(pitch_scale_, pitch_root_);
		if (clear_requested_.exchange(false))
			host_.RequestClear();
		if (recalibrate_requested_.exchange(false))
			host_.RecalibrateGain();

		auto attenuverter = [](float x) { return (x + 1.f) * 0.5f; };
		auto volts = [this](int input) { return inputs[input].getVoltage(); };

		const bool time_cv = inputs[TIME_INPUT].isConnected();
		const bool size_cv = inputs[SIZE_INPUT].isConnected();
		const bool shape_cv = inputs[SHAPE_INPUT].isConnected();
		const bool pitch_cv = inputs[PITCH_INPUT].isConnected();

		c->parameters.time = Randomized(
			time_pot_.Process(params[TIME_PARAM].getValue()),
			time_ar_pot_.Process(attenuverter(params[TIME_AR_PARAM].getValue())),
			time_cv, time_cv_.Process(volts(TIME_INPUT) * 0.2f));
		// The panel's Size knob runs -1..1; Beads' runs 0..1.
		c->parameters.size = Randomized(
			size_pot_.Process(attenuverter(params[SIZE_PARAM].getValue())),
			size_ar_pot_.Process(attenuverter(params[SIZE_AR_PARAM].getValue())),
			size_cv, size_cv_.Process(volts(SIZE_INPUT) * 0.2f));
		c->parameters.shape = Randomized(
			shape_pot_.Process(params[SHAPE_PARAM].getValue()),
			shape_ar_pot_.Process(attenuverter(params[SHAPE_AR_PARAM].getValue())),
			shape_cv, shape_cv_.Process(volts(SHAPE_INPUT) * 0.2f));
		c->parameters.pitch = Randomized(
			pitch_pot_.Process(params[PITCH_PARAM].getValue()),
			pitch_ar_pot_.Process(attenuverter(params[PITCH_AR_PARAM].getValue())),
			pitch_cv, pitch_cv_.Process(volts(PITCH_INPUT) * 12.f));
		c->parameters.monophonic_mode = false;
		c->parameters.pitch_lock = pitch_lock_.hook();

		c->density.base = density_pot_.Process(params[DENSITY_PARAM].getValue());
		// beads.cc zeroes an unpatched Density CV to ignore ADC noise.
		c->density.cv = inputs[DENSITY_INPUT].isConnected()
			? density_cv_.Process(volts(DENSITY_INPUT) * 0.2f) : 0.f;

		c->mix.feedback = robotboy_beads::MixParameter(params[FEEDBACK_PARAM].getValue(),
			volts(FEEDBACK_INPUT), params[FEEDBACK_AMT_PARAM].getValue());
		c->mix.dry_wet = robotboy_beads::MixParameter(params[DRY_WET_PARAM].getValue(),
			volts(DRY_WET_INPUT), params[DRY_WET_AMT_PARAM].getValue());
		c->mix.reverb_amount = robotboy_beads::MixParameter(params[REVERB_PARAM].getValue(),
			volts(REVERB_INPUT), params[REVERB_AMT_PARAM].getValue());
		c->mix.output_gate = false;

		// Particules has no Seed button. "Triggers" is Beads with its Seed
		// button locked (a patched Seed clocks a divider/probability gate);
		// "Gates" is Beads unlocked (grains repeat while the gate is high).
		// Unpatched, both behave as locked: Density alone, silent at noon.
		const bool seed_patched = inputs[SEED_INPUT].isConnected();
		c->seed_patched = seed_patched;
		c->seed_button = (seed_state_ == 1 && seed_patched)
			? beads::SEED_BUTTON_STATE_OFF
			: beads::SEED_BUTTON_STATE_LOCKED;

		c->gain_setting = auto_gain_ ? 0 : BeadsManualGainByte(manual_gain_db_);
	}

	void process(const ProcessArgs& args) override {
		if (!metamodule_fpu_configured_) {
			metamodule_fpu_configured_ = true;
			particules::EnableMetaModuleFlushToZero();
		}
		bool freeze_button = params[FREEZE_PARAM].getValue() > 0.5f;
		freeze_gate_.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
		bool frozen = freeze_button || freeze_gate_.isHigh();
		seed_gate_.process(inputs[SEED_INPUT].getVoltage(), 0.1f, 1.f);

#ifdef METAMODULE
		// MetaModule: Quality is a 4-position switch, so the mode IS the param.
		quality_state_ = std::clamp((int)(params[QUALITY_PARAM].getValue() + 0.5f), 0, 3);
#else
		// Desktop: momentary button cycles the mode; blocked while frozen.
		bool quality_pressed = params[QUALITY_PARAM].getValue() > 0.5f;
		if (quality_pressed && !prev_quality_button_ && !frozen)
			quality_state_ = (quality_state_ + 1) % 4;
		prev_quality_button_ = quality_pressed;
#endif
		host_.SetQuality(static_cast<robotboy_beads::Quality>(quality_state_));

		const bool in_l = inputs[IN_L_INPUT].isConnected();
		const bool in_r = inputs[IN_R_INPUT].isConnected();
		host_.SetInputs(in_l, in_r);

		const float l = inputs[IN_L_INPUT].getVoltage() * 0.2f;
		const float r = inputs[IN_R_INPUT].getVoltage() * 0.2f;
		if (std::isfinite(l) && std::isfinite(r))
			input_peak_ = std::max(std::max(std::fabs(l), std::fabs(r)),
			                       input_peak_ * (1.f - 2.f * args.sampleTime));

		const robotboy_beads::HostFrameOut out =
			host_.Process({l, r, frozen, seed_gate_.isHigh()}, this);

		if (grain_trigger_out_) {
			// Beads' gate-output mode: mono mix on L, grain gate on R.
			outputs[OUT_L_OUTPUT].setVoltage((out.l + out.r) * 0.5f * 5.f);
			outputs[OUT_R_OUTPUT].setVoltage(out.gate ? 10.f : 0.f);
		} else {
			bool r_connected = outputs[OUT_R_OUTPUT].isConnected();
			outputs[OUT_L_OUTPUT].setVoltage((r_connected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
			outputs[OUT_R_OUTPUT].setVoltage(out.r * 5.f);
		}

		// Grain LED: flash on each grain, brighter the more grains sound.
		if (out.gate && !prev_gate_)
			grain_led_ = std::min(1.f, 0.4f + 0.1f * host_.active_grains());
		prev_gate_ = out.gate;
		grain_led_ = std::max(0.f, grain_led_ - args.sampleTime / kGrainLedDecaySeconds);

		if (++light_divider_ >= 32) {
			light_divider_ = 0;
			lights[GRAIN_LIGHT].setBrightness(grain_led_);
			lights[FREEZE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);
			if (quality_state_ != light_quality_state_) {
				light_quality_state_ = quality_state_;
				lights[QUALITY_R_LIGHT].setBrightness(kQualityColors[quality_state_][0]);
				lights[QUALITY_G_LIGHT].setBrightness(kQualityColors[quality_state_][1]);
				lights[QUALITY_B_LIGHT].setBrightness(kQualityColors[quality_state_][2]);
			}
		}
	}
};

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

		appendBeadsGainMenu(menu, module);

		// --- SEED CV Mode ---
		menu->addChild(createIndexSubmenuItem("SEED CV mode",
			{"Triggers", "Gates"},
			[=]() { return module->seed_state_; },
			[=](int val) {
				withMenuUndo(module, "change SEED CV mode",
					[=]() { module->seed_state_ = val; });
			}
		));

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

		// --- Grain Trigger Output ---
		menu->addChild(createBoolMenuItem("Grain trigger on R Out", "",
			[=]() { return module->grain_trigger_out_; },
			[=](bool val) {
				withMenuUndo(module, "toggle grain trigger output",
					[=]() { module->grain_trigger_out_ = val; });
			}
		));

		// --- Clear Buffer ---
		// Deferred to the audio thread (process()) to avoid racing the DSP;
		// takes effect at the next block boundary.
		menu->addChild(createMenuItem("Clear buffer", "",
			[=]() { module->clear_requested_.store(true); },
			/*disabled=*/module->host_.buffer_empty()
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
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(59.845, 15.875)), module, Particules::SEED_INPUT));
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
	}
};


Model* modelParticules = createModel<Particules, ParticulesWidget>("Particules");
