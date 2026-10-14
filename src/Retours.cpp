#include "BeadsModule.hpp"

// Retours: the delay mode of Mutable Instruments Beads (the mode Beads enters
// with Size fully clockwise). Interval is Beads' Density knob in that mode,
// Slice is its Freeze, Clock is its Seed button and input, and Filter is the
// band filter Beads plays from its Size attenurandomizer.

// The clock light snaps on at each repeat, then fades over this long, so
// each flash spans several video frames.
static constexpr float kClockLightDecay = 0.08f;

struct Retours : BeadsModule {
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
		FEEDBACK_CV_PARAM,
		DRY_WET_CV_PARAM,
		// Added with the 15 HP panel; appended so older patches still load.
		FILTER_PARAM,
		FILTER_CV_PARAM,
		REVERB_PARAM,
		REVERB_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		SLICE_INPUT,
		CLOCK_INPUT,
		TIME_INPUT,
		INTERVAL_INPUT,
		PITCH_INPUT,
		SHAPE_INPUT,
		FEEDBACK_INPUT,
		DRY_WET_INPUT,
		FILTER_INPUT,
		REVERB_INPUT,
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
		SLICE_LIGHT,
		CLOCK_LIGHT,
		LIGHTS_LEN
	};

	// Tempo. Beads starts with its Seed button "locked": no tempo, Interval
	// sets the time directly. A tap (or a clock tick) unlocks it, and the
	// tempo then holds, even after the clock is unpatched, until "Clear saved
	// tempo" locks it again. Taps and ticks are latched per sample and read
	// once per engine block.
	bool tempoHeld = false;
	bool tapPending = false;
	bool clockTickPending = false;
	bool clockPatched = false;
	std::atomic<bool> clearTempoRequested{false};
	dsp::BooleanTrigger tapButton;

	// Beads' pot and CV conditioning (cv_reader.cc), once per engine block.
	robotboy_beads::RandomizedChannel time, pitch, shape;
	robotboy_beads::PotChannel intervalPot, filterPot;
	robotboy_beads::CvChannel intervalCv, filterCv, feedbackCv, dryWetCv, reverbCv;

	dsp::BooleanTrigger repeatTrigger;
	float clockLight = 0.f;

	Retours() : BeadsModule(beads::Processor::MODE_DELAY) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// configSwitch doesn't disable randomization (only configButton
		// does), so without this Ctrl+R could latch Slice.
		configSwitch(SLICE_PARAM, 0.f, 1.f, 0.f, "Slice", {"Off", "On"})->randomizeEnabled = false;
		configQuality();
		configButton(CLOCK_PARAM, "Tap tempo");
		configParam(TIME_PARAM, 0.f, 1.f, 0.f, "Time");
		configParam(INTERVAL_PARAM, 0.f, 1.f, 0.35f, "Interval");
		configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
		configParam<FilterParamQuantity>(FILTER_PARAM, 0.f, 1.f, 0.5f, "Filter");
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "Shape");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback");
		configParam(REVERB_PARAM, 0.f, 1.f, 0.f, "Reverb");
		configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/wet");
		configParam(TIME_AR_PARAM, -1.f, 1.f, 0.f, "Time attenurandomizer");
		configParam(PITCH_AR_PARAM, -1.f, 1.f, 0.f, "Pitch attenurandomizer");
		configParam(SHAPE_AR_PARAM, -1.f, 1.f, 0.f, "Shape attenurandomizer");
		configParam(FILTER_CV_PARAM, -1.f, 1.f, 0.f, "Filter CV amount");
		configParam(FEEDBACK_CV_PARAM, -1.f, 1.f, 0.f, "Feedback CV amount");
		configParam(REVERB_CV_PARAM, -1.f, 1.f, 0.f, "Reverb CV amount");
		configParam(DRY_WET_CV_PARAM, -1.f, 1.f, 0.f, "Dry/wet CV amount");

		configInput(IN_L_INPUT, "Audio in L");
		configInput(IN_R_INPUT, "Audio in R");
		configInput(SLICE_INPUT, "Slice gate");
		configInput(CLOCK_INPUT, "Clock");
		configInput(TIME_INPUT, "Time CV");
		configInput(INTERVAL_INPUT, "Interval CV");
		configInput(PITCH_INPUT, "1V/octave pitch");
		configInput(FILTER_INPUT, "Filter CV");
		configInput(SHAPE_INPUT, "Shape CV");
		configInput(FEEDBACK_INPUT, "Feedback CV");
		configInput(REVERB_INPUT, "Reverb CV");
		configInput(DRY_WET_INPUT, "Dry/wet CV");
		configOutput(OUT_L_OUTPUT, "Audio out L");
		configOutput(OUT_R_OUTPUT, "Audio out R");
		configOutput(GATE_OUTPUT, "Repeat gate");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// The Slice and Quality lights sit in their buttons, which have their
		// own tooltips.
		configLight(CLOCK_LIGHT, "Clock");

		time.Init(robotboy_beads::RandomizedChannel::kTime);
		pitch.Init(robotboy_beads::RandomizedChannel::kPitch);
		shape.Init(robotboy_beads::RandomizedChannel::kSizeShape);
		intervalPot.Init(robotboy_beads::PotLaw::kNormal, robotboy_beads::kPotLp, 0.f);
		robotboy_beads::InitDensityCv(&intervalCv);
		// Filter: Beads' Size attenurandomizer in delay mode, with its knob
		// law (dead band at noon) and Size's CV conditioning.
		filterPot.Init(robotboy_beads::PotLaw::kAttenuverter, robotboy_beads::kPotLp, 0.f);
		robotboy_beads::InitTimeSizeShapeCv(&filterCv);
		robotboy_beads::InitMixCv(&feedbackCv);
		robotboy_beads::InitMixCv(&dryWetCv);
		robotboy_beads::InitMixCv(&reverbCv);
	}

	// Filter knob tooltip: off at noon (Beads' dead band), otherwise how far
	// into the low-pass or high-pass side the knob is.
	struct FilterParamQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			const float amount = robotboy_beads::AttenuverterLaw(getValue());
			if (amount == 0.f)
				return "Off";
			return string::f("%s %.0f%%", amount < 0.f ? "Low-pass" : "High-pass", std::fabs(amount) * 100.f);
		}
	};

	// What the Beads firmware reads from its pots and jacks each block
	// (beads.cc), from this panel.
	void ReadControls(robotboy_beads::BlockControls* c) override {
		readSharedControls(c);
		if (clearTempoRequested.exchange(false))
			tempoHeld = false;

		auto knob = [&](int id) { return params[id].getValue(); };
		// The attenurandomizers run -1..1; Beads' pots run 0..1.
		auto bipolar = [&](int id) { return (params[id].getValue() + 1.f) * 0.5f; };
		auto patched = [&](int id) { return inputs[id].isConnected(); };
		auto cv = [&](int id) { return inputs[id].getVoltage() * 0.2f; };

		c->parameters.time = time.Process(knob(TIME_PARAM), bipolar(TIME_AR_PARAM), patched(TIME_INPUT), cv(TIME_INPUT));
		c->parameters.pitch = pitch.Process(knob(PITCH_PARAM), bipolar(PITCH_AR_PARAM), patched(PITCH_INPUT),
			inputs[PITCH_INPUT].getVoltage() * 12.f);
		c->parameters.shape = shape.Process(knob(SHAPE_PARAM), bipolar(SHAPE_AR_PARAM), patched(SHAPE_INPUT), cv(SHAPE_INPUT));
		// Size fully clockwise pins delay mode; its attenurandomizer plays the
		// band filter: bandwidth = 0.5 * (1 + attenuverter) + Size CV
		// (granular_processor.h). Noon is off, low-pass to the left,
		// high-pass to the right. Retours scales the CV with its own trimpot.
		c->parameters.size = {
			.base = 1.f,
			.attenuverter = filterPot.Process(knob(FILTER_PARAM)),
			.cv = filterCv.Process(cv(FILTER_INPUT)) * knob(FILTER_CV_PARAM),
			.has_cv = patched(FILTER_INPUT),
		};
		c->parameters.monophonic_mode = false;
		c->parameters.pitch_lock = nullptr;

		c->density.base = intervalPot.Process(knob(INTERVAL_PARAM));
		// beads.cc zeroes an unpatched Density CV, to ignore ADC noise.
		c->density.cv = patched(INTERVAL_INPUT) ? intervalCv.Process(cv(INTERVAL_INPUT)) : 0.f;

		c->mix.feedback = robotboy_beads::MixParameter(knob(FEEDBACK_PARAM),
			feedbackCv.Process(cv(FEEDBACK_INPUT)), knob(FEEDBACK_CV_PARAM));
		c->mix.dry_wet = robotboy_beads::MixParameter(knob(DRY_WET_PARAM),
			dryWetCv.Process(cv(DRY_WET_INPUT)), knob(DRY_WET_CV_PARAM));
		c->mix.reverb_amount = robotboy_beads::MixParameter(knob(REVERB_PARAM),
			reverbCv.Process(cv(REVERB_INPUT)), knob(REVERB_CV_PARAM));
		c->mix.output_gate = false;

		// Clock = Beads' Seed. A tap is the Seed button pressed for one block
		// (the first press from the locked state only unlocks, as on Beads).
		// A patched clock goes to the Seed input; its first tick unlocks too,
		// so the measured tempo survives unpatching.
		const bool clock = patched(CLOCK_INPUT);
		if (clockTickPending && clock)
			tempoHeld = true;
		c->seed_patched = clock;
		// While the engine is muted after a reformat it isn't listening, so a
		// tap waits for it.
		if (tapPending && !host.muted()) {
			c->seed_button = beads::SEED_BUTTON_STATE_ON;
			tempoHeld = true;
			tapPending = false;
		}
		else {
			c->seed_button = tempoHeld ? beads::SEED_BUTTON_STATE_OFF : beads::SEED_BUTTON_STATE_LOCKED;
		}
		clockTickPending = false;
	}

	void process(const ProcessArgs& args) override {
		const Gates gates = processGates();
		clockPatched = inputs[CLOCK_INPUT].isConnected();
		if (gates.seed)
			clockTickPending = true;
		if (tapButton.process(params[CLOCK_PARAM].getValue() > 0.5f))
			tapPending = true;

		const robotboy_beads::HostFrameOut out = processEngine(args, gates);

		// Clock light: a flash at the start of each repeat (Beads' tap gate).
		if (repeatTrigger.process(out.gate))
			clockLight = 1.f;
		clockLight = std::max(0.f, clockLight - args.sampleTime / kClockLightDecay);

		if (lightDivider.process()) {
			updateSharedLights(gates.frozen);
			lights[CLOCK_LIGHT].setBrightness(clockLight);
		}
	}

	void onReset(const ResetEvent& e) override {
		BeadsModule::onReset(e);
		clearTempoRequested = true;   // back to free-running
	}

	// "timeChangeMode" (and the older "inputTrimDb", "slewSeconds" and
	// "envelopePreFeedback") in patches from before the Beads engine are
	// ignored: Beads has one fixed time-change behavior.
};

static_assert(Retours::SLICE_PARAM == BeadsModule::FREEZE_PARAM && Retours::QUALITY_PARAM == BeadsModule::QUALITY_PARAM);
static_assert(Retours::IN_L_INPUT == BeadsModule::IN_L_INPUT && Retours::IN_R_INPUT == BeadsModule::IN_R_INPUT
	&& Retours::SLICE_INPUT == BeadsModule::FREEZE_INPUT && Retours::CLOCK_INPUT == BeadsModule::SEED_INPUT);
static_assert(Retours::OUT_L_OUTPUT == BeadsModule::OUT_L_OUTPUT && Retours::OUT_R_OUTPUT == BeadsModule::OUT_R_OUTPUT
	&& Retours::GATE_OUTPUT == BeadsModule::GATE_OUTPUT);
static_assert(Retours::QUALITY_R_LIGHT == BeadsModule::QUALITY_R_LIGHT && Retours::SLICE_LIGHT == BeadsModule::FREEZE_LIGHT);

struct RetoursWidget : ModuleWidget {
	RetoursWidget(Retours* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Retours.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Top row: Slice jack and button, Quality, Tap button, Clock jack.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.9, 15.875)), module, Retours::SLICE_INPUT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(20.4, 15.875)), module, Retours::SLICE_PARAM, Retours::SLICE_LIGHT));
		addParam(createQualityParamCentered(mm2px(Vec(38.1, 15.875)), module));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(55.8, 15.875)), module, Retours::CLOCK_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(64.3, 15.875)), module, Retours::CLOCK_INPUT));

		// Time, Interval, Pitch, Filter: knob, CV jack, trimpot (Interval has
		// no trimpot; the clock light sits in its slot).
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 42.088)), module, Retours::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 42.088)), module, Retours::INTERVAL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 42.088)), module, Retours::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 42.088)), module, Retours::FILTER_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 53.746)), module, Retours::TIME_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 53.746)), module, Retours::INTERVAL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 53.746)), module, Retours::PITCH_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 53.746)), module, Retours::FILTER_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 62.746)), module, Retours::TIME_AR_PARAM));
		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(29.6, 62.746)), module, Retours::CLOCK_LIGHT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 62.746)), module, Retours::PITCH_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 62.746)), module, Retours::FILTER_CV_PARAM));

		// Shape, Feedback, Reverb, Dry/wet: knob, CV jack, trimpot.
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 82.096)), module, Retours::SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 82.096)), module, Retours::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 82.096)), module, Retours::REVERB_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 82.096)), module, Retours::DRY_WET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 92.696)), module, Retours::SHAPE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 92.696)), module, Retours::FEEDBACK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 92.696)), module, Retours::REVERB_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 92.696)), module, Retours::DRY_WET_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 101.696)), module, Retours::SHAPE_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(29.6, 101.696)), module, Retours::FEEDBACK_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 101.696)), module, Retours::REVERB_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 101.696)), module, Retours::DRY_WET_CV_PARAM));

		// Audio I/O and the repeat gate.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.25, 114.3)), module, Retours::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.95, 114.3)), module, Retours::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.1, 114.3)), module, Retours::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.25, 114.3)), module, Retours::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(60.95, 114.3)), module, Retours::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Retours* module = getModule<Retours>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
		appendBeadsMenu(menu, module);

		// A tempo from a tap or a clock holds after the clock is unpatched;
		// this returns to free-running. Greyed out when there's no tempo, or
		// while a clock is patched (it would set the tempo again).
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear saved tempo", "",
			[=]() { module->clearTempoRequested = true; },
			!module->tempoHeld || module->clockPatched));
		menu->addChild(createClearBufferItem(module));
	}
};

Model* modelRetours = createModel<Retours, RetoursWidget>("Retours");
