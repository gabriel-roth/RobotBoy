#pragma once
#include "plugin.hpp"
#include "classic_mode.h"
#include "OndesEngine.hpp"

#include <atomic>
#include <memory>

// Ondes: a wavetable oscillator with two engines. Beads mode (the default)
// is the wavetable voice of Mutable Instruments Beads (OndesBeads); classic
// mode runs the v2.1 oscillator over its 24 banks (OndesClassic), for
// patches made before 2.2. The module owns the panel and the mode.
struct Ondes : Module, robotboy::ClassicModeSource {
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

	// --- Mode ---
	// The running mode. Written on the audio thread only.
	std::atomic<bool> classic_{false};
	// A switch the UI thread (menu) or dataFromJson asked for, applied at the
	// next process(): kModeNone, or 0/1 = Beads/classic.
	enum : int { kModeNone = -1 };
	std::atomic<int> pending_mode_{kModeNone};
	// Both engines live as long as the module. Switching stores a pointer,
	// so the display never reads a freed engine.
	std::unique_ptr<OndesEngine> beads_;
	std::unique_ptr<OndesEngine> classic_engine_;
	std::atomic<OndesEngine*> active_{nullptr};

	Ondes();

	bool classicMode() const override { return classic_.load(std::memory_order_relaxed); }
	// The mode a pending switch will land on, else the running one.
	bool displayedClassicMode() const {
		const int pending = pending_mode_.load(std::memory_order_acquire);
		return pending >= 0 ? pending != 0 : classicMode();
	}
	void requestMode(bool classic);   // UI thread
	void applyPendingMode();          // audio thread

	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void process(const ProcessArgs& args) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
};
