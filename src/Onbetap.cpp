#include "plugin.hpp"

// Onbetap — Polivoks-style multimode filter.
//
// SHELL ONLY: the DSP is not implemented yet. process() passes the stereo
// input straight to the output; the Cutoff/Q/Drive/Mode controls, their CV
// inputs, and the Character menu toggle are wired to params/ports but do
// nothing until the filter engine lands. Panel positions mirror res/Onbetap.svg
// (see panel-specs/onbetap.yaml). configParam ranges are provisional.

struct Onbetap : Module {
	enum ParamId {
		CUTOFF_PARAM,
		CUTOFF_CV_PARAM,   // Cutoff CV attenuverter
		RES_PARAM,         // Resonance (labelled "Q" on the panel)
		RES_CV_PARAM,      // Resonance CV attenuverter
		DRIVE_PARAM,
		DRIVE_CV_PARAM,    // Drive CV attenuverter
		MODE_PARAM,        // 0=LP 1=BP 2=HP 3=Notch 4=Peak
		NUM_PARAMS
	};
	enum InputId {
		AUDIO_INPUT,
		AUDIO_INPUT_R,
		CUTOFF_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
		NUM_INPUTS
	};
	enum OutputId {
		AUDIO_OUTPUT,
		AUDIO_OUTPUT_R,
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	// Character: false = tamed (stable, playable), true = vintage (drifty,
	// untuned self-oscillation). Persisted now; acted on once the DSP lands.
	bool vintageDrift = false;

	Onbetap() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(CUTOFF_PARAM, std::log2(20.f), std::log2(20000.f), std::log2(750.f),
		            "Cutoff", " Hz", 2.f);
		configParam(CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "Cutoff CV", "x");
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(RES_CV_PARAM, -1.f, 1.f, 1.f, "Resonance CV", "x");
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 1.f, "Drive CV", "x");
		configSwitch(MODE_PARAM, 0.f, 4.f, 0.f, "Mode",
		             {"Lowpass", "Bandpass", "Highpass", "Notch", "Peak"});

		configInput(AUDIO_INPUT,   "Audio L");
		configInput(AUDIO_INPUT_R, "Audio R");
		configInput(CUTOFF_INPUT,  "Cutoff CV");
		configInput(RES_INPUT,     "Resonance CV");
		configInput(DRIVE_INPUT,   "Drive CV");

		configOutput(AUDIO_OUTPUT,   "Audio L");
		configOutput(AUDIO_OUTPUT_R, "Audio R");

		configBypass(AUDIO_INPUT,   AUDIO_OUTPUT);
		configBypass(AUDIO_INPUT_R, AUDIO_OUTPUT_R);
	}

	// Passthrough until the filter engine is implemented.
	void process(const ProcessArgs& args) override {
		for (auto io : {std::make_pair(AUDIO_INPUT, AUDIO_OUTPUT),
		                std::make_pair(AUDIO_INPUT_R, AUDIO_OUTPUT_R)}) {
			int channels = std::max(1, inputs[io.first].getChannels());
			outputs[io.second].setChannels(channels);
			for (int c = 0; c < channels; c++)
				outputs[io.second].setVoltage(inputs[io.first].getVoltage(c), c);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "vintageDrift", json_boolean(vintageDrift));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* v = json_object_get(root, "vintageDrift");
		if (v)
			vintageDrift = json_boolean_value(v);
	}
};


struct OnbetapWidget : ModuleWidget {
	OnbetapWidget(Onbetap* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Onbetap.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Knobs: Q (left) / Cutoff hero (centre) / Drive (right).
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.80, 37.000)), module, Onbetap::RES_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 30.000)), module, Onbetap::CUTOFF_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(39.00, 37.000)), module, Onbetap::DRIVE_PARAM));

		// Attenuverters, one per column.
		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.80, 49.000)), module, Onbetap::RES_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(25.40, 49.000)), module, Onbetap::CUTOFF_CV_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(39.00, 49.000)), module, Onbetap::DRIVE_CV_PARAM));

		// CV jacks, one per column.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.80, 67.350)), module, Onbetap::RES_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.40, 67.350)), module, Onbetap::CUTOFF_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(39.00, 67.350)), module, Onbetap::DRIVE_INPUT));

		// Mode hero (snap enabled via configSwitch).
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(25.40, 89.833)), module, Onbetap::MODE_PARAM));

		// Stereo audio I/O.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.35, 114.017)), module, Onbetap::AUDIO_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.05, 114.017)), module, Onbetap::AUDIO_INPUT_R));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(31.75, 114.017)), module, Onbetap::AUDIO_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.45, 114.017)), module, Onbetap::AUDIO_OUTPUT_R));
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = dynamic_cast<Onbetap*>(module);
		if (!m) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Character"));
		menu->addChild(createMenuItem("Tamed (stable)",
			!m->vintageDrift ? "✓" : "",
			[m]() { m->vintageDrift = false; }));
		menu->addChild(createMenuItem("Vintage (drift)",
			m->vintageDrift ? "✓" : "",
			[m]() { m->vintageDrift = true; }));
	}
};


Model* modelOnbetap = createModel<Onbetap, OnbetapWidget>("Onbetap");
