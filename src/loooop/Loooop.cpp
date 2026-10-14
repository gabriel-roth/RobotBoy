#include "LooperModule.hpp"
#include "LoopDisplay.hpp"
#include "HeadColors.hpp"
#include <array>
#include <string>

// Playhead names follow the head colors on the panel and display: "Red
// playhead", "Yellow playhead", ...
static std::string headName(int h) {
	return std::string(loooop::kHeadColors[h].name) + " playhead";
}

struct Loooop : LooperModule<Loooop> {
	// Global params first, then the panel knobs in a block of HEAD_PARAMS
	// per head (index with X1_PARAM + HEAD_PARAMS * h), then the menu-only
	// params, each a run of four for the per-head ones (X1_PARAM + h).
	// This order is the MetaModule Elements order
	// (metamodule/loooop/Loooop_info.hh), which assigns param ids by
	// position; patch conversion between the two maps by id, so they must
	// stay in lockstep.
	enum ParamId { RECORD_PARAM, OVERDUB_PARAM, CLEAR_PARAM, GRID_PARAM, DRYWET_PARAM,
	               SIZE1_PARAM, POSITION1_PARAM, SPEED1_PARAM, JITTER1_PARAM, PAN1_PARAM, LEVEL1_PARAM,
	               SIZE2_PARAM, POSITION2_PARAM, SPEED2_PARAM, JITTER2_PARAM, PAN2_PARAM, LEVEL2_PARAM,
	               SIZE3_PARAM, POSITION3_PARAM, SPEED3_PARAM, JITTER3_PARAM, PAN3_PARAM, LEVEL3_PARAM,
	               SIZE4_PARAM, POSITION4_PARAM, SPEED4_PARAM, JITTER4_PARAM, PAN4_PARAM, LEVEL4_PARAM,
	               REC_GATE_MODE_PARAM, TRIG_WHEN_REC_PARAM, CROSSFADE_PARAM,
	               TRIG_MODE1_PARAM, TRIG_MODE2_PARAM, TRIG_MODE3_PARAM, TRIG_MODE4_PARAM,
	               SPEED_VOCT1_PARAM, SPEED_VOCT2_PARAM, SPEED_VOCT3_PARAM, SPEED_VOCT4_PARAM,
	               EXCLUDE_GRID1_PARAM, EXCLUDE_GRID2_PARAM, EXCLUDE_GRID3_PARAM, EXCLUDE_GRID4_PARAM,
	               PARAMS_LEN };
	enum InputId { AUDIO_L_INPUT, AUDIO_R_INPUT, RECORD_TRIG_INPUT, CLEAR_TRIG_INPUT, DRYWET_CV_INPUT,
	               SIZE1_CV_INPUT, POSITION1_CV_INPUT, SPEED1_CV_INPUT, JITTER1_CV_INPUT, PAN1_CV_INPUT, LEVEL1_CV_INPUT, TRIG1_INPUT, JUMP1_INPUT,
	               SIZE2_CV_INPUT, POSITION2_CV_INPUT, SPEED2_CV_INPUT, JITTER2_CV_INPUT, PAN2_CV_INPUT, LEVEL2_CV_INPUT, TRIG2_INPUT, JUMP2_INPUT,
	               SIZE3_CV_INPUT, POSITION3_CV_INPUT, SPEED3_CV_INPUT, JITTER3_CV_INPUT, PAN3_CV_INPUT, LEVEL3_CV_INPUT, TRIG3_INPUT, JUMP3_INPUT,
	               SIZE4_CV_INPUT, POSITION4_CV_INPUT, SPEED4_CV_INPUT, JITTER4_CV_INPUT, PAN4_CV_INPUT, LEVEL4_CV_INPUT, TRIG4_INPUT, JUMP4_INPUT,
	               INPUTS_LEN };
	enum OutputId { MIX_L_OUTPUT, MIX_R_OUTPUT,
	                HEAD1_L_OUTPUT, HEAD1_R_OUTPUT, HEAD2_L_OUTPUT, HEAD2_R_OUTPUT,
	                HEAD3_L_OUTPUT, HEAD3_R_OUTPUT, HEAD4_L_OUTPUT, HEAD4_R_OUTPUT,
	                OUTPUTS_LEN };
	enum LightId { RECORD_LIGHT, OVERDUB_R_LIGHT, OVERDUB_G_LIGHT, OVERDUB_B_LIGHT, LIGHTS_LEN };

	static constexpr int HEAD_PARAMS = 6;   // Size, Position, Speed, Jitter, Pan, Level
	static constexpr int HEAD_INPUTS = 8;   // their CVs, then Trig and Jump

	robotboy::OnePoleSmoother panSmoother[LoopEngine::NUM_HEADS];

	Loooop() : LooperModule(LoopEngine::NUM_HEADS) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
			const std::string n = headName(h);
			const int p = HEAD_PARAMS * h, in = HEAD_INPUTS * h;
			configParam<SpeedParamQuantity>(SPEED1_PARAM + p, -2.f, 2.f, 1.f, n + " speed");
			configParam(POSITION1_PARAM + p, 0.f, 1.f, 0.5f, n + " position");
			configParam(SIZE1_PARAM + p, 0.f, 1.f, 1.f, n + " size");
			// 0.25: four phase-locked heads at their defaults sum to unity.
			configParam(LEVEL1_PARAM + p, 0.f, 1.f, 0.25f, n + " level");
			configParam(JITTER1_PARAM + p, 0.f, 1.f, 0.f, n + " jitter");
			configParam(PAN1_PARAM + p, -1.f, 1.f, 0.f, n + " pan");
			// The menu switches opt out of Randomize: a randomized one-shot
			// with no trigger patched silences its head with nothing on the
			// panel to show why.
			configSwitch(TRIG_MODE1_PARAM + h, 0.f, 1.f, 0.f, n + " one-shot",
				{"Off", "On"})->randomizeEnabled = false;
			configSwitch(SPEED_VOCT1_PARAM + h, 0.f, 1.f, 0.f, n + " speed CV V/Oct",
				{"Off", "On"})->randomizeEnabled = false;
			configSwitch(EXCLUDE_GRID1_PARAM + h, 0.f, 1.f, 0.f, n + " exclude from Grid",
				{"Off", "On"})->randomizeEnabled = false;
			configInput(SPEED1_CV_INPUT + in, n + " speed CV");
			configInput(POSITION1_CV_INPUT + in, n + " position CV");
			configInput(SIZE1_CV_INPUT + in, n + " size CV");
			configInput(LEVEL1_CV_INPUT + in, n + " level CV");
			configInput(JITTER1_CV_INPUT + in, n + " jitter CV");
			configInput(PAN1_CV_INPUT + in, n + " pan CV");
			configInput(TRIG1_INPUT + in, n + " trigger");
			configInput(JUMP1_INPUT + in, n + " jump");
			configOutput(HEAD1_L_OUTPUT + 2 * h, n + " L");
			configOutput(HEAD1_R_OUTPUT + 2 * h, n + " R");
		}
		configParam(DRYWET_PARAM, 0.f, 1.f, 1.f, "Dry/wet");
		configButton(RECORD_PARAM, "Record");
		configButton(CLEAR_PARAM, "Clear");
		// Mode switches opt out of Randomize too: Lock gates the Record
		// button, and a randomized Grid or Crossfade reads as broken, not as
		// an inspiring variation.
		configSwitch(OVERDUB_PARAM, 0.f, 4.f, 0.f, "Overdub mode",
			{"Layer", "Decay", "Add", "Replace", "Lock"})->randomizeEnabled = false;
		// 0 = On: inverted to match the MetaModule alt-param, which zero-inits.
		configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade", {"On", "Off"})->randomizeEnabled = false;
		configSwitch(GRID_PARAM, 0.f, 5.f, 0.f, "Grid",
			{"Off", "4", "8", "16", "32", "64"})->randomizeEnabled = false;
		configSwitch(TRIG_WHEN_REC_PARAM, 0.f, 1.f, 0.f, "When recording ends",
			{"Plays back", "Keeps overdubbing"})->randomizeEnabled = false;
		// Gate: the jack's rising edge starts recording, its falling edge
		// stops it (see loooop::RecordGateHelper).
		configSwitch(REC_GATE_MODE_PARAM, 0.f, 1.f, 0.f, "Record jack",
			{"Trigger", "Gate"})->randomizeEnabled = false;
		configInput(AUDIO_L_INPUT, "Audio L");
		configInput(AUDIO_R_INPUT, "Audio R");
		configInput(RECORD_TRIG_INPUT, "Record trigger");
		configInput(CLEAR_TRIG_INPUT, "Clear trigger");
		configInput(DRYWET_CV_INPUT, "Dry/wet CV");
		configOutput(MIX_L_OUTPUT, "Mix L");
		configOutput(MIX_R_OUTPUT, "Mix R");
		configBypass(AUDIO_L_INPUT, MIX_L_OUTPUT);
		configBypass(AUDIO_R_INPUT, MIX_R_OUTPUT);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		LooperModule::onSampleRateChange(e);
		for (auto& s : panSmoother)
			s.setAlpha(robotboy::smootherAlpha(e.sampleRate, kSmoothingTau));
	}

	void process(const ProcessArgs& args) override {
		int od = processTransport();

		for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
			const int p = HEAD_PARAMS * h, in = HEAD_INPUTS * h;
			processHead(h, {SPEED1_PARAM + p, SPEED_VOCT1_PARAM + h, POSITION1_PARAM + p,
			                SIZE1_PARAM + p, JITTER1_PARAM + p, TRIG_MODE1_PARAM + h,
			                SPEED1_CV_INPUT + in, POSITION1_CV_INPUT + in, SIZE1_CV_INPUT + in,
			                JITTER1_CV_INPUT + in, TRIG1_INPUT + in, JUMP1_INPUT + in});
			engine.setLevel(h, loooop::normalizedControl(
				params[LEVEL1_PARAM + p].getValue(), inputs[LEVEL1_CV_INPUT + in].getVoltage()));
			engine.setGridExclude(h, params[EXCLUDE_GRID1_PARAM + h].getValue() > 0.5f);
		}

		loooop::StereoInput in = readInput();
		std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> heads;
		engine.process(in.l, in.r, heads);

		// The head outputs carry each head at full level; Level and Pan
		// shape only its contribution to the mix.
		float wetL = 0.f, wetR = 0.f;
		for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
			outputs[HEAD1_L_OUTPUT + 2 * h].setVoltage(heads[h].l * 5.f);
			outputs[HEAD1_R_OUTPUT + 2 * h].setVoltage(heads[h].r * 5.f);
			float pan = panSmoother[h].process(loooop::panControl(
				params[PAN1_PARAM + HEAD_PARAMS * h].getValue(),
				inputs[PAN1_CV_INPUT + HEAD_INPUTS * h].getVoltage()));
			float level = engine.smoothedLevel(h);
			wetL += heads[h].l * level * loooop::panLeftGain(pan);
			wetR += heads[h].r * level * loooop::panRightGain(pan);
		}
		writeMix(in, wetL, wetR);
		updateLights(od, args.sampleTime);
	}
};

struct LoooopWidget : ModuleWidget {
	LoooopWidget(Loooop* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Loooop.svg")));

		auto* display = new LoopDisplayWidget();
		display->engine = module ? &module->engine : nullptr;
		// The SVG's screen rect (mm), kept in sync with res/Loooop.svg and
		// metamodule/loooop/Loooop_info.hh by
		// metamodule/loooop/sync_info_positions.py; run it after a panel change.
		display->box.pos = mm2px(Vec(1.500, 10.400));
		display->box.size = mm2px(Vec(190.040, 22.350));
		addChild(display);

		// Dark screws: the SVG's screw dots are near-invisible on this panel.
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Each head is a block: Size/Position/Speed on the upper knob row,
		// Jitter/Pan/Level below, each knob over its CV jack, then a jack row
		// of Trig, Jump, Out L, Out R. Columns (mm) from res/Loooop.svg.
		static constexpr float kKnobX[4][3] = {
			{10.293f, 24.88f, 39.467f}, {58.053f, 72.64f, 87.227f},
			{105.81f, 120.4f, 134.99f}, {153.57f, 168.16f, 182.75f}};
		static constexpr float kJackX[4][4] = {
			{7.35f, 20.03f, 32.71f, 42.41f}, {55.11f, 67.79f, 80.47f, 90.17f},
			{102.87f, 115.55f, 128.23f, 137.93f}, {150.63f, 163.31f, 175.99f, 185.69f}};
		for (int h = 0; h < LoopEngine::NUM_HEADS; ++h) {
			const int p = Loooop::HEAD_PARAMS * h, in = Loooop::HEAD_INPUTS * h;
			const float* x = kKnobX[h];
			const int upper[3] = {Loooop::SIZE1_PARAM, Loooop::POSITION1_PARAM, Loooop::SPEED1_PARAM};
			const int lower[3] = {Loooop::JITTER1_PARAM, Loooop::PAN1_PARAM, Loooop::LEVEL1_PARAM};
			const int upperCv[3] = {Loooop::SIZE1_CV_INPUT, Loooop::POSITION1_CV_INPUT, Loooop::SPEED1_CV_INPUT};
			const int lowerCv[3] = {Loooop::JITTER1_CV_INPUT, Loooop::PAN1_CV_INPUT, Loooop::LEVEL1_CV_INPUT};
			for (int c = 0; c < 3; ++c) {
				addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x[c], 46.35)), module, upper[c] + p));
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x[c], 58.7)), module, upperCv[c] + in));
				addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x[c], 75.05)), module, lower[c] + p));
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x[c], 87.4)), module, lowerCv[c] + in));
			}
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kJackX[h][0], 102.1)), module, Loooop::TRIG1_INPUT + in));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(kJackX[h][1], 102.1)), module, Loooop::JUMP1_INPUT + in));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kJackX[h][2], 102.1)), module, Loooop::HEAD1_L_OUTPUT + 2 * h));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(kJackX[h][3], 102.1)), module, Loooop::HEAD1_R_OUTPUT + 2 * h));
		}

		// Bottom row: In L/R, Record + trigger, Overdub, Clear + trigger,
		// Grid, Dry/wet + CV, Mix L/R.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.35, 116.05)), module, Loooop::AUDIO_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.05, 116.05)), module, Loooop::AUDIO_R_INPUT));
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(36.452, 116.05)), module, Loooop::RECORD_PARAM, Loooop::RECORD_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(47.302, 116.05)), module, Loooop::RECORD_TRIG_INPUT));
		addParam(createLightParamCentered<OverdubButton>(mm2px(Vec(69.543, 116.05)), module, Loooop::OVERDUB_PARAM, Loooop::OVERDUB_R_LIGHT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(91.785, 116.05)), module, Loooop::CLEAR_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(102.64, 116.05)), module, Loooop::CLEAR_TRIG_INPUT));
		addParam(createParamCentered<RoundSmallBlackSnapKnob>(mm2px(Vec(122.69, 116.05)), module, Loooop::GRID_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(143.49, 116.05)), module, Loooop::DRYWET_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(155.84, 116.05)), module, Loooop::DRYWET_CV_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(175.99, 116.05)), module, Loooop::MIX_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(185.69, 116.05)), module, Loooop::MIX_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Loooop* module = getModule<Loooop>();
		if (!module)
			return;

		appendLooperMenu(menu, module);
		// Per-playhead options, by color. With One-shot unchecked, a trigger
		// restarts the playhead at its window start.
		auto perHead = [=](const char* text, int firstParam) {
			return createSubmenuItem(text, "", [=](Menu* sub) {
				for (int h = 0; h < LoopEngine::NUM_HEADS; ++h)
					sub->addChild(createBoolParamMenuItem(headName(h), module, firstParam + h));
			});
		};
		menu->addChild(perHead("One-shot on trigger", Loooop::TRIG_MODE1_PARAM));
		menu->addChild(perHead("Speed CV = V/Oct", Loooop::SPEED_VOCT1_PARAM));
		menu->addChild(perHead("Exclude from Grid", Loooop::EXCLUDE_GRID1_PARAM));
	}
};

Model* modelLoooop = createModel<Loooop, LoooopWidget>("Loooop");
