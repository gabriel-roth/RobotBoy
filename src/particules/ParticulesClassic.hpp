#pragma once
#include "ParticulesEngine.hpp"

#include "particules_dsp/particules_dsp.h"
#include <particules_dsp/control_conditioner.h>
#include "particules_block_runtime.h"

// Particules' classic engine: the v2.1 granular processor (particules_dsp)
// and its wrapper, lifted from Particules.cpp at v2.1 so patches made
// before 2.2 sound as they did. The differences from v2.1: the Seed button
// is OR'd with the Seed jack, grain triggers go to the GATE jack (Out R is
// always the right channel), and the saved settings live on the module.
class ParticulesClassic : public ParticulesEngine {
 public:
	// One block size for both hosts: matches MetaModule exactly (identical CV
	// conditioning cadence) and amortizes updateSlowParams/SetParameters ~64×
	// on VCV. Costs 64 samples of I/O latency (1.3 ms @ 48 kHz).
	static constexpr size_t kWrapperBlockSize = 64;
	static_assert(kWrapperBlockSize <= particules_dsp::kMaxBlockSize,
		"Particules wrapper block size must not exceed particules_dsp::kMaxBlockSize");

	ParticulesClassic(Particules& m, float sample_rate);
	~ParticulesClassic() override;

	bool ok() const override { return dsp_memory_ != nullptr; }
	void process(const rack::Module::ProcessArgs& args) override;
	void onSampleRateChange(float sample_rate) override;
	void onReset() override;
	void onPortChange(const rack::Module::PortChangeEvent& e) override;
	// v2.1 greyed out Clear buffer while nothing was recorded.
	bool clearDisabled() const override { return processor_.BufferEmpty(); }
	bool frozen() const override { return frozen_; }

 private:
	void ResetControlConditioners();
	void ApplyScaleToProcessor();
	void updateSlowParams(bool frozen);

	Particules& m_;
	bool frozen_ = false;

	// DSP
	particules_dsp::ParticulesProcessor processor_;
	void* dsp_memory_ = nullptr;

	// Buffer pattern
	ParticulesBlockRuntime<kWrapperBlockSize> block_runtime_;
	particules_dsp::StereoFrame scratch_output_buf_[kWrapperBlockSize] = {};

	// Cached params struct (populated by updateSlowParams, consumed by Process)
	particules_dsp::ParticulesParameters params_;

	rack::dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on FREEZE gate
	rack::dsp::SchmittTrigger seed_gate_;     // same for SEED
	bool prev_in_l_connected_ = false;
	bool prev_in_r_connected_ = false;
	bool needs_calibration_ = true;  // Calibrate on first process() if auto_gain_
	bool metamodule_fpu_configured_ = false;

	// Pitch knob cache: pitchKnobToSemitones() is a linear search; skip it when
	// the knob hasn't moved (knobs are human-speed, not audio-rate).
	float cached_pitch_knob_      = -999.f;
	float cached_pitch_semitones_ = 0.f;
	particules_dsp::ControlConditioner time_cv_conditioner_;
	particules_dsp::ControlConditioner size_cv_conditioner_;
	particules_dsp::ControlConditioner shape_cv_conditioner_;
	particules_dsp::ControlConditioner pitch_cv_conditioner_;
	// Last quality state written to LEDs — skip redundant setBrightness calls.
	int   light_quality_state_    = -1;
};
