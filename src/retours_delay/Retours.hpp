#pragma once
#include "plugin.hpp"
#include "RetoursEngine.hpp"
#include "host/grain_feed.h"

#include <atomic>
#include <memory>

// Retours: a delay running the delay mode of Mutable Instruments Beads. The
// module owns the panel and the saved settings; the engine adapter
// (RetoursBeads) does the audio.
struct Retours : Module {
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
		FEEDBACK_AR_PARAM,
		DRY_WET_AR_PARAM,
		// Added with the 15 HP panel; appended so older patches still load.
		FILTER_PARAM,
		FILTER_AR_PARAM,
		REVERB_PARAM,
		REVERB_AR_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		SLICE_INPUT,
		CLOCK_INPUT,
		TIME_CV_INPUT,
		INTERVAL_CV_INPUT,
		PITCH_CV_INPUT,
		SHAPE_CV_INPUT,
		FEEDBACK_CV_INPUT,
		DRY_WET_CV_INPUT,
		FILTER_CV_INPUT,
		REVERB_CV_INPUT,
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
		SLICE_BUTTON_LIGHT,
		CLOCK_LIGHT,
		LIGHTS_LEN
	};

	// Lucarne's display feed. The engine captures into it while a Lucarne
	// sits to the right. The UI thread reads it from Lucarne's widget.
	robotboy_beads::GrainFeed grain_feed_;
	int expander_check_ = 0;
	int quality_state_ = 0;   // 0-3, same encoding/colors as Particules
	bool prev_quality_button_ = false;
	// Clear buffer / Clear saved tempo: set by the menu or a reset, consumed
	// by the engine at its next block.
	std::atomic<bool> clear_requested_{false};
	std::atomic<bool> clear_tempo_requested_{false};
	// The engine's state for the menu, published by the audio thread
	// each frame, so the UI thread never reads the engine itself.
	std::atomic<bool> ui_clear_disabled_{true};
	std::atomic<bool> ui_clear_tempo_disabled_{true};

	// Input gain (Beads' auto gain by default), shared menu with Particules
	// (beads_menu.hpp). The engine writes input_peak_ (+-5 V = 1.0) and
	// gain_db_readout_, and consumes recalibrate_requested_.
	bool auto_gain_ = true;
	float manual_gain_db_ = 0.f;
	bool tape_hiss_ = true;   // Scorched cassette's synthetic hiss
	std::atomic<bool> recalibrate_requested_{false};
	std::atomic<float> input_peak_{0.f};
	std::atomic<float> gain_db_readout_{0.f};

	std::unique_ptr<RetoursEngine> engine_;

	Retours();

	void process(const ProcessArgs& args) override;
	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onReset(const ResetEvent& e) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
};

// The gain menu's readout of the gain auto gain applies (beads_menu.hpp).
inline float BeadsMenuGainDb(Retours* m) { return m->gain_db_readout_.load(std::memory_order_relaxed); }
