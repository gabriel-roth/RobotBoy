#pragma once
#include "plugin.hpp"
#include "classic_mode.h"
#include "RetoursEngine.hpp"

#include <atomic>
#include <memory>

// Retours: a delay with two engines. Beads mode (the default) is the delay
// mode of Mutable Instruments Beads (RetoursBeads); classic mode runs the
// v2.1 engine, for patches made before 2.2. The module owns the panel, the
// saved settings and the mode; the engine adapter does the audio.
struct Retours : Module, robotboy::ClassicModeSource {
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

	// --- Shared state: settings both engines use ---
	int quality_state_ = 0;   // 0-3, same encoding/colors as Particules
	bool prev_quality_button_ = false;
	// Clear buffer / Clear saved tempo: set by the menu or a reset, consumed
	// by the active engine at its next block.
	std::atomic<bool> clear_requested_{false};
	std::atomic<bool> clear_tempo_requested_{false};
	// The active engine's state for the menu, published by the audio thread
	// each frame, so the UI thread never reads the engine itself.
	std::atomic<bool> ui_frozen_{false};
	std::atomic<bool> ui_clear_disabled_{true};
	std::atomic<bool> ui_clear_tempo_disabled_{true};

	// --- Beads-only saved state ---
	// Input gain (Beads' auto gain by default), shared menu with Particules
	// (beads_menu.hpp). The engine writes input_peak_ (+-5 V = 1.0) and
	// gain_db_readout_, and consumes recalibrate_requested_.
	bool auto_gain_ = true;
	float manual_gain_db_ = 0.f;
	bool tape_hiss_ = true;   // Scorched cassette's synthetic hiss
	std::atomic<bool> recalibrate_requested_{false};
	std::atomic<float> input_peak_{0.f};
	std::atomic<float> gain_db_readout_{0.f};

	// --- Classic-only saved state ---
	// v2.1's "Time change response": retours_delay_dsp::TimeChangeMode
	// (0 = Tape (doppler), 1 = Crossfade). Saved as "timeChangeMode".
	int time_change_mode_ = 0;

	// --- Mode ---
	// Written by the audio thread; atomic because the Pitch tooltip and the
	// menu read it.
	std::atomic<bool> classic_{false};
	// Applied by the next process(): -1 = none; 0/1 = a live switch to
	// Beads/classic (menu), refused while sliced; 2/3 = the same, forced
	// (patch or preset load, undo/redo), applied even while sliced.
	enum : int { kModeNone = -1, kModeLive = 0, kModeForced = 2 };
	std::atomic<int> pending_mode_{kModeNone};
	std::unique_ptr<RetoursEngine> engine_;
	float mute_seconds_ = 0.f;
	bool processed_any_ = false;   // a load before the first frame switches unmuted
	float sample_rate_ = 48000.f;  // what the factories size a new engine for

	Retours();

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
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
};

// The gain menu's readout of the gain auto gain applies (beads_menu.hpp).
inline float BeadsMenuGainDb(Retours* m) { return m->gain_db_readout_.load(std::memory_order_relaxed); }
