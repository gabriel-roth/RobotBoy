#include "RetoursClassic.hpp"
#include "Retours.hpp"
#include "beads_menu.hpp"
#include "metamodule_fpu.h"
#include "particules_dsp/types.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef METAMODULE
#ifndef __APPLE__
#include <malloc.h>
#endif
#endif

// The v2.1 Retours wrapper (Retours.cpp at v2.1.0), lifted with as few edits
// as possible so classic renders stay bit-identical to v2.1 while Filter is
// at noon and Reverb at 0. The saved settings it reads (Quality, Time change
// response) are on the module, reached through m_.

// CLOCK_LIGHT flash decay time. The beat flash snaps to full brightness, then
// decays over this long so each pulse spans many video frames and reads evenly
// (a hard on/off flash is ~1-2 frames at fast tempos, which aliases against the
// frame rate and looks uneven).
static constexpr float kClockFlashDecaySeconds = 0.08f;
// Precomputed 1/kClockFlashDecaySeconds (== 12.5f exactly) so the block-rate
// decay can multiply instead of dividing every sample.
static constexpr float kClockFlashDecayRate = 1.f / kClockFlashDecaySeconds;

// GATE pulse length per delay tick.
static constexpr float kGatePulseSeconds = 0.001f;

// The reverb's settings, as classic Particules sets them
// (src/particules/dsp/src/particules_processor.cpp, SetParameters):
// decay 0.3 + 0.65 x amount, diffusion 0.7, +4 dB wet makeup, and a feedback
// low-pass per Quality, warmest on Scorched cassette.
static constexpr float kReverbDiffusion = 0.7f;
static constexpr float kReverbMakeupGain = 1.5849f;  // 10^(4/20)
static constexpr float kReverbLpByQuality[4] = {
	0.7f,   // Bright digital
	0.6f,   // Cold digital
	0.5f,   // Sunny tape
	0.3f,   // Scorched cassette
};

static void* AlignedAlloc(size_t alignment, size_t bytes) {
	void* p = nullptr;
#if defined(METAMODULE) && !defined(SIMULATOR) && !defined(__APPLE__)
	p = memalign(alignment, bytes);
#elif defined(_WIN32)
	p = _aligned_malloc(bytes, alignment);
#else
	if (posix_memalign(&p, alignment, bytes) != 0)
		p = nullptr;
#endif
	return p;
}

static void AlignedFree(void* p) {
#ifdef _WIN32
	_aligned_free(p);
#else
	std::free(p);
#endif
}

RetoursClassic::RetoursClassic(Retours& m, float sample_rate) : m_(m) {
	sample_rate_ = sample_rate;
	// DSP init — also called on sample rate change via onSampleRateChange()
	float sampleRate = sample_rate;
	auto req = retours_delay_dsp::RetoursProcessor::GetMemoryRequirements(sampleRate);
	dsp_memory_ = AlignedAlloc(req.alignment, req.total_bytes);
	if (!dsp_memory_) {
		// processor_.Init() below already no-ops safely on a null pointer,
		// but a failed allocation of this size is worth surfacing instead
		// of silently running with a dead DSP chain. WARN is defined
		// identically by both hosts' logger.hpp (Rack SDK and MetaModule
		// plugin SDK).
		WARN("Retours: dsp_memory_ allocation failed (%zu bytes)", req.total_bytes);
	}
	processor_.Init(dsp_memory_, req.total_bytes, sampleRate);
	processor_.SetWetFilter(&wet_filter_);
	wet_filter_.Init(sampleRate);

	// Filter: the same knob law and CV conditioning as Beads mode
	// (RetoursBeads).
	filter_pot_.Init(robotboy_beads::PotLaw::kAttenuverter, robotboy_beads::kPotLp, 0.f);
	robotboy_beads::InitTimeSizeShapeCv(&filter_cv_);
	robotboy_beads::InitMixCv(&reverb_cv_);

	reverb_buffer_ = static_cast<float*>(
		AlignedAlloc(16, particules_dsp::kReverbBufferSize * sizeof(float)));
	if (!reverb_buffer_)
		WARN("Retours: reverb buffer allocation failed");
	InitReverb();
}

RetoursClassic::~RetoursClassic() {
	AlignedFree(dsp_memory_);
	AlignedFree(reverb_buffer_);
}

void RetoursClassic::InitReverb() {
	reverb_fed_ = false;
	if (reverb_buffer_)
		reverb_.Init(reverb_buffer_, particules_dsp::kReverbBufferSize, sample_rate_);
}

// The filter keeps its state across the dead band; clear it with the buffer
// and on a sample-rate change so old audio doesn't ring out later.
void RetoursClassic::ResetWetFilter() {
	wet_filter_.Init(sample_rate_);
}

void RetoursClassic::onSampleRateChange(float sample_rate) {
	sample_rate_ = sample_rate;
	// Memory requirements are sample-rate-independent (fixed frame budget),
	// so we can reinitialize into the same allocation.
	block_runtime_ = RetoursBlockRuntime<kWrapperBlockSize>{};
	std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
	clock_light_phase_ = 0.f;
	clock_light_level_ = 0.f;
	prev_flash_ = false;
	gate_samples_left_ = 0;
	// Re-arm so a possibly-new audio thread gets FTZ configured.
	metamodule_fpu_configured_ = false;
	if (dsp_memory_) {
		auto req = retours_delay_dsp::RetoursProcessor::GetMemoryRequirements(sample_rate);
		processor_.Init(dsp_memory_, req.total_bytes, sample_rate);
		processor_.SetWetFilter(&wet_filter_);
	}
	ResetWetFilter();
	InitReverb();
}

// The engine half of v2.1's onReset; the module has already reset the
// saved settings.
void RetoursClassic::onReset() {
	block_runtime_          = RetoursBlockRuntime<kWrapperBlockSize>{};
	std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
	clock_light_phase_       = 0.f;
	clock_light_level_       = 0.f;
	prev_flash_ = false;
	gate_samples_left_ = 0;
	m_.clear_requested_.store(false);   // reset clears now; drop any queued menu clear
	m_.clear_tempo_requested_.store(false);
	processor_.ClearBuffer();
	processor_.ClearTappedTempo();   // initialize returns to free-running
	ResetWetFilter();
}

void AppendRetoursClassicMenu(Retours* module, rack::ui::Menu* menu) {
	// --- Time change response ---
	menu->addChild(createIndexSubmenuItem("Time change response",
		{"Tape (doppler)", "Crossfade"},
		[=]() { return module->time_change_mode_; },
		[=](int val) {
			withMenuUndo(module, "change time-change response", [=]() {
				module->time_change_mode_ = clamp(val, 0, 1);
			});
		}
	));
}

void RetoursClassic::updateSlowParams(bool frozen) {
	auto& params = m_.params;
	auto& inputs = m_.inputs;

	params_.density = params[Retours::INTERVAL_PARAM].getValue();
	params_.time     = params[Retours::TIME_PARAM].getValue();
	{
		float raw = params[Retours::PITCH_PARAM].getValue();
		if (raw != cached_pitch_knob_) {
			cached_pitch_knob_      = raw;
			cached_pitch_semitones_ = pitchKnobToSemitones(raw);
		}
		params_.pitch_semitones = cached_pitch_semitones_;
	}
	params_.shape    = params[Retours::SHAPE_PARAM].getValue();
	params_.feedback = params[Retours::FEEDBACK_PARAM].getValue();
	params_.dry_wet  = params[Retours::DRY_WET_PARAM].getValue();

	// All CVs are raw volts here: BaseTimeControl/ArModulator/the mix
	// stages apply their own documented scaling (density_cv: -1 V/oct via
	// exp2; feedback_cv/dry_wet_cv: /5 V direct add; time/pitch/shape_cv:
	// consumed by ArModulator, also /5 V or *12 internally).
	//
	// getVoltage() can return NaN on a hostile/broken patch cable feed
	// (e.g. another module divides by zero upstream); sanitize to 0 V
	// here at the ingestion boundary rather than let it reach
	// RetoursProcessor (which also guards, belt-and-braces, in
	// SetParameters -- see retours_processor.cpp).
	auto sanitizeVoltage = [](float v) { return std::isfinite(v) ? v : 0.f; };
	params_.density_cv = sanitizeVoltage(inputs[Retours::INTERVAL_CV_INPUT].getVoltage());
	params_.time_cv           = sanitizeVoltage(inputs[Retours::TIME_CV_INPUT].getVoltage());
	params_.time_cv_connected = inputs[Retours::TIME_CV_INPUT].isConnected();
	params_.pitch_cv           = sanitizeVoltage(inputs[Retours::PITCH_CV_INPUT].getVoltage());
	params_.pitch_cv_connected = inputs[Retours::PITCH_CV_INPUT].isConnected();
	params_.shape_cv           = sanitizeVoltage(inputs[Retours::SHAPE_CV_INPUT].getVoltage());
	params_.shape_cv_connected = inputs[Retours::SHAPE_CV_INPUT].isConnected();
	// Feedback/Dry-wet CV pass through their panel attenuverters (bipolar
	// scale here; the DSP then applies its documented /5 V add).
	params_.feedback_cv = sanitizeVoltage(inputs[Retours::FEEDBACK_CV_INPUT].getVoltage())
		* params[Retours::FEEDBACK_AR_PARAM].getValue();
	params_.dry_wet_cv  = sanitizeVoltage(inputs[Retours::DRY_WET_CV_INPUT].getVoltage())
		* params[Retours::DRY_WET_AR_PARAM].getValue();

	params_.time_ar  = params[Retours::TIME_AR_PARAM].getValue();
	params_.pitch_ar = params[Retours::PITCH_AR_PARAM].getValue();
	params_.shape_ar = params[Retours::SHAPE_AR_PARAM].getValue();

	params_.clock_tick_offset = block_runtime_.TakeClockTickOffset();
	params_.clock_connected   = inputs[Retours::CLOCK_INPUT].isConnected();
	params_.freeze = frozen;

	params_.quality = static_cast<retours_delay_dsp::QualityMode>(m_.quality_state_);
	params_.time_change_mode      = static_cast<retours_delay_dsp::TimeChangeMode>(
		std::clamp(m_.time_change_mode_, 0, 1));
	// Baked, no UI: input trim fixed at 0 dB (unity), Doppler slew fixed.
	params_.input_trim_db         = 0.f;
	params_.slew_seconds          = retours_delay_dsp::kSlewSecondsDefault;
}

// Once per block: the Filter's bandwidth and the Reverb's settings.
void RetoursClassic::updateFilterAndReverb() {
	auto& params = m_.params;
	auto& inputs = m_.inputs;

	// Filter: Beads' bandwidth = 0.5 * (1 + attenuverter) + Size CV
	// (granular_processor.h), with RetoursBeads' knob law (filter_pot_
	// applies AttenuverterLaw) and CV path. Noon is off.
	const float att = filter_pot_.Process(params[Retours::FILTER_PARAM].getValue());
	float bw = 0.5f * (1.f + att);
	const float filter_cv = filter_cv_.Process(
		inputs[Retours::FILTER_CV_INPUT].getVoltage() * 0.2f)
		* params[Retours::FILTER_AR_PARAM].getValue();
	if (inputs[Retours::FILTER_CV_INPUT].isConnected())
		bw += filter_cv;
	const bool was_active = wet_filter_.active();
	wet_filter_.SetBandwidth(bw);
	// Leaving the dead band: start from a clean filter rather than the
	// state it held when it last went quiet.
	if (wet_filter_.active() && !was_active) {
		ResetWetFilter();
		wet_filter_.SetBandwidth(bw);
	}

	// Reverb: Beads mode's knob + CV x amount law, classic Particules' settings.
	reverb_amount_ = robotboy_beads::MixParameter(params[Retours::REVERB_PARAM].getValue(),
		reverb_cv_.Process(inputs[Retours::REVERB_CV_INPUT].getVoltage() * 0.2f),
		params[Retours::REVERB_AR_PARAM].getValue());
	reverb_.SetAmount(reverb_amount_);
	reverb_.SetDecay(0.3f + reverb_amount_ * 0.65f);
	reverb_.SetDiffusion(kReverbDiffusion);
	reverb_.SetMakeupGain(kReverbMakeupGain);
	reverb_.SetLpCutoff(kReverbLpByQuality[std::clamp(m_.quality_state_, 0, 3)]);
	if (reverb_amount_ > 0.f)
		reverb_fed_ = true;
}

void RetoursClassic::process(const rack::Module::ProcessArgs& args) {
	auto& params = m_.params;
	auto& inputs = m_.inputs;
	auto& outputs = m_.outputs;
	auto& lights = m_.lights;

	if (!metamodule_fpu_configured_) {
		metamodule_fpu_configured_ = true;
		particules::EnableMetaModuleFlushToZero();
	}
	bool freeze_latch = params[Retours::SLICE_PARAM].getValue() > 0.5f;
	freeze_gate_.process(inputs[Retours::SLICE_INPUT].getVoltage(), 0.1f, 1.f);
	bool frozen = freeze_latch || freeze_gate_.isHigh();
	frozen_ = frozen;

	// CLOCK: jack rising edge (Schmitt) OR button rising edge both count
	// as ticks; clock_connected reflects only the jack, so button-only
	// taps take the core's tap-tempo path (see RetoursParameters comment).
	bool clock_gate_was_high = clock_gate_.isHigh();
	clock_gate_.process(inputs[Retours::CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
	bool clock_jack_rising = clock_gate_.isHigh() && !clock_gate_was_high;

	bool clock_button = params[Retours::CLOCK_PARAM].getValue() > 0.5f;
	bool clock_button_rising = clock_button && !prev_clock_button_;
	prev_clock_button_ = clock_button;

	block_runtime_.NoteClockEdgeSample(clock_jack_rising || clock_button_rising,
		block_runtime_.InputIndex());

#ifndef METAMODULE
	// Desktop: momentary button cycles the mode; blocked while frozen.
	bool quality_pressed = params[Retours::QUALITY_PARAM].getValue() > 0.5f;
	if (quality_pressed && !m_.prev_quality_button_ && !frozen)
		m_.quality_state_ = (m_.quality_state_ + 1) % 4;
	m_.prev_quality_button_ = quality_pressed;
#endif

	// Output from previously processed block
	retours_delay_dsp::StereoFrame out = block_runtime_.ReadOutputSample();
	// Reverb after the dry/wet mix, as classic Particules. Skipped entirely
	// at an amount of 0 once the tank is asleep, or if it was never fed.
	if (reverb_buffer_ && (reverb_amount_ > 0.f || (reverb_fed_ && !reverb_.IsAsleep())))
		reverb_.Process(out.l, out.r, &out.l, &out.r);
	outputs[Retours::OUT_L_OUTPUT].setVoltage((out_r_connected_ ? out.l : (out.l + out.r) * 0.5f) * 5.f);
	outputs[Retours::OUT_R_OUTPUT].setVoltage(out.r * 5.f);
	outputs[Retours::GATE_OUTPUT].setVoltage(gate_samples_left_ > 0 ? 10.f : 0.f);
	if (gate_samples_left_ > 0)
		--gate_samples_left_;

	// Accumulate input
	float l = inputs[Retours::IN_L_INPUT].getVoltage() * 0.2f;
	if (!std::isfinite(l)) l = 0.f;
	float r = in_r_connected_ ? inputs[Retours::IN_R_INPUT].getVoltage() * 0.2f : l;
	if (!std::isfinite(r)) r = 0.f;
	block_runtime_.PushInputSample({l, r});
	// Mono detection: no IN_R cable means a mono source, so the recording
	// buffer can drop to 1 channel and double its effective duration for
	// the same byte pool (see RetoursParameters::mono_input).
	params_.mono_input = !in_r_connected_;

	// Process when block is full
	if (block_runtime_.BlockReady()) {
		// Refresh the connectivity cache once per block (M7) -- see
		// out_r_connected_'s comment.
		out_r_connected_ = outputs[Retours::OUT_R_OUTPUT].isConnected();
		in_r_connected_  = inputs[Retours::IN_R_INPUT].isConnected();
		if (m_.clear_requested_.exchange(false)) {
			processor_.ClearBuffer();
			ResetWetFilter();
		}
		if (m_.clear_tempo_requested_.exchange(false))
			processor_.ClearTappedTempo();
#ifdef METAMODULE
		// MetaModule: Quality is a 4-position switch, so the mode IS the
		// param. Consumed only by updateSlowParams()/params_.quality below
		// and the LED check further down, so read once per block.
		m_.quality_state_ = std::clamp((int)(params[Retours::QUALITY_PARAM].getValue() + 0.5f), 0, 3);
#endif
		updateSlowParams(frozen);
		updateFilterAndReverb();

		processor_.SetParameters(params_);
		processor_.Process(block_runtime_.InputBuffer(),
						   scratch_output_buf_,
						   kWrapperBlockSize);
		block_runtime_.CommitProcessedBlock(scratch_output_buf_, kWrapperBlockSize);

		// CLOCK_LIGHT blink. When clocked, it is a true clock indicator:
		// one flash per measured beat, re-anchored to every incoming tick
		// (like the Shape envelope's resync) so it visibly locks to the
		// clock instead of free-running at the subdivided base Interval —
		// which, at fast tempos on a subdivided knob, strobed too fast to
		// read. When free-running there is no external beat, so it falls
		// back to blinking at the base Interval (the effective repeat rate).
		float beat_seconds = processor_.ClockBeatSeconds();  // 0 unless clocked
		float period_seconds = (beat_seconds > 0.f) ? beat_seconds
		                                             : processor_.BaseTimeSeconds();
		float period_samples = std::max(period_seconds * args.sampleRate, 1.f);
		// Re-anchor to the clock edge: a tick landed in this block if the
		// core consumed a tick offset for it.
		if (params_.clock_tick_offset >= 0)
			clock_light_phase_ = 0.f;
		clock_light_phase_ += static_cast<float>(kWrapperBlockSize) / period_samples;
		if (clock_light_phase_ >= 1.f)
			clock_light_phase_ -= std::floor(clock_light_phase_);

		// Snap to full brightness during the flash window at the start of
		// each beat, then decay smoothly (see kClockFlashDecaySeconds).
		// Decay and publish now run once per block instead of every
		// sample: kClockFlashDecayRate is exact, so the closed-form
		// multiply reproduces the per-sample max(0, x - sampleTime/tau)
		// result applied kWrapperBlockSize times (the flash/decay branch
		// is already fixed for the whole block, since clock_light_phase_
		// only changes here).
		const bool flash = clock_light_phase_ < 0.1f;
		if (flash)
			clock_light_level_ = 1.f;
		else
			clock_light_level_ = std::max(0.f,
				clock_light_level_ - kWrapperBlockSize * args.sampleTime * kClockFlashDecayRate);
		lights[Retours::CLOCK_LIGHT].setBrightness(clock_light_level_);
		// GATE: one pulse as each flash starts (a beat wrap, or a clock
		// tick re-anchoring the phase), not for every block of the window.
		if (flash && !prev_flash_)
			gate_samples_left_ = std::max(1, (int)std::lround(kGatePulseSeconds * args.sampleRate));
		prev_flash_ = flash;
	}

	// Light updates
	lights[Retours::SLICE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);

	if (m_.quality_state_ != light_quality_state_) {
		light_quality_state_ = m_.quality_state_;
		lights[Retours::QUALITY_R_LIGHT].setBrightness(kQualityColors[m_.quality_state_][0]);
		lights[Retours::QUALITY_G_LIGHT].setBrightness(kQualityColors[m_.quality_state_][1]);
		lights[Retours::QUALITY_B_LIGHT].setBrightness(kQualityColors[m_.quality_state_][2]);
	}
}

std::unique_ptr<RetoursEngine> MakeRetoursClassic(Retours& m) {
	g_retours_classic_builds.fetch_add(1, std::memory_order_relaxed);
	return std::unique_ptr<RetoursEngine>(new (std::nothrow) RetoursClassic(m, m.sample_rate_));
}
