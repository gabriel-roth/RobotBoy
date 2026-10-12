#pragma once
#include "plugin.hpp"
#include "classic_mode.h"
#include "ParticulesEngine.hpp"

#include <atomic>
#include <memory>

// Particules: a granular processor with two engines. Beads mode (the
// default) runs the Mutable Instruments Beads source (ParticulesBeads);
// classic mode runs the v2.1 engine, for patches made before 2.2. The
// module owns the panel, the saved settings and the mode; the engine
// adapter does the audio.
struct Particules : Module, robotboy::ClassicModeSource {
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

	// --- Shared state: settings both engines use ---
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
	// Read by the gain menu (beads_menu.hpp) while it is open, so they live
	// here rather than in an engine a mode switch can destroy. The engine
	// writes input_peak_ (+-5 V = 1.0) and gain_db_readout_, and consumes
	// recalibrate_requested_.
	std::atomic<bool> recalibrate_requested_{false};
	float input_peak_ = 0.f;
	float gain_db_readout_ = 0.f;
	// Clear buffer: set by the menu or a reset, consumed by the active
	// engine at its next block.
	std::atomic<bool> clear_requested_{false};
	// The active engine's state for the menu, published by the audio thread
	// each frame, so the UI thread never reads the engine itself.
	std::atomic<bool> ui_frozen_{false};
	std::atomic<bool> ui_clear_disabled_{true};

	// --- Beads-only saved state ---
	// Beads' Seed button. Locked (the default) is Beads' normal state:
	// Density alone makes grains, and a patched Seed input is a clock to
	// divide or a trigger to gate by probability. A press unlocks it and
	// makes grains while held (with Seed patched, while either is high); a
	// 4-second hold locks it again. Saved as "seedState": 0 = locked, 1 =
	// unlocked (the old "Seed CV mode" menu's Triggers/Gates).
	bool seed_locked_ = true;
	bool tape_hiss_ = true;   // Scorched cassette's synthetic hiss

	// --- Classic-only saved state ---
	int classic_seed_mode_ = 0;   // 0 = Triggers, 1 = Gates ("classicSeedMode")

	// --- Mode ---
	// Written by the audio thread; atomic because the Pitch tooltip and the
	// menu read it.
	std::atomic<bool> classic_{false};
	// Applied by the next process(): -1 = none; 0/1 = a live switch to
	// Beads/classic (menu), refused while frozen; 2/3 = the same, forced
	// (patch or preset load, undo/redo), applied even while frozen.
	enum : int { kModeNone = -1, kModeLive = 0, kModeForced = 2 };
	std::atomic<int> pending_mode_{kModeNone};
	std::unique_ptr<ParticulesEngine> engine_;
	float mute_seconds_ = 0.f;
	bool processed_any_ = false;   // a load before the first frame switches unmuted
	float sample_rate_ = 48000.f;  // what the factories size a new engine for

	Particules();

	bool classicMode() const override { return classic_.load(std::memory_order_relaxed); }
	// The mode the menu shows: a requested switch, else the running mode.
	bool displayedClassicMode() const {
		const int pending = pending_mode_.load(std::memory_order_acquire);
		return pending >= 0 ? (pending & 1) != 0 : classicMode();
	}
	void requestMode(bool classic);            // UI thread
	void applyPendingMode(float sample_rate);  // audio thread

	void process(const ProcessArgs& args) override;
	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onReset(const ResetEvent& e) override;
	void onPortChange(const PortChangeEvent& e) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
};

// The gain menu's readout of the gain auto gain applies (beads_menu.hpp).
inline float BeadsMenuGainDb(Particules* m) { return m->gain_db_readout_; }
