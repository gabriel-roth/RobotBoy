#pragma once
#include "ParticulesEngine.hpp"

#include "host/controls.h"
#include "host/engine_host.h"
#include "host/pitch_lock.h"

#include <atomic>

// Particules' Beads engine: the granular half of Mutable Instruments Beads,
// running on the Beads source (src/beads). Maps the panel onto what the
// Beads firmware reads from its pots and jacks each block (beads.cc).
class ParticulesBeads : public ParticulesEngine, public robotboy_beads::ControlSource {
 public:
	ParticulesBeads(Particules& m, float sample_rate);

	bool ok() const override { return ok_; }
	void process(const rack::Module::ProcessArgs& args) override;
	void onSampleRateChange(float sample_rate) override;
	void onReset() override;
	void requestClear() override { clear_requested_.store(true); }
	bool clearDisabled() const override { return host_.buffer_empty(); }
	bool frozen() const override { return frozen_; }
	void appendMenu(rack::ui::Menu* menu) override;

	// Called by the engine host at the start of every Beads block.
	void ReadControls(robotboy_beads::BlockControls* c) override;

 private:
	void InitControlChannels();
	static beads::RandomizedParameter Randomized(float base, float attenuverter,
	                                             bool has_cv, float cv);

	Particules& m_;
	bool ok_ = false;
	bool frozen_ = false;

	robotboy_beads::EngineHost host_;
	robotboy_beads::ScalePitchLock pitch_lock_;

	// Beads' pot and CV conditioning (cv_reader.cc), run once per engine block.
	robotboy_beads::PotChannel time_pot_, size_pot_, shape_pot_, pitch_pot_, density_pot_;
	robotboy_beads::PotChannel time_ar_pot_, size_ar_pot_, shape_ar_pot_, pitch_ar_pot_;
	robotboy_beads::CvChannel time_cv_, size_cv_, shape_cv_, pitch_cv_, density_cv_;
	robotboy_beads::CvChannel feedback_cv_, dry_wet_cv_, reverb_cv_;

	// Seed button runtime state (the locked/unlocked state is saved, so it
	// lives on the module as seed_locked_).
	bool seed_held_ = false;          // pressed and not yet long-pressed
	bool prev_seed_button_ = false;
	float seed_hold_seconds_ = 0.f;
	float seed_pulse_phase_ = 0.f;
	rack::dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on FREEZE gate
	rack::dsp::SchmittTrigger seed_gate_;     // same for SEED
	std::atomic<bool> clear_requested_{false};
	bool metamodule_fpu_configured_ = false;

	// Grain LED. (The input-level readout, input_peak_, is on the module.)
	bool prev_gate_ = false;
	float grain_led_ = 0.f;
	int light_divider_ = 0;
	int light_quality_state_ = -1;
};
