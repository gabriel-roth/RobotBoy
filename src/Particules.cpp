#include "BeadsModule.hpp"
#include "host/pitch_lock.h"

// Particules: the granular mode of Mutable Instruments Beads. The panel is
// Beads' own, with a CV input and trimpot for Feedback, Dry/wet and Reverb
// where Beads has one assignable CV.

// Seconds for the grain light to fade after a flash.
static constexpr float kGrainLightDecay = 0.08f;
// Beads' Seed button (ui.cc): holding it this long locks it again.
static constexpr float kSeedLongPress = 4.f;
// Period of the locked Seed light's slow pulse (ui.cc: a 2048 ms triangle).
static constexpr float kSeedPulsePeriod = 2.048f;

struct Particules : BeadsModule {
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
		FEEDBACK_CV_PARAM,
		REVERB_CV_PARAM,
		DRY_WET_CV_PARAM,
		SEED_PARAM,   // added later; appended so older patches still load
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
		GATE_OUTPUT,   // added later; appended so older patches still load
		OUTPUTS_LEN
	};
	enum LightId {
		QUALITY_R_LIGHT,
		QUALITY_G_LIGHT,
		QUALITY_B_LIGHT,
		FREEZE_LIGHT,
		GRAIN_LIGHT,
		SEED_LIGHT,
		LIGHTS_LEN
	};

	// Beads' Seed button. Locked (the default) is Beads' normal state:
	// Density alone makes grains, and a patched Seed input is a clock to
	// divide or a trigger to gate by probability. A press unlocks it and
	// makes grains while held (with Seed patched, while either is high); a
	// 4-second hold locks it again. Saved as "seedState": 0 = locked, 1 =
	// unlocked.
	bool seedLocked = true;
	bool seedHeld = false;   // pressed, and not yet held long enough to lock
	float seedHoldTime = 0.f;
	float seedPulsePhase = 0.f;
	dsp::BooleanTrigger seedButton;

	// Lock pitch: a robotboy_beads::PitchLockMode (0 = off, 1-2 = octaves
	// (+ fifths), 3-7 = scales); the root applies to the scale modes only. The
	// menu sets scaleChanged; the next engine block applies the change.
	int pitchScale = 0;
	int pitchRoot = 0;
	std::atomic<bool> scaleChanged{true};
	robotboy_beads::ScalePitchLock pitchLock;

	// Beads' pot and CV conditioning (cv_reader.cc), once per engine block.
	robotboy_beads::RandomizedChannel time, size, shape, pitch;
	robotboy_beads::PotChannel densityPot;
	robotboy_beads::CvChannel densityCv, feedbackCv, dryWetCv, reverbCv;

	dsp::BooleanTrigger grainTrigger;
	float grainLight = 0.f;

	Particules() : BeadsModule(beads::Processor::MODE_GRANULAR) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// configSwitch doesn't disable randomization (only configButton
		// does), so without this Ctrl+R could latch Freeze.
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"})->randomizeEnabled = false;
		configQuality();
		configButton(SEED_PARAM, "Seed (hold 4 s to lock)");
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density");
		configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Time");
		configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
		configParam(SIZE_PARAM, -1.f, 1.f, 0.f, "Size");
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.5f, "Shape");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback");
		configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/wet");
		configParam(REVERB_PARAM, 0.f, 1.f, 0.f, "Reverb");
		configParam(TIME_AR_PARAM, -1.f, 1.f, 0.f, "Time attenurandomizer");
		configParam(SIZE_AR_PARAM, -1.f, 1.f, 0.f, "Size attenurandomizer");
		configParam(SHAPE_AR_PARAM, -1.f, 1.f, 0.f, "Shape attenurandomizer");
		configParam(PITCH_AR_PARAM, -1.f, 1.f, 0.f, "Pitch attenurandomizer");
		configParam(FEEDBACK_CV_PARAM, -1.f, 1.f, 0.f, "Feedback CV amount");
		configParam(DRY_WET_CV_PARAM, -1.f, 1.f, 0.f, "Dry/wet CV amount");
		configParam(REVERB_CV_PARAM, -1.f, 1.f, 0.f, "Reverb CV amount");

		configInput(IN_L_INPUT, "Audio in L");
		configInput(IN_R_INPUT, "Audio in R");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(SEED_INPUT, "Seed/clock");
		configInput(TIME_INPUT, "Time CV");
		configInput(DENSITY_INPUT, "Density CV");
		configInput(PITCH_INPUT, "1V/octave pitch");
		configInput(SIZE_INPUT, "Size CV");
		configInput(SHAPE_INPUT, "Shape CV");
		configInput(FEEDBACK_INPUT, "Feedback CV");
		configInput(REVERB_INPUT, "Reverb CV");
		configInput(DRY_WET_INPUT, "Dry/wet CV");
		configOutput(OUT_L_OUTPUT, "Audio out L");
		configOutput(OUT_R_OUTPUT, "Audio out R");
		configOutput(GATE_OUTPUT, "Grain gate");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// The Freeze, Quality and Seed lights sit in their buttons, which
		// have their own tooltips.
		configLight(GRAIN_LIGHT, "Grain");

		time.Init(robotboy_beads::RandomizedChannel::kTime);
		size.Init(robotboy_beads::RandomizedChannel::kSizeShape);
		shape.Init(robotboy_beads::RandomizedChannel::kSizeShape);
		pitch.Init(robotboy_beads::RandomizedChannel::kPitch);
		densityPot.Init(robotboy_beads::PotLaw::kNormal, robotboy_beads::kPotLp, 0.f);
		robotboy_beads::InitDensityCv(&densityCv);
		robotboy_beads::InitMixCv(&feedbackCv);
		robotboy_beads::InitMixCv(&dryWetCv);
		robotboy_beads::InitMixCv(&reverbCv);
	}

	// What the Beads firmware reads from its pots and jacks each block
	// (beads.cc), from this panel.
	void ReadControls(robotboy_beads::BlockControls* c) override {
		readSharedControls(c);
		if (scaleChanged.exchange(false))
			pitchLock.Set(pitchScale, pitchRoot);

		auto knob = [&](int id) { return params[id].getValue(); };
		// The attenurandomizers (and Size) run -1..1; Beads' pots run 0..1.
		auto bipolar = [&](int id) { return (params[id].getValue() + 1.f) * 0.5f; };
		auto patched = [&](int id) { return inputs[id].isConnected(); };
		auto cv = [&](int id) { return inputs[id].getVoltage() * 0.2f; };

		c->parameters.time = time.Process(knob(TIME_PARAM), bipolar(TIME_AR_PARAM), patched(TIME_INPUT), cv(TIME_INPUT));
		c->parameters.size = size.Process(bipolar(SIZE_PARAM), bipolar(SIZE_AR_PARAM), patched(SIZE_INPUT), cv(SIZE_INPUT));
		c->parameters.shape = shape.Process(knob(SHAPE_PARAM), bipolar(SHAPE_AR_PARAM), patched(SHAPE_INPUT), cv(SHAPE_INPUT));
		c->parameters.pitch = pitch.Process(knob(PITCH_PARAM), bipolar(PITCH_AR_PARAM), patched(PITCH_INPUT),
			inputs[PITCH_INPUT].getVoltage() * 12.f);
		c->parameters.monophonic_mode = false;
		c->parameters.pitch_lock = pitchLock.hook();

		c->density.base = densityPot.Process(knob(DENSITY_PARAM));
		// beads.cc zeroes an unpatched Density CV, to ignore ADC noise.
		c->density.cv = patched(DENSITY_INPUT) ? densityCv.Process(cv(DENSITY_INPUT)) : 0.f;

		c->mix.feedback = robotboy_beads::MixParameter(knob(FEEDBACK_PARAM),
			feedbackCv.Process(cv(FEEDBACK_INPUT)), knob(FEEDBACK_CV_PARAM));
		c->mix.dry_wet = robotboy_beads::MixParameter(knob(DRY_WET_PARAM),
			dryWetCv.Process(cv(DRY_WET_INPUT)), knob(DRY_WET_CV_PARAM));
		c->mix.reverb_amount = robotboy_beads::MixParameter(knob(REVERB_PARAM),
			reverbCv.Process(cv(REVERB_INPUT)), knob(REVERB_CV_PARAM));
		c->mix.output_gate = false;

		c->seed_patched = patched(SEED_INPUT);
		c->seed_button = seedLocked ? beads::SEED_BUTTON_STATE_LOCKED
			: seedHeld ? beads::SEED_BUTTON_STATE_ON
			: beads::SEED_BUTTON_STATE_OFF;
	}

	void process(const ProcessArgs& args) override {
		const Gates gates = processGates();

		// Seed button (ui.cc): a press unlocks it and holds; a 4 s hold
		// locks it again (and ends the hold, though the button is still down).
		const bool seedPressed = params[SEED_PARAM].getValue() > 0.5f;
		if (seedButton.process(seedPressed)) {
			seedLocked = false;
			seedHeld = true;
			seedHoldTime = 0.f;
		}
		if (seedPressed && seedHeld) {
			seedHoldTime += args.sampleTime;
			if (seedHoldTime >= kSeedLongPress) {
				seedLocked = true;
				seedHeld = false;
			}
		}
		if (!seedPressed)
			seedHeld = false;

		const robotboy_beads::HostFrameOut out = processEngine(args, gates);

		// Grain light: a flash per grain, brighter the more grains sound.
		if (grainTrigger.process(out.gate))
			grainLight = std::min(1.f, 0.4f + 0.1f * host.active_grains());
		grainLight = std::max(0.f, grainLight - args.sampleTime / kGrainLightDecay);

		if (lightDivider.process()) {
			const float lightTime = args.sampleTime * lightDivider.getDivision();
			updateSharedLights(gates.frozen);
			lights[GRAIN_LIGHT].setBrightness(grainLight);
			// Seed light (ui.cc): a slow pulse while locked; while unlocked,
			// lit while the button or the Seed input is high.
			seedPulsePhase += lightTime / kSeedPulsePeriod;
			seedPulsePhase -= std::floor(seedPulsePhase);
			const float triangle = 1.f - std::fabs(2.f * seedPulsePhase - 1.f);
			lights[SEED_LIGHT].setBrightness(seedLocked ? 0.25f + 0.75f * triangle
				: (seedHeld || gates.seed) ? 1.f : 0.f);
		}
	}

	void onReset(const ResetEvent& e) override {
		BeadsModule::onReset(e);
		seedLocked = true;
		seedHeld = false;
		pitchScale = 0;
		pitchRoot = 0;
		scaleChanged = true;
	}

	json_t* dataToJson() override {
		json_t* rootJ = BeadsModule::dataToJson();
		json_object_set_new(rootJ, "seedState", json_integer(seedLocked ? 0 : 1));
		json_object_set_new(rootJ, "pitchScale", json_integer(pitchScale));
		json_object_set_new(rootJ, "pitchRoot", json_integer(pitchRoot));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		BeadsModule::dataFromJson(rootJ);
		// Before 2.2 "seedState" was the Seed CV mode: Triggers (0) loads
		// locked and Gates (1) unlocked, the closest Beads behavior.
		json_t* j = json_object_get(rootJ, "seedState");
		seedLocked = j ? json_integer_value(j) == 0 : true;
		pitchScale = 0;
		if ((j = json_object_get(rootJ, "pitchScale")))
			pitchScale = clamp((int)json_integer_value(j), 0, 7);
		else if ((j = json_object_get(rootJ, "pitchLock")))   // before the scale modes
			pitchScale = clamp((int)json_integer_value(j), 0, 2);
		j = json_object_get(rootJ, "pitchRoot");
		pitchRoot = j ? clamp((int)json_integer_value(j), 0, 11) : 0;
		scaleChanged = true;
		// "grainTriggerOut" (an old Out R option) is ignored: the grain gate
		// has its own output now.
	}
};

static_assert(Particules::FREEZE_PARAM == BeadsModule::FREEZE_PARAM && Particules::QUALITY_PARAM == BeadsModule::QUALITY_PARAM);
static_assert(Particules::IN_L_INPUT == BeadsModule::IN_L_INPUT && Particules::IN_R_INPUT == BeadsModule::IN_R_INPUT
	&& Particules::FREEZE_INPUT == BeadsModule::FREEZE_INPUT && Particules::SEED_INPUT == BeadsModule::SEED_INPUT);
static_assert(Particules::OUT_L_OUTPUT == BeadsModule::OUT_L_OUTPUT && Particules::OUT_R_OUTPUT == BeadsModule::OUT_R_OUTPUT
	&& Particules::GATE_OUTPUT == BeadsModule::GATE_OUTPUT);
static_assert(Particules::QUALITY_R_LIGHT == BeadsModule::QUALITY_R_LIGHT && Particules::FREEZE_LIGHT == BeadsModule::FREEZE_LIGHT);

struct ParticulesWidget : ModuleWidget {
	ParticulesWidget(Particules* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Particules.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Top row: Freeze jack and button, Quality, Seed button and jack.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.9, 15.875)), module, Particules::FREEZE_INPUT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(20.4, 15.875)), module, Particules::FREEZE_PARAM, Particules::FREEZE_LIGHT));
		addParam(createQualityParamCentered(mm2px(Vec(38.1, 15.875)), module));
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(55.8, 15.875)), module, Particules::SEED_PARAM, Particules::SEED_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(64.3, 15.875)), module, Particules::SEED_INPUT));

		// Time, Density, Pitch, Size: knob, CV jack, attenurandomizer (the
		// grain light sits in Density's trimpot slot).
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 42.088)), module, Particules::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 42.088)), module, Particules::DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 42.088)), module, Particules::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 42.088)), module, Particules::SIZE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 53.746)), module, Particules::TIME_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 53.746)), module, Particules::DENSITY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 53.746)), module, Particules::PITCH_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 53.746)), module, Particules::SIZE_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 62.746)), module, Particules::TIME_AR_PARAM));
		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(29.6, 62.746)), module, Particules::GRAIN_LIGHT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 62.746)), module, Particules::PITCH_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 62.746)), module, Particules::SIZE_AR_PARAM));

		// Shape, Feedback, Reverb, Dry/wet: knob, CV jack, trimpot.
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 82.096)), module, Particules::SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 82.096)), module, Particules::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 82.096)), module, Particules::REVERB_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 82.096)), module, Particules::DRY_WET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 92.696)), module, Particules::SHAPE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 92.696)), module, Particules::FEEDBACK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 92.696)), module, Particules::REVERB_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 92.696)), module, Particules::DRY_WET_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 101.696)), module, Particules::SHAPE_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(29.6, 101.696)), module, Particules::FEEDBACK_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 101.696)), module, Particules::REVERB_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 101.696)), module, Particules::DRY_WET_CV_PARAM));

		// Audio I/O and the grain gate.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.25, 114.3)), module, Particules::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.95, 114.3)), module, Particules::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.1, 114.3)), module, Particules::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.25, 114.3)), module, Particules::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(60.95, 114.3)), module, Particules::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Particules* module = getModule<Particules>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		appendBeadsMenu(menu, module);

		menu->addChild(createIndexSubmenuItem("Lock pitch",
			{"Off", "Octaves", "Octaves + 5ths", "Chromatic", "Major", "Minor", "Major pentatonic", "Minor pentatonic"},
			[=]() { return module->pitchScale; },
			[=](int i) {
				withMenuUndo(module, "change pitch lock", [=]() {
					module->pitchScale = i;
					module->scaleChanged = true;
				});
			}));
		// Greyed out unless a scale mode was selected when the menu opened.
		MenuItem* rootItem = createIndexSubmenuItem("Root",
			{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
			[=]() { return module->pitchRoot; },
			[=](int i) {
				withMenuUndo(module, "change scale root", [=]() {
					module->pitchRoot = i;
					module->scaleChanged = true;
				});
			});
		rootItem->disabled = module->pitchScale < robotboy_beads::kPitchChromatic;
		menu->addChild(rootItem);

		menu->addChild(createClearBufferItem(module));
	}
};

Model* modelParticules = createModel<Particules, ParticulesWidget>("Particules");
