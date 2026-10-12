#include "RetoursBeads.hpp"
#include "Retours.hpp"
#include "beads_menu.hpp"
#include "metamodule_fpu.h"

#include <algorithm>
#include <cmath>
#include <new>

// CLOCK_LIGHT flash decay time. The beat flash snaps to full brightness, then
// decays over this long so each pulse spans many video frames and reads evenly.
static constexpr float kClockFlashDecaySeconds = 0.08f;

std::atomic<int> g_retours_beads_builds{0};
std::atomic<int> g_retours_classic_builds{0};

RetoursBeads::RetoursBeads(Retours& m, float sample_rate) : m_(m) {
	using robotboy_beads::PotLaw;
	const float lp = robotboy_beads::kPotLp;
	time_pot_.Init(PotLaw::kNormal, lp, robotboy_beads::kTimeHysteresis);
	interval_pot_.Init(PotLaw::kNormal, lp, 0.f);
	shape_pot_.Init(PotLaw::kNormal, lp, 0.f);
	pitch_pot_.Init(PotLaw::kDetune, lp, 0.f);
	time_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	pitch_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	shape_ar_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	robotboy_beads::InitTimeSizeShapeCv(&time_cv_);
	robotboy_beads::InitTimeSizeShapeCv(&shape_cv_);
	robotboy_beads::InitPitchCv(&pitch_cv_);
	robotboy_beads::InitDensityCv(&interval_cv_);
	robotboy_beads::InitMixCv(&feedback_cv_);
	robotboy_beads::InitMixCv(&dry_wet_cv_);
	robotboy_beads::InitMixCv(&reverb_cv_);
	// The filter is Beads' Size attenurandomizer in delay mode: same knob
	// law (dead band at noon) and Size's CV conditioning.
	filter_pot_.Init(PotLaw::kAttenuverter, lp, 0.f);
	robotboy_beads::InitTimeSizeShapeCv(&filter_cv_);

	// Delay mode only, with Beads' reverb.
	ok_ = host_.Init(beads::Processor::MODE_DELAY, true, sample_rate);
}

void RetoursBeads::onSampleRateChange(float sample_rate) {
	host_.SetHostRate(sample_rate);
	metamodule_fpu_configured_ = false;
}

void AppendRetoursBeadsMenu(Retours* m, rack::ui::Menu* menu) {
	appendBeadsGainMenu(menu, m);
	appendBeadsHissMenu(menu, m);
}

beads::RandomizedParameter RetoursBeads::Randomized(float base, float attenuverter,
                                                    bool has_cv, float cv) {
	beads::RandomizedParameter r;
	r.base = base;
	r.attenuverter = attenuverter;
	r.has_cv = has_cv;
	r.cv = cv;
	return r;
}

void RetoursBeads::ReadControls(robotboy_beads::BlockControls* c) {
	if (m_.clear_requested_.exchange(false))
		host_.RequestClear();
	if (m_.recalibrate_requested_.exchange(false))
		host_.RecalibrateGain();
	if (m_.clear_tempo_requested_.exchange(false))
		tempo_held_ = false;

	auto attenuverter = [](float x) { return (x + 1.f) * 0.5f; };
	auto volts = [this](int input) { return m_.inputs[input].getVoltage(); };
	auto& params = m_.params;
	auto& inputs = m_.inputs;

	const bool time_cv = inputs[Retours::TIME_CV_INPUT].isConnected();
	const bool pitch_cv = inputs[Retours::PITCH_CV_INPUT].isConnected();
	const bool shape_cv = inputs[Retours::SHAPE_CV_INPUT].isConnected();

	c->parameters.time = Randomized(
		time_pot_.Process(params[Retours::TIME_PARAM].getValue()),
		time_ar_pot_.Process(attenuverter(params[Retours::TIME_AR_PARAM].getValue())),
		time_cv, time_cv_.Process(volts(Retours::TIME_CV_INPUT) * 0.2f));
	c->parameters.pitch = Randomized(
		pitch_pot_.Process(params[Retours::PITCH_PARAM].getValue()),
		pitch_ar_pot_.Process(attenuverter(params[Retours::PITCH_AR_PARAM].getValue())),
		pitch_cv, pitch_cv_.Process(volts(Retours::PITCH_CV_INPUT) * 12.f));
	c->parameters.shape = Randomized(
		shape_pot_.Process(params[Retours::SHAPE_PARAM].getValue()),
		shape_ar_pot_.Process(attenuverter(params[Retours::SHAPE_AR_PARAM].getValue())),
		shape_cv, shape_cv_.Process(volts(Retours::SHAPE_CV_INPUT) * 0.2f));
	// FILTER is Beads' delay-mode bandwidth filter, which Beads plays from
	// its Size attenurandomizer: bandwidth = 0.5 * (1 + attenuverter) +
	// Size CV (granular_processor.h). Noon is off, low-pass to the left,
	// high-pass to the right. Retours scales the CV with its own trimpot.
	// The Size knob itself only chooses the mode, which is pinned here.
	const bool filter_cv = inputs[Retours::FILTER_CV_INPUT].isConnected();
	c->parameters.size = Randomized(1.f,
		filter_pot_.Process(params[Retours::FILTER_PARAM].getValue()),
		filter_cv,
		filter_cv_.Process(volts(Retours::FILTER_CV_INPUT) * 0.2f) * params[Retours::FILTER_AR_PARAM].getValue());
	c->parameters.monophonic_mode = false;
	c->parameters.pitch_lock = nullptr;

	c->density.base = interval_pot_.Process(params[Retours::INTERVAL_PARAM].getValue());
	c->density.cv = inputs[Retours::INTERVAL_CV_INPUT].isConnected()
		? interval_cv_.Process(volts(Retours::INTERVAL_CV_INPUT) * 0.2f) : 0.f;

	c->mix.feedback = robotboy_beads::MixParameter(params[Retours::FEEDBACK_PARAM].getValue(),
		feedback_cv_.Process(volts(Retours::FEEDBACK_CV_INPUT) * 0.2f), params[Retours::FEEDBACK_AR_PARAM].getValue());
	c->mix.dry_wet = robotboy_beads::MixParameter(params[Retours::DRY_WET_PARAM].getValue(),
		dry_wet_cv_.Process(volts(Retours::DRY_WET_CV_INPUT) * 0.2f), params[Retours::DRY_WET_AR_PARAM].getValue());
	c->mix.reverb_amount = robotboy_beads::MixParameter(params[Retours::REVERB_PARAM].getValue(),
		reverb_cv_.Process(volts(Retours::REVERB_CV_INPUT) * 0.2f), params[Retours::REVERB_AR_PARAM].getValue());
	c->mix.output_gate = false;

	// Clock = Beads' Seed. A tap is the Seed button pressed for one block
	// (the first press from the locked state only unlocks, as on Beads).
	// A patched clock goes to the Seed input; its first tick unlocks too,
	// so the measured tempo survives unpatching.
	const bool clock_patched = inputs[Retours::CLOCK_INPUT].isConnected();
	if (clock_tick_pending_ && clock_patched)
		tempo_held_ = true;
	c->seed_patched = clock_patched;
	// While the engine is muted after a reformat it isn't listening, so a
	// tap waits for it.
	if (tap_pending_ && !host_.muted()) {
		c->seed_button = beads::SEED_BUTTON_STATE_ON;
		tempo_held_ = true;
		tap_pending_ = false;
	} else {
		c->seed_button = tempo_held_
			? beads::SEED_BUTTON_STATE_OFF
			: beads::SEED_BUTTON_STATE_LOCKED;
	}
	clock_tick_pending_ = false;

	c->gain_setting = m_.auto_gain_ ? 0 : BeadsManualGainByte(m_.manual_gain_db_);
}

void RetoursBeads::process(const rack::Module::ProcessArgs& args) {
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
	clock_patched_ = inputs[Retours::CLOCK_INPUT].isConnected();

	clock_gate_.process(inputs[Retours::CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
	if (clock_gate_.isHigh())
		clock_tick_pending_ = true;
	bool clock_button = params[Retours::CLOCK_PARAM].getValue() > 0.5f;
	if (clock_button && !prev_clock_button_)
		tap_pending_ = true;
	prev_clock_button_ = clock_button;

#ifdef METAMODULE
	// MetaModule: Quality is a 4-position switch, so the mode IS the param.
	m_.quality_state_ = std::clamp((int)(params[Retours::QUALITY_PARAM].getValue() + 0.5f), 0, 3);
#else
	// Desktop: momentary button cycles the mode; blocked while sliced.
	bool quality_pressed = params[Retours::QUALITY_PARAM].getValue() > 0.5f;
	if (quality_pressed && !m_.prev_quality_button_ && !frozen)
		m_.quality_state_ = (m_.quality_state_ + 1) % 4;
	m_.prev_quality_button_ = quality_pressed;
#endif
	host_.SetQuality(static_cast<robotboy_beads::Quality>(m_.quality_state_));
	host_.SetHissEnabled(m_.tape_hiss_);
	host_.SetInputs(inputs[Retours::IN_L_INPUT].isConnected(), inputs[Retours::IN_R_INPUT].isConnected());

	const float l = inputs[Retours::IN_L_INPUT].getVoltage() * 0.2f;
	const float r = inputs[Retours::IN_R_INPUT].getVoltage() * 0.2f;
	if (std::isfinite(l) && std::isfinite(r))
		m_.input_peak_.store(std::max(std::max(std::fabs(l), std::fabs(r)),
		                     m_.input_peak_.load(std::memory_order_relaxed) * (1.f - 2.f * args.sampleTime)),
		                     std::memory_order_relaxed);

	const robotboy_beads::HostFrameOut out =
		host_.Process({l, r, frozen, clock_gate_.isHigh()}, this);

	bool r_connected = outputs[Retours::OUT_R_OUTPUT].isConnected();
	outputs[Retours::OUT_L_OUTPUT].setVoltage((r_connected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
	outputs[Retours::OUT_R_OUTPUT].setVoltage(out.r * 5.f);
	// GATE: Beads' gate stream (what its gate-output mode puts on the
	// right output), high for the first half of each repeat.
	outputs[Retours::GATE_OUTPUT].setVoltage(out.gate ? 10.f : 0.f);

	// Clock light: flashes at the start of each repeat (Beads' tap gate).
	if (out.gate && !prev_gate_)
		clock_light_level_ = 1.f;
	prev_gate_ = out.gate;
	clock_light_level_ = std::max(0.f,
		clock_light_level_ - args.sampleTime / kClockFlashDecaySeconds);

	if (++light_divider_ >= 32) {
		light_divider_ = 0;
		m_.gain_db_readout_.store(host_.gain_db(), std::memory_order_relaxed);
		lights[Retours::CLOCK_LIGHT].setBrightness(clock_light_level_);
		lights[Retours::SLICE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);
		if (m_.quality_state_ != light_quality_state_) {
			light_quality_state_ = m_.quality_state_;
			lights[Retours::QUALITY_R_LIGHT].setBrightness(kQualityColors[m_.quality_state_][0]);
			lights[Retours::QUALITY_G_LIGHT].setBrightness(kQualityColors[m_.quality_state_][1]);
			lights[Retours::QUALITY_B_LIGHT].setBrightness(kQualityColors[m_.quality_state_][2]);
		}
	}
}

std::unique_ptr<RetoursEngine> MakeRetoursBeads(Retours& m) {
	g_retours_beads_builds.fetch_add(1, std::memory_order_relaxed);
	return std::unique_ptr<RetoursEngine>(new (std::nothrow) RetoursBeads(m, m.sample_rate_));
}
