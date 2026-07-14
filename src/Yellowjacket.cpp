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
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "screaming");
		if (s)
			screaming = json_boolean_value(s);
	}
};


struct YellowjacketWidget : ModuleWidget {
	YellowjacketWidget(Yellowjacket* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Yellowjacket.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions mirror res/Yellowjacket.svg (vcv-panel-gen).
		// Knobs: Res / Freq hero / Drive.
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(13.493, 33.200)), module, Yellowjacket::RES_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(30.480, 30.400)), module, Yellowjacket::FREQ_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(47.467, 33.200)), module, Yellowjacket::DRIVE_PARAM));

		// Attenuverters, one per column.
		addParam(createParamCentered<Trimpot>(mm2px(Vec(13.493, 46.200)), module, Yellowjacket::RES_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(30.480, 46.200)), module, Yellowjacket::FREQ_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(47.467, 46.200)), module, Yellowjacket::DRIVE_CV_PARAM));

		// CV jacks, stacked under each attenuverter.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.493, 58.550)), module, Yellowjacket::RES_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.480, 58.550)), module, Yellowjacket::FREQ_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(47.467, 58.550)), module, Yellowjacket::DRIVE_INPUT));

		// Below the rect — left column: In, Blend, Mix; right column: HP, BP, LP.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.350, 73.050)), module, Yellowjacket::AUDIO_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.050, 73.050)), module, Yellowjacket::AUDIO_INPUT_R));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.910, 73.050)), module, Yellowjacket::HP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.610, 73.050)), module, Yellowjacket::HP_OUTPUT_R));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.203, 92.700)), module, Yellowjacket::BLEND_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.913, 92.700)), module, Yellowjacket::BP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.613, 92.700)), module, Yellowjacket::BP_OUTPUT_R));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(9.350, 116.050)), module, Yellowjacket::MIX_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(19.050, 116.050)), module, Yellowjacket::MIX_OUTPUT_R));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.910, 116.050)), module, Yellowjacket::LP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.610, 116.050)), module, Yellowjacket::LP_OUTPUT_R));
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
	}
};

Model* modelYellowjacket = createModel<Yellowjacket, YellowjacketWidget>("Yellowjacket");
