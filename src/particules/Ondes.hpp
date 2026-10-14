#pragma once
#include "plugin.hpp"
#include "OndesEngine.hpp"

#include <memory>

// Ondes: a wavetable oscillator, the wavetable voice of Mutable Instruments
// Beads (OndesBeads). The module owns the panel.
struct Ondes : Module {
	enum ParamId {
		PITCH_PARAM,
		POSITION_PARAM,
		POSITION_AMT_PARAM,
		BANK_PARAM,
		BANK_AMT_PARAM,
		PARAMS_LEN
	};
	enum InputId { VOCT_INPUT, BANK_INPUT, POSITION_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	// Lives as long as the module, so the panel display can read it from
	// the UI thread.
	std::unique_ptr<OndesEngine> engine_;

	Ondes();

	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void process(const ProcessArgs& args) override;
};
