#include "plugin.hpp"
#include "beads/beads.h"
#include "../vendor/beads_dsp/src/util/control_conditioner.h"
#include "particules_block_runtime.h"
#include "particules_cv_conditioning.h"
#include "particules_density_control.h"
#include "metamodule_fpu.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#ifdef METAMODULE
#ifndef __APPLE__
#include <malloc.h>
#endif
#endif

// One block size for both hosts: matches MetaModule exactly (identical CV
// conditioning cadence) and amortizes updateSlowParams/SetParameters ~64×
// on VCV. Costs 64 samples of I/O latency (1.3 ms @ 48 kHz).
static constexpr size_t kWrapperBlockSize = 64;
static_assert(kWrapperBlockSize <= beads::kMaxBlockSize,
	"Particules wrapper block size must not exceed beads::kMaxBlockSize");


struct Particules;

struct QualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};

struct Particules : Module {
	enum ParamId {
		FREEZE_PARAM,
		DENSITY_PARAM,
		TIME_PARAM,
		PITCH_PARAM,
		QUALITY_PARAM,
		FEEDBACK_PARAM,
		FEEDBACK_AMT_PARAM,
		SIZE_PARAM,
		SHAPE_PARAM,
		DRY_WET_PARAM,
		DRY_WET_AMT_PARAM,
		REVERB_PARAM,
		REVERB_AMT_PARAM,
		TIME_AR_PARAM,
		SIZE_AR_PARAM,
		SHAPE_AR_PARAM,
		PITCH_AR_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		FEEDBACK_INPUT,
		DRY_WET_INPUT,
		REVERB_INPUT,
		TIME_INPUT,
		SIZE_INPUT,
		SHAPE_INPUT,
		PITCH_INPUT,
		DENSITY_INPUT,
		IN_L_INPUT,
		IN_R_INPUT,
		FREEZE_INPUT,
		SEED_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		QUALITY_R_LIGHT,
		QUALITY_G_LIGHT,
		QUALITY_B_LIGHT,
		FREEZE_BUTTON_LIGHT,
		GRAIN_LIGHT,
		LIGHTS_LEN
	};

	// DSP
	beads::BeadsProcessor processor_;
	void* dsp_memory_ = nullptr;

	// Buffer pattern
	ParticulesBlockRuntime<kWrapperBlockSize> block_runtime_;
	beads::StereoFrame scratch_output_buf_[kWrapperBlockSize] = {};

	// Cached params struct (populated by updateSlowParams, consumed by Process)
	beads::BeadsParameters params_;

	// Module state
	int quality_state_ = 0;   // 0–3
	int seed_state_ = 0;      // 0=Triggers, 1=Gates
	bool prev_quality_button_ = false;
	dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on FREEZE gate
	dsp::SchmittTrigger seed_gate_;     // same for SEED
	int  pitch_lock_ = 0;  // 0=off, 1=octaves, 2=octaves+5ths
	bool grain_trigger_out_ = false;
	bool auto_gain_ = true;
	float manual_gain_db_ = 0.f;
	bool prev_in_l_connected_ = false;
	bool prev_in_r_connected_ = false;
	bool needs_calibration_ = true;  // Calibrate on first process() if auto_gain_
	bool metamodule_fpu_configured_ = false;
	// Menu "Clear buffer" is a UI-thread click; defer the actual ClearBuffer()
	// call to the audio thread (process()) so it's not racing the DSP.
	std::atomic<bool> clear_requested_{false};

	// Pitch knob cache: pitchKnobToSemitones() is a linear search; skip it when
	// the knob hasn't moved (knobs are human-speed, not audio-rate).
	float cached_pitch_knob_      = -999.f;
	float cached_pitch_semitones_ = 0.f;
	beads::ControlConditioner time_cv_conditioner_;
	beads::ControlConditioner size_cv_conditioner_;
	beads::ControlConditioner shape_cv_conditioner_;
	beads::ControlConditioner pitch_cv_conditioner_;
	// Last quality state written to LEDs — skip redundant setBrightness calls.
	int   light_quality_state_    = -1;

	Particules() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// configSwitch does NOT disable randomization (only configButton does),
		// so FREEZE needs the explicit flag or Ctrl+R randomly latches freeze.
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"})
			->randomizeEnabled = false;
		configButton<QualityParamQuantity>(QUALITY_PARAM, "Quality");
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density");
		configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Time");
		configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback");
		configParam(FEEDBACK_AMT_PARAM, 0.f, 1.f, 0.5f, "Feedback CV amount");
		configParam(SIZE_PARAM, -1.f, 1.f, 0.f, "Size");
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.5f, "Shape");
		configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/Wet");
		configParam(DRY_WET_AMT_PARAM, 0.f, 1.f, 0.5f, "Dry/wet CV amount");
		configParam(REVERB_PARAM, 0.f, 1.f, 0.f, "Reverb");
		configParam(REVERB_AMT_PARAM, 0.f, 1.f, 0.5f, "Reverb CV amount");
		configParam(TIME_AR_PARAM, -1.f, 1.f, 0.f, "Time CV amount");
		configParam(SIZE_AR_PARAM, -1.f, 1.f, 0.f, "Size CV amount");
		configParam(SHAPE_AR_PARAM, -1.f, 1.f, 0.f, "Shape CV amount");
		configParam(PITCH_AR_PARAM, -1.f, 1.f, 0.f, "Pitch CV amount");

		configInput(FEEDBACK_INPUT, "Feedback CV");
		configInput(DRY_WET_INPUT, "Dry/wet CV");
		configInput(REVERB_INPUT, "Reverb CV");
		configInput(TIME_INPUT, "Time CV");
		configInput(SIZE_INPUT, "Size CV");
		configInput(SHAPE_INPUT, "Shape CV");
		configInput(PITCH_INPUT, "Pitch CV (V/oct)");
		configInput(DENSITY_INPUT, "Density CV");
		configInput(IN_L_INPUT, "Audio in L");
		configInput(IN_R_INPUT, "Audio in R");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(SEED_INPUT, "Seed/clock");
		configOutput(OUT_L_OUTPUT, "Audio out L");
		configOutput(OUT_R_OUTPUT, "Audio out R");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// configLight() is intentionally omitted: the lights are embedded in
		// button params (FREEZE, QUALITY), so their tooltips come from configParam.
		// Adding separate light labels would show a second overlapping tooltip.

		// DSP init — also called on sample rate change via onSampleRateChange()
		float sampleRate = APP->engine->getSampleRate();
		auto req = beads::BeadsProcessor::GetMemoryRequirements(sampleRate);
#if defined(METAMODULE) && !defined(SIMULATOR)
		dsp_memory_ = memalign(req.alignment, req.total_bytes);
#elif defined(_WIN32)
		dsp_memory_ = _aligned_malloc(req.total_bytes, req.alignment);
#else
		if (posix_memalign(&dsp_memory_, req.alignment, req.total_bytes) != 0)
			dsp_memory_ = nullptr;
#endif
		if (!dsp_memory_) {
			// processor_.Init() below already no-ops safely on a null pointer
			// (see BeadsProcessor::Init), but a failed allocation of this size
			// (~a few hundred KB) is worth surfacing instead of silently
			// running with a dead DSP chain. WARN is defined identically by
			// both hosts' logger.hpp (Rack SDK and MetaModule plugin SDK).
			WARN("Particules: dsp_memory_ allocation failed (%zu bytes)", req.total_bytes);
		}
		processor_.Init(dsp_memory_, req.total_bytes, sampleRate);
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

	~Particules() {
#ifdef _WIN32
		_aligned_free(dsp_memory_);
#else
		std::free(dsp_memory_);
#endif
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		// Memory requirements are sample-rate-independent (fixed frame budget),
		// so we can reinitialize into the same allocation.
		// Reassigning block_runtime_ discards its configuration; it must be
		// followed by ConfigureSampleRate or the LED decay reverts to the
		// flat BlockSize=1 @ 48 kHz default.
		block_runtime_ = ParticulesBlockRuntime<kWrapperBlockSize>{};
		block_runtime_.ConfigureSampleRate(e.sampleRate);
		std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
		// Re-arm so a possibly-new audio thread gets FTZ configured.
		metamodule_fpu_configured_ = false;
		if (dsp_memory_) {
			auto req = beads::BeadsProcessor::GetMemoryRequirements(e.sampleRate);
			processor_.Init(dsp_memory_, req.total_bytes, e.sampleRate);
		}
		ResetControlConditioners();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		quality_state_       = 0;
		seed_state_          = 0;
		pitch_lock_          = 0;
		grain_trigger_out_   = false;
		// Reassigning block_runtime_ discards its configuration; it must be
		// followed by ConfigureSampleRate or the LED decay reverts to the
		// flat BlockSize=1 @ 48 kHz default.
		block_runtime_       = ParticulesBlockRuntime<kWrapperBlockSize>{};
		block_runtime_.ConfigureSampleRate(APP->engine->getSampleRate());
		std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
		auto_gain_           = true;
		manual_gain_db_      = 0.f;
		needs_calibration_   = true;
		processor_.ClearBuffer();   // 4 s buffer, feedback path, reverb tail
		ResetControlConditioners();
	}

	void ResetControlConditioners() {
		time_cv_conditioner_.Reset(0.0f);
		size_cv_conditioner_.Reset(0.0f);
		shape_cv_conditioner_.Reset(0.0f);
		pitch_cv_conditioner_.Reset(0.0f);
	}

	void onPortChange(const PortChangeEvent& e) override {
		// Trigger auto gain calibration when audio inputs are connected/disconnected
		// This handles both VCV Rack virtual cables and MetaModule physical panel cables
		if (e.type == Port::INPUT && (e.portId == IN_L_INPUT || e.portId == IN_R_INPUT)) {
			needs_calibration_ = true;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "qualityState", json_integer(quality_state_));
		json_object_set_new(root, "seedState",    json_integer(seed_state_));
		json_object_set_new(root, "autoGain",     json_boolean(auto_gain_));
		json_object_set_new(root, "manualGainDb", json_real(manual_gain_db_));
		json_object_set_new(root, "pitchLock",       json_integer(pitch_lock_));
		json_object_set_new(root, "grainTriggerOut", json_boolean(grain_trigger_out_));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "qualityState")))
			quality_state_ = clamp((int)json_integer_value(j), 0, 3);
		if ((j = json_object_get(root, "seedState")))
			seed_state_ = clamp((int)json_integer_value(j), 0, 1);
		if ((j = json_object_get(root, "autoGain")))
			auto_gain_ = json_boolean_value(j);
		if ((j = json_object_get(root, "manualGainDb")))
			manual_gain_db_ = clamp((float)json_real_value(j), 0.f, 32.f);
		if ((j = json_object_get(root, "pitchLock")))
			pitch_lock_ = clamp((int)json_integer_value(j), 0, 2);
		if ((j = json_object_get(root, "grainTriggerOut")))
			grain_trigger_out_ = json_boolean_value(j);
	}

	void updateSlowParams(bool frozen) {
		params_.time  = params[TIME_PARAM].getValue();
		params_.size  = params[SIZE_PARAM].getValue();
		params_.shape = params[SHAPE_PARAM].getValue();
		{
			float raw = params[PITCH_PARAM].getValue();
			if (raw != cached_pitch_knob_) {
				cached_pitch_knob_      = raw;
				cached_pitch_semitones_ = pitchKnobToSemitones(raw);
			}
			params_.pitch = cached_pitch_semitones_;
		}
		params_.density              = params[DENSITY_PARAM].getValue();
		params_.density_cv_connected = inputs[DENSITY_INPUT].isConnected();
		params_.density_cv           = particules::ComputeSlowDensityOffset(
			inputs[DENSITY_INPUT].getVoltage());

		params_.time_ar  = params[TIME_AR_PARAM].getValue();
		params_.size_ar  = params[SIZE_AR_PARAM].getValue();
		params_.shape_ar = params[SHAPE_AR_PARAM].getValue();
		params_.pitch_ar = params[PITCH_AR_PARAM].getValue();

		float conditioned_time_cv  = time_cv_conditioner_.Process(inputs[TIME_INPUT].getVoltage()) * 0.2f;
		float conditioned_size_cv  = size_cv_conditioner_.Process(inputs[SIZE_INPUT].getVoltage()) * 0.2f;
		float conditioned_shape_cv = shape_cv_conditioner_.Process(inputs[SHAPE_INPUT].getVoltage()) * 0.2f;
		float conditioned_pitch_cv = pitch_cv_conditioner_.Process(inputs[PITCH_INPUT].getVoltage()) * 12.f;

		params_.time_cv           = conditioned_time_cv;
		params_.time_cv_connected = inputs[TIME_INPUT].isConnected();
		params_.size_cv           = conditioned_size_cv;
		params_.size_cv_connected = inputs[SIZE_INPUT].isConnected();
		params_.shape_cv          = conditioned_shape_cv;
		params_.shape_cv_connected = inputs[SHAPE_INPUT].isConnected();
		params_.pitch_cv           = conditioned_pitch_cv;
		params_.pitch_cv_connected = inputs[PITCH_INPUT].isConnected();

		params_.pitch_lock = pitch_lock_;

		params_.feedback = clamp(params[FEEDBACK_PARAM].getValue() + inputs[FEEDBACK_INPUT].getVoltage() * 0.2f * params[FEEDBACK_AMT_PARAM].getValue(), 0.f, 1.f);
		params_.dry_wet  = clamp(params[DRY_WET_PARAM].getValue()  + inputs[DRY_WET_INPUT].getVoltage()  * 0.2f * params[DRY_WET_AMT_PARAM].getValue(),  0.f, 1.f);
		params_.reverb   = clamp(params[REVERB_PARAM].getValue()   + inputs[REVERB_INPUT].getVoltage()   * 0.2f * params[REVERB_AMT_PARAM].getValue(),   0.f, 1.f);

		params_.gate           = block_runtime_.ConsumeSeedGateLatch();
		params_.freeze = frozen;

		if (seed_state_ == 0) {
			params_.trigger_mode = inputs[SEED_INPUT].isConnected()
				? beads::TriggerMode::kClocked
				: beads::TriggerMode::kLatched;
		} else {
			params_.trigger_mode = beads::TriggerMode::kGated;
		}
		params_.quality_mode = static_cast<beads::QualityMode>(quality_state_);

		params_.auto_gain      = auto_gain_;
		params_.manual_gain_db = auto_gain_ ? NAN : manual_gain_db_;
	}

	void process(const ProcessArgs& args) override {
		if (!metamodule_fpu_configured_) {
			metamodule_fpu_configured_ = true;
			particules::EnableMetaModuleFlushToZero();
		}
		bool freeze_button = params[FREEZE_PARAM].getValue() > 0.5f;
		freeze_gate_.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
		bool frozen = freeze_button || freeze_gate_.isHigh();

		// Latch the SEED gate every sample so short triggers survive the
		// block boundary on MetaModule (updateSlowParams runs once per block).
		seed_gate_.process(inputs[SEED_INPUT].getVoltage(), 0.1f, 1.f);
		block_runtime_.NoteSeedGateSample(seed_gate_.isHigh());

		// Button cycling — rising-edge detection every sample; blocked while frozen
		bool quality_pressed = params[QUALITY_PARAM].getValue() > 0.5f;
		if (quality_pressed && !prev_quality_button_ && !frozen)
			quality_state_ = (quality_state_ + 1) % 4;
		prev_quality_button_ = quality_pressed;

		// Output from previously processed block
		beads::StereoFrame out = block_runtime_.ReadOutputSample();
		if (grain_trigger_out_) {
			// R outputs grain trigger pulses; L always outputs mono mix
			if (block_runtime_.ConsumeTriggerPulseSample()) {
				outputs[OUT_R_OUTPUT].setVoltage(10.f);
			} else {
				outputs[OUT_R_OUTPUT].setVoltage(0.f);
			}
			outputs[OUT_L_OUTPUT].setVoltage((out.l + out.r) * 0.5f * 5.f);
		} else {
			bool r_connected = outputs[OUT_R_OUTPUT].isConnected();
			outputs[OUT_L_OUTPUT].setVoltage((r_connected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
			outputs[OUT_R_OUTPUT].setVoltage(out.r * 5.f);
		}

		// Accumulate input
		bool in_l_connected = inputs[IN_L_INPUT].isConnected();
		bool in_r_connected = inputs[IN_R_INPUT].isConnected();
		if (auto_gain_ && (needs_calibration_ ||
		                   (in_l_connected != prev_in_l_connected_) ||
		                   (in_r_connected != prev_in_r_connected_))) {
			processor_.TriggerAutoGainCalibration();
			needs_calibration_ = false;
		}
		prev_in_l_connected_ = in_l_connected;
		prev_in_r_connected_ = in_r_connected;

		float l = inputs[IN_L_INPUT].getVoltage() * 0.2f;
		if (!std::isfinite(l)) l = 0.f;
		float r = in_r_connected ? inputs[IN_R_INPUT].getVoltage() * 0.2f : l;
		if (!std::isfinite(r)) r = 0.f;
		block_runtime_.PushInputSample({l, r});

		// Process when block is full
		if (block_runtime_.BlockReady()) {
			if (clear_requested_.exchange(false))
				processor_.ClearBuffer();
			updateSlowParams(frozen);

			processor_.SetParameters(params_);
			processor_.Process(block_runtime_.InputBuffer(),
							   scratch_output_buf_,
							   kWrapperBlockSize);
			bool triggered = processor_.GrainTriggeredThisBlock();
			block_runtime_.CommitProcessedBlock(scratch_output_buf_, kWrapperBlockSize);
			block_runtime_.NoteGrainActivity(processor_.ActiveGrainCount(), triggered);
			if (triggered && grain_trigger_out_) {
				block_runtime_.StartGrainTriggerPulse(
					static_cast<int>(args.sampleRate * 0.001f));
			}
			// Decay grain LED through the block runtime helper.
			block_runtime_.DecayGrainLed();
			lights[GRAIN_LIGHT].setBrightness(block_runtime_.GrainLed());
		}

		// Light updates
		lights[FREEZE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);

		if (quality_state_ != light_quality_state_) {
			light_quality_state_ = quality_state_;
			lights[QUALITY_R_LIGHT].setBrightness(kQualityColors[quality_state_][0]);
			lights[QUALITY_G_LIGHT].setBrightness(kQualityColors[quality_state_][1]);
			lights[QUALITY_B_LIGHT].setBrightness(kQualityColors[quality_state_][2]);
		}

	}
};

std::string QualityParamQuantity::getDisplayValueString() {
	Particules* m = dynamic_cast<Particules*>(module);
	if (!m) return "";
	static const char* kNames[] = {"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"};
	return kNames[m->quality_state_];
}

struct ManualGainQuantity : Quantity {
	Particules* module;

	ManualGainQuantity(Particules* m) : module(m) {}

	void setValue(float value) override {
		if (module) {
			module->manual_gain_db_ = clamp(value, getMinValue(), getMaxValue());
		}
	}

	float getValue() override {
		return module ? module->manual_gain_db_ : getDefaultValue();
	}

	float getMinValue() override { return 0.0f; }
	float getMaxValue() override { return 32.0f; }
	float getDefaultValue() override { return 0.0f; }
	std::string getLabel() override { return "Manual gain"; }
	std::string getUnit() override { return " dB"; }
	std::string getDisplayValueString() override {
		return string::f("%.1f", getValue());
	}
};

#ifndef METAMODULE
struct ManualGainSlider : ui::Slider {
	ManualGainSlider(ManualGainQuantity* q) {
		quantity = q;
		box.size.x = 200.0f;
	}
	~ManualGainSlider() {
		delete quantity;
	}
};
#endif

struct ParticulesWidget : ModuleWidget {
	void appendContextMenu(Menu* menu) override {
		Particules* module = dynamic_cast<Particules*>(this->module);
		if (!module) return;

		menu->addChild(new MenuSeparator);

		// --- Auto Gain ---
		struct AutoGainItem : MenuItem {
			Particules* module;

			void onAction(const event::Action& e) override {
				if (!module->auto_gain_) {
					module->auto_gain_ = true;
				}
				module->processor_.TriggerAutoGainCalibration();
			}

			void step() override {
				text = module->auto_gain_ ? "Auto gain" : "Enable auto gain";
				if (module->auto_gain_) {
					rightText = string::f("%.1f dB", module->processor_.AutoGainDb());
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
					module->auto_gain_ = false;
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
				m->addChild(new ManualGainSlider(new ManualGainQuantity(module)));
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

		// --- SEED CV Mode ---
		menu->addChild(createIndexSubmenuItem("SEED CV mode",
			{"Triggers", "Gates"},
			[=]() { return module->seed_state_; },
			[=](int val) { module->seed_state_ = val; }
		));

		// --- Pitch Lock ---
		menu->addChild(createIndexSubmenuItem("Lock pitch",
			{"Off", "Octaves", "Octaves + 5ths"},
			[=]() { return module->pitch_lock_; },
			[=](int val) { module->pitch_lock_ = val; }
		));

		// --- Grain Trigger Output ---
		menu->addChild(createBoolMenuItem("Grain trigger on R output", "",
			[=]() { return module->grain_trigger_out_; },
			[=](bool val) { module->grain_trigger_out_ = val; }
		));

		// --- Clear Buffer ---
		// Deferred to the audio thread (process()) to avoid racing the DSP;
		// takes effect at the next block boundary.
		menu->addChild(createMenuItem("Clear buffer", "",
			[=]() { module->clear_requested_.store(true); }
		));

	}

	ParticulesWidget(Particules* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Particules.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(20.4, 15.875)), module, Particules::FREEZE_PARAM, Particules::FREEZE_BUTTON_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(38.1, 15.875)), module, Particules::QUALITY_PARAM, Particules::QUALITY_R_LIGHT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 42.088)), module, Particules::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 42.088)), module, Particules::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 42.088)), module, Particules::DENSITY_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 62.746)), module, Particules::TIME_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 62.746)), module, Particules::PITCH_AR_PARAM));

		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(29.6, 62.746)), module, Particules::GRAIN_LIGHT));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 42.088)), module, Particules::SIZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.6, 82.096)), module, Particules::SHAPE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 62.746)), module, Particules::SIZE_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(12.6, 101.696)), module, Particules::SHAPE_AR_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(29.6, 82.096)), module, Particules::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(63.6, 82.096)), module, Particules::DRY_WET_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(46.6, 82.096)), module, Particules::REVERB_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(29.6, 101.696)), module, Particules::FEEDBACK_AMT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(63.6, 101.696)), module, Particules::DRY_WET_AMT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.6, 101.696)), module, Particules::REVERB_AMT_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.9, 15.875)), module, Particules::FREEZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(59.845, 15.875)), module, Particules::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 53.746)), module, Particules::TIME_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 53.746)), module, Particules::PITCH_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 53.746)), module, Particules::DENSITY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 53.746)), module, Particules::SIZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.6, 92.696)), module, Particules::SHAPE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.6, 92.696)), module, Particules::FEEDBACK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(63.6, 92.696)), module, Particules::DRY_WET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(46.6, 92.696)), module, Particules::REVERB_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.25, 114.3)), module, Particules::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.95, 114.3)), module, Particules::IN_R_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(51.25, 114.3)), module, Particules::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(60.95, 114.3)), module, Particules::OUT_R_OUTPUT));
	}
};


Model* modelParticules = createModel<Particules, ParticulesWidget>("Particules");
