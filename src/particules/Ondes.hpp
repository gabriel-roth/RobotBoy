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

	// Context-menu anti-aliasing: the voice runs at this multiple of the
	// host rate (1 = off, as on Beads; 2 or 4).
	int oversampling_ = 1;

	Ondes();

	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onReset(const ResetEvent& e) override;
	void process(const ProcessArgs& args) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
};
