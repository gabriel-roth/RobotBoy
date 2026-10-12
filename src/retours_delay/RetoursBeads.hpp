#pragma once
#include "RetoursEngine.hpp"

#include "host/controls.h"
#include "host/engine_host.h"

// Retours' Beads engine: the delay mode of Mutable Instruments Beads (the
// mode Beads enters with Size fully clockwise), running on the Beads source
// (src/beads). Interval is Beads' Density knob in that mode, Slice is its
// Freeze, and Clock is its Seed button and input.
class RetoursBeads : public RetoursEngine, public robotboy_beads::ControlSource {
 public:
	RetoursBeads(Retours& m, float sample_rate);

	bool ok() const override { return ok_; }
	void process(const rack::Module::ProcessArgs& args) override;
	void onSampleRateChange(float sample_rate) override;
	void onReset() override {}
	bool clearDisabled() const override { return host_.buffer_empty(); }
	// Nothing to clear, or a patched clock would set the tempo again.
	bool clearTempoDisabled() const override { return !tempo_held_ || clock_patched_; }
	bool frozen() const override { return frozen_; }

	// Called by the engine host at the start of every Beads block.
	void ReadControls(robotboy_beads::BlockControls* c) override;

 private:
	static beads::RandomizedParameter Randomized(float base, float attenuverter,
	                                             bool has_cv, float cv);

	Retours& m_;
	bool ok_ = false;
	bool frozen_ = false;
	bool clock_patched_ = false;

	robotboy_beads::EngineHost host_;

	// Beads' pot and CV conditioning (cv_reader.cc), run once per engine block.
	robotboy_beads::PotChannel time_pot_, pitch_pot_, shape_pot_, interval_pot_, filter_pot_;
	robotboy_beads::PotChannel time_ar_pot_, pitch_ar_pot_, shape_ar_pot_;
	robotboy_beads::CvChannel time_cv_, pitch_cv_, shape_cv_, interval_cv_;
	robotboy_beads::CvChannel feedback_cv_, dry_wet_cv_, reverb_cv_, filter_cv_;

	rack::dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on SLICE gate
	rack::dsp::SchmittTrigger clock_gate_;    // same for CLOCK jack
	bool prev_clock_button_ = false;
	bool metamodule_fpu_configured_ = false;

	// Tempo. Beads starts with its Seed button "locked": no tempo, Interval
	// sets the time directly. A tap (or a clock tick) unlocks it, and the
	// tempo then holds, even after the clock is unpatched, until "Clear saved
	// tempo" locks it again. Taps and ticks are latched per sample and
	// consumed once per engine block.
	bool tempo_held_ = false;
	bool tap_pending_ = false;
	bool clock_tick_pending_ = false;

	bool prev_gate_ = false;
	float clock_light_level_ = 0.f;
	int light_divider_ = 0;
	int light_quality_state_ = -1;
};
