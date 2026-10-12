#include "ParticulesClassic.hpp"
#include "Particules.hpp"
#include "beads_menu.hpp"
#include "particules_cv_conditioning.h"
#include "particules_density_control.h"
#include "particules_gain_display.h"
#include "particules_scales.h"
#include "metamodule_fpu.h"

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

// The v2.1 Particules wrapper (Particules.cpp at v2.1.0), lifted with as few
// edits as possible so classic renders stay bit-identical to v2.1. The saved
// settings it reads (Quality, gain, Lock pitch, Seed CV mode) are on the
// module, reached through m_.

ParticulesClassic::ParticulesClassic(Particules& m, float sample_rate) : m_(m) {
	// DSP init — also called on sample rate change via onSampleRateChange()
	float sampleRate = sample_rate;
	auto req = particules_dsp::ParticulesProcessor::GetMemoryRequirements(sampleRate);
#if defined(METAMODULE) && !defined(SIMULATOR) && !defined(__APPLE__)
	dsp_memory_ = memalign(req.alignment, req.total_bytes);
#elif defined(_WIN32)
	dsp_memory_ = _aligned_malloc(req.total_bytes, req.alignment);
#else
	if (posix_memalign(&dsp_memory_, req.alignment, req.total_bytes) != 0)
		dsp_memory_ = nullptr;
#endif
	if (!dsp_memory_) {
		// processor_.Init() below already no-ops safely on a null pointer
		// (see ParticulesProcessor::Init), but a failed allocation of this size
		// (~a few hundred KB) is worth surfacing instead of silently
		// running with a dead DSP chain. WARN is defined identically by
		// both hosts' logger.hpp (Rack SDK and MetaModule plugin SDK).
		WARN("Particules: dsp_memory_ allocation failed (%zu bytes)", req.total_bytes);
	}
	processor_.Init(dsp_memory_, req.total_bytes, sampleRate);
	m_.scale_dirty_.store(true, std::memory_order_release);
	block_runtime_.ConfigureSampleRate(sampleRate);
	const int cv_dec = particules::CvDecimationForBlock(kWrapperBlockSize);
	const float cv_smooth =
		particules::CvSmoothingForBlock(particules::kCvSmoothing, kWrapperBlockSize);
	const float pitch_smooth =
		particules::CvSmoothingForBlock(particules::kPitchCvSmoothing, kWrapperBlockSize);
	time_cv_conditioner_.Init(cv_dec, cv_smooth, particules::kMenuCvQuantizeStep, 0.0f);
	size_cv_conditioner_.Init(cv_dec, cv_smooth, particules::kMenuCvQuantizeStep, 0.0f);
	shape_cv_conditioner_.Init(cv_dec, cv_smooth, particules::kMenuCvQuantizeStep, 0.0f);
	pitch_cv_conditioner_.Init(cv_dec, pitch_smooth, particules::kPitchCvQuantizeStep, 0.0f);
}

ParticulesClassic::~ParticulesClassic() {
#ifdef _WIN32
	_aligned_free(dsp_memory_);
#else
	std::free(dsp_memory_);
#endif
}

void ParticulesClassic::onSampleRateChange(float sample_rate) {
	// Memory requirements are sample-rate-independent (fixed frame budget),
	// so we can reinitialize into the same allocation.
	// Reassigning block_runtime_ discards its configuration; it must be
	// followed by ConfigureSampleRate or the LED decay reverts to the
	// flat BlockSize=1 @ 48 kHz default.
	block_runtime_ = ParticulesBlockRuntime<kWrapperBlockSize>{};
	block_runtime_.ConfigureSampleRate(sample_rate);
	std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
	// Re-arm so a possibly-new audio thread gets FTZ configured.
	metamodule_fpu_configured_ = false;
	if (dsp_memory_) {
		auto req = particules_dsp::ParticulesProcessor::GetMemoryRequirements(sample_rate);
		processor_.Init(dsp_memory_, req.total_bytes, sample_rate);
		// Init placement-news the Impl — quantizer state is gone.
		m_.scale_dirty_.store(true, std::memory_order_release);
	}
	ResetControlConditioners();
}

// The engine half of v2.1's onReset; the module has already reset the
// saved settings.
void ParticulesClassic::onReset() {
	// Reassigning block_runtime_ discards its configuration; it must be
	// followed by ConfigureSampleRate or the LED decay reverts to the
	// flat BlockSize=1 @ 48 kHz default.
	block_runtime_       = ParticulesBlockRuntime<kWrapperBlockSize>{};
	block_runtime_.ConfigureSampleRate(m_.sample_rate_);
	std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
	needs_calibration_   = true;
	m_.clear_requested_.store(false);   // reset clears now; drop any queued menu clear
	processor_.ClearBuffer();   // recording buffer only (not feedback path or reverb tail)
	ResetControlConditioners();
}

void ParticulesClassic::ResetControlConditioners() {
	time_cv_conditioner_.Reset(0.0f);
	size_cv_conditioner_.Reset(0.0f);
	shape_cv_conditioner_.Reset(0.0f);
	pitch_cv_conditioner_.Reset(0.0f);
}

// Audio-thread only: (re)push the scale selection into the engine.
void ParticulesClassic::ApplyScaleToProcessor() {
	uint32_t count = 0;
	const int* semis = particules::ScaleSemitones(m_.pitch_scale_, &count);
	if (semis) {
		double ratios[particules::kMaxScaleNotes];
		particules::BuildScaleRatios(semis, count, ratios);
		processor_.LoadScale(ratios, count);
		processor_.SetScaleRoot(60 + m_.pitch_root_);
	} else {
		processor_.ClearScale();
	}
}

void ParticulesClassic::onPortChange(const rack::Module::PortChangeEvent& e) {
	// Trigger auto gain calibration when audio inputs are connected/disconnected
	// This handles both VCV Rack virtual cables and MetaModule physical panel cables
	if (e.type == Port::INPUT && (e.portId == Particules::IN_L_INPUT || e.portId == Particules::IN_R_INPUT)) {
		needs_calibration_ = true;
	}
}

void ParticulesClassic::updateSlowParams(bool frozen) {
	auto& params = m_.params;
	auto& inputs = m_.inputs;

	params_.time  = params[Particules::TIME_PARAM].getValue();
	params_.size  = params[Particules::SIZE_PARAM].getValue();
	params_.shape = params[Particules::SHAPE_PARAM].getValue();
	{
		float raw = params[Particules::PITCH_PARAM].getValue();
		if (raw != cached_pitch_knob_) {
			cached_pitch_knob_      = raw;
			cached_pitch_semitones_ = pitchKnobToSemitones(raw);
		}
		params_.pitch = cached_pitch_semitones_;
	}
	params_.density              = params[Particules::DENSITY_PARAM].getValue();
	params_.density_cv_connected = inputs[Particules::DENSITY_INPUT].isConnected();
	params_.density_cv           = particules::ComputeSlowDensityOffset(
		inputs[Particules::DENSITY_INPUT].getVoltage());

	params_.time_ar  = params[Particules::TIME_AR_PARAM].getValue();
	params_.size_ar  = params[Particules::SIZE_AR_PARAM].getValue();
	params_.shape_ar = params[Particules::SHAPE_AR_PARAM].getValue();
	params_.pitch_ar = params[Particules::PITCH_AR_PARAM].getValue();

	float conditioned_time_cv  = time_cv_conditioner_.Process(inputs[Particules::TIME_INPUT].getVoltage()) * 0.2f;
	float conditioned_size_cv  = size_cv_conditioner_.Process(inputs[Particules::SIZE_INPUT].getVoltage()) * 0.2f;
	float conditioned_shape_cv = shape_cv_conditioner_.Process(inputs[Particules::SHAPE_INPUT].getVoltage()) * 0.2f;
	float conditioned_pitch_cv = pitch_cv_conditioner_.Process(inputs[Particules::PITCH_INPUT].getVoltage()) * 12.f;

	params_.time_cv           = conditioned_time_cv;
	params_.time_cv_connected = inputs[Particules::TIME_INPUT].isConnected();
	params_.size_cv           = conditioned_size_cv;
	params_.size_cv_connected = inputs[Particules::SIZE_INPUT].isConnected();
	params_.shape_cv          = conditioned_shape_cv;
	params_.shape_cv_connected = inputs[Particules::SHAPE_INPUT].isConnected();
	params_.pitch_cv           = conditioned_pitch_cv;
	params_.pitch_cv_connected = inputs[Particules::PITCH_INPUT].isConnected();

	// Scale modes use the engine quantizer; pitch_lock must stay 0 there
	// or the engine would quantize twice (scale first, then pitch_lock).
	params_.pitch_lock =
		(m_.pitch_scale_ <= particules::kPitchOctavesFifths) ? m_.pitch_scale_ : 0;

	params_.feedback = clamp(params[Particules::FEEDBACK_PARAM].getValue() + inputs[Particules::FEEDBACK_INPUT].getVoltage() * 0.2f * params[Particules::FEEDBACK_AMT_PARAM].getValue(), 0.f, 1.f);
	params_.dry_wet  = clamp(params[Particules::DRY_WET_PARAM].getValue()  + inputs[Particules::DRY_WET_INPUT].getVoltage()  * 0.2f * params[Particules::DRY_WET_AMT_PARAM].getValue(),  0.f, 1.f);
	params_.reverb   = clamp(params[Particules::REVERB_PARAM].getValue()   + inputs[Particules::REVERB_INPUT].getVoltage()   * 0.2f * params[Particules::REVERB_AMT_PARAM].getValue(),   0.f, 1.f);

	params_.gate           = block_runtime_.ConsumeSeedGateLatch();
	params_.freeze = frozen;

	// With nothing patched into SEED, both modes free-run on Density
	// (kLatched) — the SEED CV mode only decides how a *patched* input is
	// interpreted (Triggers -> kClocked, Gates -> kGated). Before
	// 2026-07-26, Gates set kGated unconditionally, which silenced the
	// module when SEED was unpatched (gate permanently low).
	const bool seed_patched = inputs[Particules::SEED_INPUT].isConnected();
	if (m_.classic_seed_mode_ == 0) {
		params_.trigger_mode = seed_patched
			? particules_dsp::TriggerMode::kClocked
			: particules_dsp::TriggerMode::kLatched;
	} else {
		params_.trigger_mode = seed_patched
			? particules_dsp::TriggerMode::kGated
			: particules_dsp::TriggerMode::kLatched;
	}
	params_.quality_mode = static_cast<particules_dsp::QualityMode>(m_.quality_state_);

	params_.auto_gain      = m_.auto_gain_;
	params_.manual_gain_db = m_.auto_gain_ ? NAN : m_.manual_gain_db_;
}

void ParticulesClassic::process(const rack::Module::ProcessArgs& args) {
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

	// Latch the SEED gate every sample so short triggers survive the
	// block boundary on MetaModule (updateSlowParams runs once per block).
	// The Seed button is OR'd with the jack: in Triggers mode the engine
	// clocks on its rising edge, in Gates mode it holds the gate.
	seed_gate_.process(inputs[Particules::SEED_INPUT].getVoltage(), 0.1f, 1.f);
	const bool seed_button = params[Particules::SEED_PARAM].getValue() > 0.5f;
	block_runtime_.NoteSeedGateSample(seed_gate_.isHigh() || seed_button);

#ifndef METAMODULE
	// Desktop: momentary button cycles the mode; blocked while frozen.
	bool quality_pressed = params[Particules::QUALITY_PARAM].getValue() > 0.5f;
	if (quality_pressed && !m_.prev_quality_button_ && !frozen)
		m_.quality_state_ = (m_.quality_state_ + 1) % 4;
	m_.prev_quality_button_ = quality_pressed;
#endif

	// Output from previously processed block. Grain trigger pulses go to
	// GATE; Out L/R are always the stereo output.
	particules_dsp::StereoFrame out = block_runtime_.ReadOutputSample();
	outputs[Particules::GATE_OUTPUT].setVoltage(block_runtime_.ConsumeTriggerPulseSample() ? 10.f : 0.f);
	bool r_connected = outputs[Particules::OUT_R_OUTPUT].isConnected();
	outputs[Particules::OUT_L_OUTPUT].setVoltage((r_connected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
	outputs[Particules::OUT_R_OUTPUT].setVoltage(out.r * 5.f);

	// Accumulate input
	bool in_l_connected = inputs[Particules::IN_L_INPUT].isConnected();
	bool in_r_connected = inputs[Particules::IN_R_INPUT].isConnected();
	// R jack unpatched (R←L normalization above) is effectively mono input;
	// the buffer transition machine uses this to size the recording buffer.
	params_.mono_input = !in_r_connected;
	// The gain menu asks for a recalibration (v2.1 called the engine from
	// the menu directly).
	if (m_.recalibrate_requested_.exchange(false))
		needs_calibration_ = true;
	if (m_.auto_gain_ && (needs_calibration_ ||
	                   (in_l_connected != prev_in_l_connected_) ||
	                   (in_r_connected != prev_in_r_connected_))) {
		processor_.TriggerAutoGainCalibration();
		needs_calibration_ = false;
	}
	prev_in_l_connected_ = in_l_connected;
	prev_in_r_connected_ = in_r_connected;

	float l = inputs[Particules::IN_L_INPUT].getVoltage() * 0.2f;
	if (!std::isfinite(l)) l = 0.f;
	float r = in_r_connected ? inputs[Particules::IN_R_INPUT].getVoltage() * 0.2f : l;
	if (!std::isfinite(r)) r = 0.f;
	block_runtime_.PushInputSample({l, r});

	// Process when block is full
	if (block_runtime_.BlockReady()) {
		if (m_.clear_requested_.exchange(false))
			processor_.ClearBuffer();
		if (m_.scale_dirty_.exchange(false, std::memory_order_acquire))
			ApplyScaleToProcessor();
#ifdef METAMODULE
		// MetaModule: Quality is a 4-position switch, so the mode IS the
		// param. Consumed only by updateSlowParams()/params_.quality_mode
		// below and the LED check further down, so read once per block.
		m_.quality_state_ = std::clamp((int)(params[Particules::QUALITY_PARAM].getValue() + 0.5f), 0, 3);
#endif
		updateSlowParams(frozen);

		processor_.SetParameters(params_);
		processor_.Process(block_runtime_.InputBuffer(),
						   scratch_output_buf_,
						   kWrapperBlockSize);
		bool triggered = processor_.GrainTriggeredThisBlock();
		block_runtime_.CommitProcessedBlock(scratch_output_buf_, kWrapperBlockSize);
		block_runtime_.NoteGrainActivity(processor_.ActiveGrainCount(), triggered);
		if (triggered) {
			block_runtime_.StartGrainTriggerPulse(
				static_cast<int>(args.sampleRate * 0.001f));
		}
		// Decay grain LED through the block runtime helper.
		block_runtime_.DecayGrainLed();
		lights[Particules::GRAIN_LIGHT].setBrightness(block_runtime_.GrainLed());
		// For the gain menu, which must not read the engine.
		m_.input_peak_ = processor_.InputLevel();
		m_.gain_db_readout_ = processor_.AutoGainDb();
	}

	// Light updates
	lights[Particules::FREEZE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);
	lights[Particules::SEED_LIGHT].setBrightness(seed_button ? 1.f : 0.f);

	if (m_.quality_state_ != light_quality_state_) {
		light_quality_state_ = m_.quality_state_;
		lights[Particules::QUALITY_R_LIGHT].setBrightness(kQualityColors[m_.quality_state_][0]);
		lights[Particules::QUALITY_G_LIGHT].setBrightness(kQualityColors[m_.quality_state_][1]);
		lights[Particules::QUALITY_B_LIGHT].setBrightness(kQualityColors[m_.quality_state_][2]);
	}
}

// v2.1's gain items and input readout, then Seed CV mode. They read only
// module state: the audio thread publishes the gain and input level there.
void AppendParticulesClassicMenu(Particules* module, rack::ui::Menu* menu) {
	// --- Auto Gain ---
	struct AutoGainItem : MenuItem {
		Particules* module;

		void onAction(const event::Action& e) override {
			if (!module->auto_gain_) {
				withMenuUndo(module, "enable auto gain",
					[this]() { module->auto_gain_ = true; });
			}
			module->recalibrate_requested_.store(true);
		}

		void step() override {
			text = module->auto_gain_ ? "Auto gain" : "Enable auto gain";
			if (module->auto_gain_) {
				rightText = string::f("%.1f dB", module->gain_db_readout_);
			} else {
				rightText = "";
			}
			MenuItem::step();
		}
	};
	{
		auto* item = new AutoGainItem;
		item->text = "Enable auto gain";
		item->module = module;
		menu->addChild(item);
	}

	// --- Manual Gain / Disable Auto Gain ---
	struct ManualGainItem : MenuItem {
		Particules* module;

		void onAction(const event::Action& e) override {
			if (module->auto_gain_) {
				withMenuUndo(module, "disable auto gain",
					[this]() { module->auto_gain_ = false; });
			}
		}

		Menu* createChildMenu() override {
			if (module->auto_gain_) return nullptr;
			Menu* m = new Menu;
#ifdef METAMODULE
			for (int db = 0; db <= 32; db++) {
				m->addChild(createCheckMenuItem(
					string::f("%d dB", db), "",
					[this, db]() { return (int)std::round(module->manual_gain_db_) == db; },
					[this, db]() { module->manual_gain_db_ = (float)db; }
				));
			}
#else
			// beads_menu.hpp's slider is v2.1's ManualGainSlider/Quantity.
			m->addChild(new BeadsManualGainSlider(new BeadsManualGainQuantity<Particules>(module)));
#endif
			return m;
		}

		void step() override {
			if (module->auto_gain_) {
				text = "Disable auto gain";
				rightText = "";
			} else {
				text = "Manual gain";
				rightText = string::f("%.1f dB ▸", module->manual_gain_db_);
			}
			MenuItem::step();
		}
	};
	{
		auto* item = new ManualGainItem;
		item->text = "Manual gain";
		item->module = module;
		menu->addChild(item);
	}

	// --- Input level readout (display-only) ---
	// On VCV, step() refreshes the label live while the menu is open.
	// On MetaModule menus don't animate, so the label is computed once
	// at menu-open time — acceptable.
	struct InputLevelItem : MenuItem {
		Particules* module;
#ifndef METAMODULE
		void step() override {
			rightText = FormatInputLevelDb(module->input_peak_);
			MenuItem::step();
		}
#endif
	};
	{
		auto* item = new InputLevelItem;
		item->module = module;
		item->text = "Input";
		item->rightText = FormatInputLevelDb(module->input_peak_);
		item->disabled = true;
		menu->addChild(item);
	}

	// --- SEED CV Mode ---
	menu->addChild(createIndexSubmenuItem("SEED CV mode",
		{"Triggers", "Gates"},
		[=]() { return module->classic_seed_mode_; },
		[=](int val) {
			withMenuUndo(module, "change SEED CV mode",
				[=]() { module->classic_seed_mode_ = val; });
		}
	));
}

std::unique_ptr<ParticulesEngine> MakeParticulesClassic(Particules& m) {
	g_particules_classic_builds.fetch_add(1, std::memory_order_relaxed);
	return std::unique_ptr<ParticulesEngine>(new (std::nothrow) ParticulesClassic(m, m.sample_rate_));
}
