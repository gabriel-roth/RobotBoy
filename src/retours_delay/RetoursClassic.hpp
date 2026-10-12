#pragma once
#include "RetoursEngine.hpp"

#include "host/controls.h"
#include "retours_delay_dsp/retours_dsp.h"
#include "retours_block_runtime.h"
#include "classic_wet_filter.h"
#include "fx/reverb.h"

// Retours' classic engine: the v2.1 delay (retours_delay_dsp) and its
// wrapper, lifted from Retours.cpp at v2.1 so patches made before 2.2 sound
// as they did. Added to v2.1: the Filter (Beads' bandwidth filter on the wet
// signal), the Reverb (classic Particules' reverb after the mix) and the
// GATE jack (a 1 ms pulse per delay tick). The saved settings live on the
// module.
class RetoursClassic : public RetoursEngine {
 public:
	// Same rationale as Particules: one block size for both hosts (identical
	// CV conditioning cadence, amortized SetParameters cost on VCV). 64
	// samples of I/O latency (1.3 ms @ 48 kHz).
	static constexpr size_t kWrapperBlockSize = 64;
	static_assert(kWrapperBlockSize <= retours_delay_dsp::kMaxBlockSize,
		"Retours wrapper block size must not exceed retours_delay_dsp::kMaxBlockSize");

	RetoursClassic(Retours& m, float sample_rate);
	~RetoursClassic() override;

	bool ok() const override { return dsp_memory_ != nullptr && reverb_buffer_ != nullptr; }
	void process(const rack::Module::ProcessArgs& args) override;
	void onSampleRateChange(float sample_rate) override;
	void onReset() override;
	// v2.1 greyed out Clear buffer while nothing was recorded, and Clear
	// saved tempo while no tempo was held.
	bool clearDisabled() const override { return processor_.BufferEmpty(); }
	bool clearTempoDisabled() const override { return !processor_.IsClocked(); }
	bool frozen() const override { return frozen_; }

 private:
	void updateSlowParams(bool frozen);
	void updateFilterAndReverb();
	void ResetWetFilter();
	void InitReverb();

	Retours& m_;
	bool frozen_ = false;
	float sample_rate_ = 48000.f;

	// DSP
	retours_delay_dsp::RetoursProcessor processor_;
	void* dsp_memory_ = nullptr;

	// Buffer pattern
	RetoursBlockRuntime<kWrapperBlockSize> block_runtime_;
	retours_delay_dsp::StereoFrame scratch_output_buf_[kWrapperBlockSize] = {};

	// Cached params struct (populated by updateSlowParams, consumed by Process)
	retours_delay_dsp::RetoursParameters params_;

	rack::dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on SLICE gate
	rack::dsp::SchmittTrigger clock_gate_;    // same for CLOCK jack
	bool prev_clock_button_ = false;          // CLOCK momentary button edge detect
	bool metamodule_fpu_configured_ = false;

	// Pitch knob cache: pitchKnobToSemitones() is a linear search; skip it when
	// the knob hasn't moved (knobs are human-speed, not audio-rate).
	float cached_pitch_knob_      = -999.f;
	float cached_pitch_semitones_ = 0.f;
	// Last quality state written to LEDs — skip redundant setBrightness calls.
	int   light_quality_state_    = -1;
	// Cable-connectivity cache (M7): isConnected() is a jack-pointer check,
	// cheap on its own, but was being called every sample for a value that
	// only needs re-evaluating once per 64-sample block. Refreshed in the
	// BlockReady() branch; the output-fold (OUT_R) and mono-detect (IN_R)
	// consumers already tolerate the resulting <=64-sample (1.3 ms) latency
	// on a cable (dis)connect.
	bool out_r_connected_ = false;
	bool in_r_connected_  = false;
	// CLOCK_LIGHT blink phase, advanced once per block by
	// blockFrames / (period * sampleRate); wraps at 1.0.
	float clock_light_phase_       = 0.f;
	// Decaying brightness of the Clock light (see kClockFlashDecaySeconds).
	float clock_light_level_       = 0.f;

	// --- Filter: Beads mode's knob law, dead band and CV path ---
	retours_classic::BandwidthFilter wet_filter_;
	robotboy_beads::PotChannel filter_pot_;
	robotboy_beads::CvChannel filter_cv_;

	// --- Reverb: classic Particules' reverb, after the dry/wet mix ---
	particules_dsp::Reverb reverb_;
	float* reverb_buffer_ = nullptr;
	robotboy_beads::CvChannel reverb_cv_;
	float reverb_amount_ = 0.f;
	// Set once the reverb has had a non-zero amount since its last Init, so
	// a tank that was never fed is skipped entirely (bit-identical to v2.1).
	bool reverb_fed_ = false;

	// --- GATE: a 1 ms pulse at each Clock light flash (delay tick) ---
	bool prev_flash_ = false;
	int gate_samples_left_ = 0;
};
