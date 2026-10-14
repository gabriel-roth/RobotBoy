#pragma once
#include "plugin.hpp"
#include "ParticulesEngine.hpp"
#include "host/grain_feed.h"

#include <atomic>
#include <memory>

// Particules: a granular processor running the Mutable Instruments Beads
// source. The module owns the panel and the saved settings; the engine
// adapter (ParticulesBeads) does the audio.
struct Particules : Module {
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
		FEEDBACK_AMT_PARAM,
		REVERB_AMT_PARAM,
		DRY_WET_AMT_PARAM,
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
		FREEZE_BUTTON_LIGHT,
		GRAIN_LIGHT,
		SEED_LIGHT,
		LIGHTS_LEN
	};

	// Lucarne's display feed. The engine captures into it while a Lucarne
	// sits to the right. The UI thread reads it from Lucarne's widget.
	robotboy_beads::GrainFeed grain_feed_;
	int expander_check_ = 0;
	int quality_state_ = 0;   // 0-3: Bright, Cold, Sunny, Scorched
	bool prev_quality_button_ = false;
	// Lock pitch: 0 = off, 1-2 = octaves (+ fifths), 3-7 = scales (see
	// particules_scales.h); the root applies to the scale modes only.
	int pitch_scale_ = 0;
	int pitch_root_ = 0;
	// Menu/load/reset mutate this state on the UI thread; the audio thread
	// applies it at the next engine block.
	std::atomic<bool> scale_dirty_{true};
	bool auto_gain_ = true;
	float manual_gain_db_ = 0.f;
	// Read by the gain menu (beads_menu.hpp) while it is open. The engine
	// writes input_peak_ (+-5 V = 1.0) and gain_db_readout_, and consumes
	// recalibrate_requested_.
	std::atomic<bool> recalibrate_requested_{false};
	// Atomic (relaxed): the audio thread writes them while the menu reads.
	std::atomic<float> input_peak_{0.f};
	std::atomic<float> gain_db_readout_{0.f};
	// Clear buffer: set by the menu or a reset, consumed by the engine at
	// its next block.
	std::atomic<bool> clear_requested_{false};
	// The engine's state for the menu, published by the audio thread
	// each frame, so the UI thread never reads the engine itself.
	std::atomic<bool> ui_clear_disabled_{true};

	// Beads' Seed button. Locked (the default) is Beads' normal state:
	// Density alone makes grains, and a patched Seed input is a clock to
	// divide or a trigger to gate by probability. A press unlocks it and
	// makes grains while held (with Seed patched, while either is high); a
	// 4-second hold locks it again. Saved as "seedState": 0 = locked, 1 =
	// unlocked (the old "Seed CV mode" menu's Triggers/Gates).
	bool seed_locked_ = true;
	bool tape_hiss_ = true;   // Scorched cassette's synthetic hiss

	std::unique_ptr<ParticulesEngine> engine_;

	Particules();

	void process(const ProcessArgs& args) override;
	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onReset(const ResetEvent& e) override;
	void onPortChange(const PortChangeEvent& e) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
};

// The gain menu's readout of the gain auto gain applies (beads_menu.hpp).
inline float BeadsMenuGainDb(Particules* m) { return m->gain_db_readout_.load(std::memory_order_relaxed); }
