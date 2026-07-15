#include "plugin.hpp"

// Yellowjacket — EDP Wasp-style CMOS state-variable filter (stereo).
//
// SHELL ONLY: the DSP is not implemented yet. process() passes the stereo audio
// input straight to all four output pairs; the Freq/Res/Drive/Blend controls,
// their CV inputs, and the Character menu toggle are wired to params/ports but
// do nothing until the filter engine lands. Panel positions mirror
// res/Yellowjacket.svg (see panel-specs/yellowjacket.yaml). configParam ranges
// are provisional.

struct Yellowjacket : Module {
	enum ParamId {
		FREQ_PARAM,
		RES_PARAM,
		DRIVE_PARAM,
		BLEND_PARAM,       // LP <-> notch <-> HP crossfade on the Mix output
		FREQ_CV_PARAM,     // Frequency CV attenuverter
		RES_CV_PARAM,      // Resonance CV attenuverter
		DRIVE_CV_PARAM,    // Drive CV attenuverter
		NUM_PARAMS
	};
	enum InputId {
		AUDIO_INPUT,
		AUDIO_INPUT_R,
		FREQ_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
		BLEND_INPUT,
		NUM_INPUTS
	};
	enum OutputId {
		LP_OUTPUT,  LP_OUTPUT_R,
		BP_OUTPUT,  BP_OUTPUT_R,
		HP_OUTPUT,  HP_OUTPUT_R,
		MIX_OUTPUT, MIX_OUTPUT_R,   // Blend of LP/HP (notch at centre)
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	// Character: false = tamed (overload limiter on, no self-oscillation, like
	// the original EDP Wasp); true = screaming (limiter off, self-oscillates).
	// Persisted now; acted on once the DSP lands.
	bool screaming = false;

	// Panel theme: 0 = charcoal (default), 1 = gold. Persisted; the widget reads
	// it to pick which faceplate SVG to draw.
	int panelTheme = 0;

	Yellowjacket() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(FREQ_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "Frequency", " Hz", 2.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(BLEND_PARAM, 0.f, 1.f, 0.5f, "Blend (LP–notch–HP)");
		configParam(FREQ_CV_PARAM,  -1.f, 1.f, 1.f, "Frequency CV", "x");
		configParam(RES_CV_PARAM,   -1.f, 1.f, 1.f, "Resonance CV", "x");
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 1.f, "Drive CV", "x");

		configInput(AUDIO_INPUT,   "Audio L");
		configInput(AUDIO_INPUT_R, "Audio R");
		configInput(FREQ_INPUT,  "Frequency CV");
		configInput(RES_INPUT,   "Resonance CV");
		configInput(DRIVE_INPUT, "Drive CV");
		configInput(BLEND_INPUT, "Blend CV");

		configOutput(LP_OUTPUT,   "Lowpass L");
		configOutput(LP_OUTPUT_R, "Lowpass R");
		configOutput(BP_OUTPUT,   "Bandpass L");
		configOutput(BP_OUTPUT_R, "Bandpass R");
		configOutput(HP_OUTPUT,   "Highpass L");
		configOutput(HP_OUTPUT_R, "Highpass R");
		configOutput(MIX_OUTPUT,   "Mix L (LP–notch–HP blend)");
		configOutput(MIX_OUTPUT_R, "Mix R (LP–notch–HP blend)");

		// Bypass routes the dry audio input to every output.
		for (int out : {LP_OUTPUT, BP_OUTPUT, HP_OUTPUT, MIX_OUTPUT})
			configBypass(AUDIO_INPUT, out);
		for (int out : {LP_OUTPUT_R, BP_OUTPUT_R, HP_OUTPUT_R, MIX_OUTPUT_R})
			configBypass(AUDIO_INPUT_R, out);
	}

	// Passthrough until the filter engine is implemented: L in -> all L outs,
	// R in -> all R outs (R normalled to L when unpatched), per polyphony channel.
	void process(const ProcessArgs& args) override {
		int channels = std::max({1, inputs[AUDIO_INPUT].getChannels(),
		                            inputs[AUDIO_INPUT_R].getChannels()});
		bool rConnected = inputs[AUDIO_INPUT_R].isConnected();

		for (int c = 0; c < channels; c++) {
			float l = inputs[AUDIO_INPUT].getPolyVoltage(c);
			float r = rConnected ? inputs[AUDIO_INPUT_R].getPolyVoltage(c) : l;
			for (int out : {LP_OUTPUT, BP_OUTPUT, HP_OUTPUT, MIX_OUTPUT})
				outputs[out].setVoltage(l, c);
			for (int out : {LP_OUTPUT_R, BP_OUTPUT_R, HP_OUTPUT_R, MIX_OUTPUT_R})
				outputs[out].setVoltage(r, c);
		}
		for (int out = 0; out < NUM_OUTPUTS; out++)
			outputs[out].setChannels(channels);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "screaming", json_boolean(screaming));
		json_object_set_new(root, "panelTheme", json_integer(panelTheme));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "screaming");
		if (s)
			screaming = json_boolean_value(s);
		json_t* p = json_object_get(root, "panelTheme");
		if (p)
			panelTheme = json_integer_value(p);
	}
};


struct YellowjacketWidget : ModuleWidget {
	app::SvgPanel* panel = nullptr;

	// Swap the faceplate SVG for the selected theme (0 = charcoal, 1 = gold).
	void setPanelTheme(int t) {
		std::string f = (t == 1) ? "res/Yellowjacket-gold.svg" : "res/Yellowjacket.svg";
		panel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, f)));
	}

	YellowjacketWidget(Yellowjacket* module) {
		setModule(module);
		panel = createPanel(asset::plugin(pluginInstance, "res/Yellowjacket.svg"));
		setPanel(panel);
		if (module)
			setPanelTheme(module->panelTheme);

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions mirror res/Yellowjacket.svg (vcv-panel-gen), 10 HP.
		// Top cluster: Res / Freq hero / Drive, each over its attenuverter + CV jack.
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.467, 32.000)), module, Yellowjacket::RES_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.400, 29.200)), module, Yellowjacket::FREQ_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(40.333, 32.000)), module, Yellowjacket::DRIVE_PARAM));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(10.467, 45.000)), module, Yellowjacket::RES_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(25.400, 45.000)), module, Yellowjacket::FREQ_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(40.333, 45.000)), module, Yellowjacket::DRIVE_CV_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.467, 56.250)), module, Yellowjacket::RES_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.400, 56.250)), module, Yellowjacket::FREQ_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.333, 56.250)), module, Yellowjacket::DRIVE_INPUT));

		// Audio input, centered in the gap between the two zones.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.550, 69.550)), module, Yellowjacket::AUDIO_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.250, 69.550)), module, Yellowjacket::AUDIO_INPUT_R));

		// Lower zone. Left column: Blend CV / Blend knob / Mix. Right column: HP / BP / LP.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.480, 86.750)), module, Yellowjacket::BLEND_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.450, 86.750)), module, Yellowjacket::HP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.150, 86.750)), module, Yellowjacket::HP_OUTPUT_R));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(13.497, 101.950)), module, Yellowjacket::BLEND_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.447, 101.950)), module, Yellowjacket::BP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.147, 101.950)), module, Yellowjacket::BP_OUTPUT_R));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(8.650, 117.150)), module, Yellowjacket::MIX_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(18.350, 117.150)), module, Yellowjacket::MIX_OUTPUT_R));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.450, 117.150)), module, Yellowjacket::LP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.150, 117.150)), module, Yellowjacket::LP_OUTPUT_R));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Yellowjacket*>(module);
		if (!m) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Character"));
		menu->addChild(createMenuItem("Tame (limited)",
			!m->screaming ? "✓" : "",
			[m]() { m->screaming = false; }));
		menu->addChild(createMenuItem("Screaming (self-oscillates)",
			m->screaming ? "✓" : "",
			[m]() { m->screaming = true; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Panel",
			{"Charcoal", "Gold"},
			[m]() -> size_t { return m->panelTheme; },
			[m, this](size_t i) { m->panelTheme = (int)i; setPanelTheme((int)i); }));
	}
};

Model* modelYellowjacket = createModel<Yellowjacket, YellowjacketWidget>("Yellowjacket");
