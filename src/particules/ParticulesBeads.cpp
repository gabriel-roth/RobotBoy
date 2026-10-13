#include "ParticulesBeads.hpp"
#include "Particules.hpp"
#include "beads_menu.hpp"
#include "metamodule_fpu.h"

#include <algorithm>
#include <cmath>
#include <new>

// Seconds for the grain LED to fade after a flash.
static constexpr float kGrainLedDecaySeconds = 0.08f;
// Beads' Seed button (ui.cc): holding it this long re-locks it.
static constexpr float kSeedLongPressSeconds = 4.f;
// Period of the locked Seed light's slow pulse (ui.cc: a 2048 ms triangle).
static constexpr float kSeedPulseSeconds = 2.048f;

std::atomic<int> g_particules_beads_builds{0};
std::atomic<int> g_particules_classic_builds{0};

ParticulesBeads::ParticulesBeads(Particules& m, float sample_rate) : m_(m) {
	InitControlChannels();
	ok_ = host_.Init(beads::Processor::MODE_GRANULAR, true, sample_rate);
	host_.SetGrainFeed(&m.grain_feed_);
}

void ParticulesBeads::InitControlChannels() {
	using robotboy_beads::PotLaw;
	const float lp = robotboy_beads::kPotLp;
	time_pot_.Init(PotLaw::kNormal, lp, robotboy_beads::kTimeHysteresis);
	size_pot_.Init(PotLaw::kNormal, lp, 0.f);
	shape_pot_.Init(PotLaw::kNormal, lp, 0.f);
	density_pot_.Init(PotLaw::kNormal, lp, 0.f);
	pitch_pot_.Init(PotLaw::kDetune, lp, 0.f);
	time_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	size_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	shape_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	pitch_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	robotboy_beads::InitTimeSizeShapeCv(&time_cv_);
	robotboy_beads::InitTimeSizeShapeCv(&size_cv_);
	robotboy_beads::InitTimeSizeShapeCv(&shape_cv_);
	robotboy_beads::InitPitchCv(&pitch_cv_);
	robotboy_beads::InitDensityCv(&density_cv_);
	robotboy_beads::InitMixCv(&feedback_cv_);
	robotboy_beads::InitMixCv(&dry_wet_cv_);
	robotboy_beads::InitMixCv(&reverb_cv_);
}

void ParticulesBeads::onSampleRateChange(float sample_rate) {
	host_.SetHostRate(sample_rate);
	// Re-arm so a possibly-new audio thread gets FTZ configured.
	metamodule_fpu_configured_ = false;
}

void ParticulesBeads::onReset() {
	seed_held_ = false;
}

void AppendParticulesBeadsMenu(Particules* m, rack::ui::Menu* menu) {
	appendBeadsGainMenu(menu, m);
	appendBeadsHissMenu(menu, m);
}

beads::RandomizedParameter ParticulesBeads::Randomized(float base, float attenuverter,
                                                       bool has_cv, float cv) {
	beads::RandomizedParameter r;
	r.base = base;
	r.attenuverter = attenuverter;
	r.has_cv = has_cv;
	r.cv = cv;
	return r;
}

void ParticulesBeads::ReadControls(robotboy_beads::BlockControls* c) {
	if (m_.scale_dirty_.exchange(false, std::memory_order_acquire))
		pitch_lock_.Set(m_.pitch_scale_, m_.pitch_root_);
	if (m_.clear_requested_.exchange(false))
		host_.RequestClear();
	if (m_.recalibrate_requested_.exchange(false))
		host_.RecalibrateGain();

	auto attenuverter = [](float x) { return (x + 1.f) * 0.5f; };
	auto volts = [this](int input) { return m_.inputs[input].getVoltage(); };
	auto& params = m_.params;
	auto& inputs = m_.inputs;

	const bool time_cv = inputs[Particules::TIME_INPUT].isConnected();
	const bool size_cv = inputs[Particules::SIZE_INPUT].isConnected();
	const bool shape_cv = inputs[Particules::SHAPE_INPUT].isConnected();
	const bool pitch_cv = inputs[Particules::PITCH_INPUT].isConnected();

	c->parameters.time = Randomized(
		time_pot_.Process(params[Particules::TIME_PARAM].getValue()),
		time_ar_pot_.Process(attenuverter(params[Particules::TIME_AR_PARAM].getValue())),
		time_cv, time_cv_.Process(volts(Particules::TIME_INPUT) * 0.2f));
	// The panel's Size knob runs -1..1; Beads' runs 0..1.
	c->parameters.size = Randomized(
		size_pot_.Process(attenuverter(params[Particules::SIZE_PARAM].getValue())),
		size_ar_pot_.Process(attenuverter(params[Particules::SIZE_AR_PARAM].getValue())),
		size_cv, size_cv_.Process(volts(Particules::SIZE_INPUT) * 0.2f));
	c->parameters.shape = Randomized(
		shape_pot_.Process(params[Particules::SHAPE_PARAM].getValue()),
		shape_ar_pot_.Process(attenuverter(params[Particules::SHAPE_AR_PARAM].getValue())),
		shape_cv, shape_cv_.Process(volts(Particules::SHAPE_INPUT) * 0.2f));
	c->parameters.pitch = Randomized(
		pitch_pot_.Process(params[Particules::PITCH_PARAM].getValue()),
		pitch_ar_pot_.Process(attenuverter(params[Particules::PITCH_AR_PARAM].getValue())),
		pitch_cv, pitch_cv_.Process(volts(Particules::PITCH_INPUT) * 12.f));
	c->parameters.monophonic_mode = false;
	c->parameters.pitch_lock = pitch_lock_.hook();

	c->density.base = density_pot_.Process(params[Particules::DENSITY_PARAM].getValue());
	// beads.cc zeroes an unpatched Density CV to ignore ADC noise.
	c->density.cv = inputs[Particules::DENSITY_INPUT].isConnected()
		? density_cv_.Process(volts(Particules::DENSITY_INPUT) * 0.2f) : 0.f;

	c->mix.feedback = robotboy_beads::MixParameter(params[Particules::FEEDBACK_PARAM].getValue(),
		feedback_cv_.Process(volts(Particules::FEEDBACK_INPUT) * 0.2f), params[Particules::FEEDBACK_AMT_PARAM].getValue());
	c->mix.dry_wet = robotboy_beads::MixParameter(params[Particules::DRY_WET_PARAM].getValue(),
		dry_wet_cv_.Process(volts(Particules::DRY_WET_INPUT) * 0.2f), params[Particules::DRY_WET_AMT_PARAM].getValue());
	c->mix.reverb_amount = robotboy_beads::MixParameter(params[Particules::REVERB_PARAM].getValue(),
		reverb_cv_.Process(volts(Particules::REVERB_INPUT) * 0.2f), params[Particules::REVERB_AMT_PARAM].getValue());
	c->mix.output_gate = false;

	// Seed button, exactly as beads.cc reports it to the DSP.
	c->seed_patched = inputs[Particules::SEED_INPUT].isConnected();
	c->seed_button = m_.seed_locked_
		? beads::SEED_BUTTON_STATE_LOCKED
		: (seed_held_ ? beads::SEED_BUTTON_STATE_ON : beads::SEED_BUTTON_STATE_OFF);

	c->gain_setting = m_.auto_gain_ ? 0 : BeadsManualGainByte(m_.manual_gain_db_);
}

void ParticulesBeads::process(const rack::Module::ProcessArgs& args) {
	auto& params = m_.params;
	auto& inputs = m_.inputs;
	auto& outputs = m_.outputs;
	auto& lights = m_.lights;

	if (!metamodule_fpu_configured_) {
		metamodule_fpu_configured_ = true;
		particules::EnableMetaModuleFlushToZero();
	}
	bool freeze_button = params[Particules::FREEZE_PARAM].getValue() > 0.5f;
	freeze_gate_.process(inputs[Particules::FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
	bool frozen = freeze_button || freeze_gate_.isHigh();
	frozen_ = frozen;
	seed_gate_.process(inputs[Particules::SEED_INPUT].getVoltage(), 0.1f, 1.f);

	// Seed button (ui.cc): a press unlocks and holds; a 4 s hold locks
	// again (and ends the hold, though the button is still down).
	const bool seed_button = params[Particules::SEED_PARAM].getValue() > 0.5f;
	if (seed_button && !prev_seed_button_) {
		m_.seed_locked_ = false;
		seed_held_ = true;
		seed_hold_seconds_ = 0.f;
	}
	if (seed_button && seed_held_) {
		seed_hold_seconds_ += args.sampleTime;
		if (seed_hold_seconds_ >= kSeedLongPressSeconds) {
			m_.seed_locked_ = true;
			seed_held_ = false;
		}
	}
	if (!seed_button)
		seed_held_ = false;
	prev_seed_button_ = seed_button;

#ifdef METAMODULE
	// MetaModule: Quality is a 4-position switch, so the mode IS the param.
	m_.quality_state_ = std::clamp((int)(params[Particules::QUALITY_PARAM].getValue() + 0.5f), 0, 3);
#else
	// Desktop: momentary button cycles the mode; blocked while frozen.
	bool quality_pressed = params[Particules::QUALITY_PARAM].getValue() > 0.5f;
	if (quality_pressed && !m_.prev_quality_button_ && !frozen)
		m_.quality_state_ = (m_.quality_state_ + 1) % 4;
	m_.prev_quality_button_ = quality_pressed;
#endif
	host_.SetQuality(static_cast<robotboy_beads::Quality>(m_.quality_state_));
	host_.SetHissEnabled(m_.tape_hiss_);

	const bool in_l = inputs[Particules::IN_L_INPUT].isConnected();
	const bool in_r = inputs[Particules::IN_R_INPUT].isConnected();
	host_.SetInputs(in_l, in_r);

	const float l = inputs[Particules::IN_L_INPUT].getVoltage() * 0.2f;
	const float r = inputs[Particules::IN_R_INPUT].getVoltage() * 0.2f;
	if (std::isfinite(l) && std::isfinite(r))
		m_.input_peak_.store(std::max(std::max(std::fabs(l), std::fabs(r)),
		                     m_.input_peak_.load(std::memory_order_relaxed) * (1.f - 2.f * args.sampleTime)),
		                     std::memory_order_relaxed);

	const robotboy_beads::HostFrameOut out =
		host_.Process({l, r, frozen, seed_gate_.isHigh()}, this);

	bool r_connected = outputs[Particules::OUT_R_OUTPUT].isConnected();
	outputs[Particules::OUT_L_OUTPUT].setVoltage((r_connected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
	outputs[Particules::OUT_R_OUTPUT].setVoltage(out.r * 5.f);
	// GATE: Beads' gate stream (what its gate-output mode puts on the
	// right output), high for the first half of each grain's trigger period.
	outputs[Particules::GATE_OUTPUT].setVoltage(out.gate ? 10.f : 0.f);

	// Grain LED: flash on each grain, brighter the more grains sound.
	if (out.gate && !prev_gate_)
		grain_led_ = std::min(1.f, 0.4f + 0.1f * host_.active_grains());
	prev_gate_ = out.gate;
	grain_led_ = std::max(0.f, grain_led_ - args.sampleTime / kGrainLedDecaySeconds);

	if (++light_divider_ >= 32) {
		light_divider_ = 0;
		m_.gain_db_readout_.store(host_.gain_db(), std::memory_order_relaxed);
		lights[Particules::GRAIN_LIGHT].setBrightness(grain_led_);
		// Seed light (ui.cc): a slow pulse while locked; while unlocked,
		// lit when the button or the Seed input is high.
		seed_pulse_phase_ += 32.f * args.sampleTime / kSeedPulseSeconds;
		seed_pulse_phase_ -= std::floor(seed_pulse_phase_);
		const float triangle = 1.f - std::fabs(2.f * seed_pulse_phase_ - 1.f);
		lights[Particules::SEED_LIGHT].setBrightness(m_.seed_locked_
			? 0.25f + 0.75f * triangle
			: ((seed_held_ || seed_gate_.isHigh()) ? 1.f : 0.f));
		lights[Particules::FREEZE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);
		if (m_.quality_state_ != light_quality_state_) {
			light_quality_state_ = m_.quality_state_;
			lights[Particules::QUALITY_R_LIGHT].setBrightness(kQualityColors[m_.quality_state_][0]);
			lights[Particules::QUALITY_G_LIGHT].setBrightness(kQualityColors[m_.quality_state_][1]);
			lights[Particules::QUALITY_B_LIGHT].setBrightness(kQualityColors[m_.quality_state_][2]);
		}
	}
}

std::unique_ptr<ParticulesEngine> MakeParticulesBeads(Particules& m) {
	g_particules_beads_builds.fetch_add(1, std::memory_order_relaxed);
	return std::unique_ptr<ParticulesEngine>(new (std::nothrow) ParticulesBeads(m, m.sample_rate_));
}
