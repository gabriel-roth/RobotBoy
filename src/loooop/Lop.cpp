#include "LooperModule.hpp"
#include "LoopDisplay.hpp"
#include <array>

struct Lop : LooperModule<Lop> {
	// This order is the MetaModule Elements order (Lop_info.hh), which
	// assigns param ids by position; patch conversion between the two maps
	// by id, so they must stay in lockstep. Panel knobs, then globals, then
	// the menu-only params.
	enum ParamId { SIZE_PARAM, POSITION_PARAM, SPEED_PARAM, JITTER_PARAM,
	               DRYWET_PARAM, RECORD_PARAM, CLEAR_PARAM, OVERDUB_PARAM, GRID_PARAM,
	               REC_GATE_MODE_PARAM, TRIG_WHEN_REC_PARAM, CROSSFADE_PARAM, TRIG_MODE_PARAM,
	               SPEED_VOCT_PARAM,
	               PARAMS_LEN };
	enum InputId { AUDIO_L_INPUT, AUDIO_R_INPUT,
	               SIZE_CV_INPUT, POSITION_CV_INPUT, SPEED_CV_INPUT, JITTER_CV_INPUT, TRIG_INPUT, JUMP_INPUT,
	               DRYWET_CV_INPUT, RECORD_TRIG_INPUT, CLEAR_TRIG_INPUT, INPUTS_LEN };
	enum OutputId { MIX_L_OUTPUT, MIX_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { RECORD_LIGHT, OVERDUB_R_LIGHT, OVERDUB_G_LIGHT, OVERDUB_B_LIGHT, LIGHTS_LEN };

	// A single playhead, whose level stays at its 1.0 default.
	Lop() : LooperModule(1) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<SpeedParamQuantity>(SPEED_PARAM, -2.f, 2.f, 1.f, "Speed");
		configParam(POSITION_PARAM, 0.f, 1.f, 0.5f, "Position");
		configParam(SIZE_PARAM, 0.f, 1.f, 1.f, "Size");
		configParam(JITTER_PARAM, 0.f, 1.f, 0.f, "Jitter");
		configParam(DRYWET_PARAM, 0.f, 1.f, 1.f, "Dry/wet");
		configButton(RECORD_PARAM, "Record");
		configButton(CLEAR_PARAM, "Clear");
		// Mode switches, panel and menu alike, opt out of Randomize (as on
		// Loooop): a randomized one-shot with no trigger patched silences the
		// loop with nothing on the panel to show why.
		configSwitch(OVERDUB_PARAM, 0.f, 4.f, 0.f, "Overdub mode",
			{"Layer", "Decay", "Add", "Replace", "Lock"})->randomizeEnabled = false;
		configSwitch(TRIG_MODE_PARAM, 0.f, 1.f, 0.f, "One-shot",
			{"Off", "On"})->randomizeEnabled = false;
		configSwitch(SPEED_VOCT_PARAM, 0.f, 1.f, 0.f, "Speed CV V/Oct",
			{"Off", "On"})->randomizeEnabled = false;
		// 0 = On: inverted to match the MetaModule alt-param, which zero-inits.
		configSwitch(CROSSFADE_PARAM, 0.f, 1.f, 0.f, "Crossfade",
			{"On", "Off"})->randomizeEnabled = false;
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
		configInput(SPEED_CV_INPUT, "Speed CV");
		configInput(POSITION_CV_INPUT, "Position CV");
		configInput(SIZE_CV_INPUT, "Size CV");
		configInput(JITTER_CV_INPUT, "Jitter CV");
		configInput(DRYWET_CV_INPUT, "Dry/wet CV");
		configInput(TRIG_INPUT, "Trigger");
		configInput(JUMP_INPUT, "Jump");
		configOutput(MIX_L_OUTPUT, "Audio L");
		configOutput(MIX_R_OUTPUT, "Audio R");
		configBypass(AUDIO_L_INPUT, MIX_L_OUTPUT);
		configBypass(AUDIO_R_INPUT, MIX_R_OUTPUT);
	}

	void process(const ProcessArgs& args) override {
		int od = processTransport();
		processHead(0, {SPEED_PARAM, SPEED_VOCT_PARAM, POSITION_PARAM, SIZE_PARAM, JITTER_PARAM,
		                TRIG_MODE_PARAM, SPEED_CV_INPUT, POSITION_CV_INPUT, SIZE_CV_INPUT,
		                JITTER_CV_INPUT, TRIG_INPUT, JUMP_INPUT});

		loooop::StereoInput in = readInput();
		std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> heads;
		engine.process(in.l, in.r, heads);
		writeMix(in, heads[0].l, heads[0].r);
		updateLights(od, args.sampleTime);
	}
};

struct LopWidget : ModuleWidget {
	LopWidget(Lop* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lop.svg")));

		auto* display = new LoopDisplayWidget();
		display->engine = module ? &module->engine : nullptr;
		display->demoHeads = 1;
		// The SVG's screen rect (mm), kept in sync with res/Lop.svg and
		// metamodule/loooop/Lop_info.hh by
		// metamodule/loooop/sync_info_positions.py; run it after a panel change.
		display->box.pos = mm2px(Vec(1.500, 10.400));
		display->box.size = mm2px(Vec(57.960, 22.350));
		addChild(display);

		// Dark screws: the SVG's screw dots are near-invisible on this panel.
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions mirror res/Lop.svg (panel-specs/lop.yaml).
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.87, 46.05)), module, Lop::SIZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(23.61, 46.05)), module, Lop::POSITION_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(37.35, 46.05)), module, Lop::SPEED_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(51.09, 46.05)), module, Lop::JITTER_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.87, 58.0)), module, Lop::SIZE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.61, 58.0)), module, Lop::POSITION_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(37.35, 58.0)), module, Lop::SPEED_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(51.09, 58.0)), module, Lop::JITTER_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.16, 74.15)), module, Lop::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 74.15)), module, Lop::JUMP_INPUT));
		addParam(createParamCentered<RoundSmallBlackSnapKnob>(mm2px(Vec(48.8, 74.15)), module, Lop::GRID_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.16, 92.95)), module, Lop::DRYWET_PARAM));
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(30.48, 92.95)), module, Lop::RECORD_PARAM, Lop::RECORD_LIGHT));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(48.8, 92.95)), module, Lop::CLEAR_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.16, 104.9)), module, Lop::DRYWET_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 104.9)), module, Lop::RECORD_TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.8, 104.9)), module, Lop::CLEAR_TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.35, 116.05)), module, Lop::AUDIO_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.05, 116.05)), module, Lop::AUDIO_R_INPUT));
		addParam(createLightParamCentered<OverdubButton>(mm2px(Vec(30.48, 116.05)), module, Lop::OVERDUB_PARAM, Lop::OVERDUB_R_LIGHT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(43.91, 116.05)), module, Lop::MIX_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(53.61, 116.05)), module, Lop::MIX_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Lop* module = getModule<Lop>();
		if (!module)
			return;

		// Overdub and Grid are panel controls; the menu holds the rest, in
		// Loooop's order and wording. With One-shot unchecked, a trigger
		// restarts the playhead at its window start.
		appendLooperMenu(menu, module);
		menu->addChild(createBoolParamMenuItem("One-shot on trigger", module, Lop::TRIG_MODE_PARAM));
		menu->addChild(createBoolParamMenuItem("Speed CV = V/Oct", module, Lop::SPEED_VOCT_PARAM));
	}
};

Model* modelLop = createModel<Lop, LopWidget>("Lop");
