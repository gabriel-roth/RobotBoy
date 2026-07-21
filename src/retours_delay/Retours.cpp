#include "plugin.hpp"
#include "retours_delay_dsp/retours_dsp.h"
#include "retours_block_runtime.h"
#include "metamodule_fpu.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#ifdef METAMODULE
#ifndef __APPLE__
#include <malloc.h>
#endif
#endif

// Same rationale as Particules: one block size for both hosts (identical CV
// conditioning cadence, amortized SetParameters cost on VCV). 64 samples of
// I/O latency (1.3 ms @ 48 kHz).
static constexpr size_t kWrapperBlockSize = 64;
static_assert(kWrapperBlockSize <= retours_delay_dsp::kMaxBlockSize,
	"Retours wrapper block size must not exceed retours_delay_dsp::kMaxBlockSize");


struct Retours;

struct RetoursQualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};

struct Retours : Module {
	enum ParamId {
		SLICE_PARAM,
		INTERVAL_PARAM,
		TIME_PARAM,
		PITCH_PARAM,
		SHAPE_PARAM,
		FEEDBACK_PARAM,
		DRY_WET_PARAM,
		TIME_AR_PARAM,
		PITCH_AR_PARAM,
		SHAPE_AR_PARAM,
		QUALITY_PARAM,
		CLOCK_PARAM,
		FEEDBACK_AR_PARAM,
		DRY_WET_AR_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		INTERVAL_CV_INPUT,
		TIME_CV_INPUT,
		PITCH_CV_INPUT,
		SHAPE_CV_INPUT,
		FEEDBACK_CV_INPUT,
		DRY_WET_CV_INPUT,
		CLOCK_INPUT,
		SLICE_INPUT,
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
		SLICE_BUTTON_LIGHT,
		CLOCK_LIGHT,
		LIGHTS_LEN
	};

	// DSP
	retours_delay_dsp::RetoursProcessor processor_;
	void* dsp_memory_ = nullptr;

	// Buffer pattern
	RetoursBlockRuntime<kWrapperBlockSize> block_runtime_;
	retours_delay_dsp::StereoFrame scratch_output_buf_[kWrapperBlockSize] = {};

	// Cached params struct (populated by updateSlowParams, consumed by Process)
	retours_delay_dsp::RetoursParameters params_;

	// Module state
	int quality_state_ = 0;   // 0-3, same encoding/colors as Particules
	bool prev_quality_button_ = false;
	dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on SLICE gate
	dsp::SchmittTrigger clock_gate_;     // same for CLOCK jack
	bool prev_clock_button_ = false;     // CLOCK momentary button edge detect
	retours_delay_dsp::TimeChangeMode time_change_mode_ = retours_delay_dsp::TimeChangeMode::kTape;
	bool envelope_pre_feedback_ = false;
	float input_trim_db_ = 0.f;
	float slew_seconds_ = retours_delay_dsp::kSlewSecondsDefault;
	float random_lfo_hz_ = retours_delay_dsp::kRandomLfoHz;
	bool metamodule_fpu_configured_ = false;
	// Menu "Clear buffer" is a UI-thread click; defer the actual ClearBuffer()
	// call to the audio thread (process()) so it's not racing the DSP.
	std::atomic<bool> clear_requested_{false};

	// Pitch knob cache: pitchKnobToSemitones() is a linear search; skip it when
	// the knob hasn't moved (knobs are human-speed, not audio-rate).
	float cached_pitch_knob_      = -999.f;
	float cached_pitch_semitones_ = 0.f;
	// Last quality state written to LEDs — skip redundant setBrightness calls.
	int   light_quality_state_    = -1;
	// CLOCK_LIGHT blink phase, advanced once per block by
	// blockFrames / (BaseTimeSeconds() * sampleRate); wraps at 1.0.
	float clock_light_phase_       = 0.f;

	Retours() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// configSwitch does NOT disable randomization (only configButton does),
		// so SLICE needs the explicit flag or Ctrl+R randomly latches freeze.
		configSwitch(SLICE_PARAM, 0.f, 1.f, 0.f, "Slice", {"Off", "On"})
			->randomizeEnabled = false;
#ifdef METAMODULE
		// MetaModule: a real 4-position switch so the firmware shows the mode
		// name (the adapter can't label a momentary button). Desktop keeps the
		// momentary cycle button below.
		configSwitch(QUALITY_PARAM, 0.f, 3.f, 0.f, "Quality",
			{"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"});
#else
		configButton<RetoursQualityParamQuantity>(QUALITY_PARAM, "Quality");
#endif
		configParam(INTERVAL_PARAM, 0.f, 1.f, 0.35f, "Interval");
		configParam(TIME_PARAM, 0.f, 1.f, 0.f, "Time");
		configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "Shape");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback");
		configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/wet");
		configParam(TIME_AR_PARAM, -1.f, 1.f, 0.f, "Time CV amount");
		configParam(PITCH_AR_PARAM, -1.f, 1.f, 0.f, "Pitch CV amount");
		configParam(SHAPE_AR_PARAM, -1.f, 1.f, 0.f, "Shape CV amount");
		configButton(CLOCK_PARAM, "Tap tempo");
		configParam(FEEDBACK_AR_PARAM, -1.f, 1.f, 0.f, "Feedback CV amount");
		configParam(DRY_WET_AR_PARAM, -1.f, 1.f, 0.f, "Dry/wet CV amount");

		configInput(IN_L_INPUT, "Audio in L");
		configInput(IN_R_INPUT, "Audio in R");
		configInput(INTERVAL_CV_INPUT, "Interval CV");
		configInput(TIME_CV_INPUT, "Time CV");
		configInput(PITCH_CV_INPUT, "Pitch CV");
		configInput(SHAPE_CV_INPUT, "Shape CV");
		configInput(FEEDBACK_CV_INPUT, "Feedback CV");
		configInput(DRY_WET_CV_INPUT, "Dry/wet CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(SLICE_INPUT, "Slice gate");
		configOutput(OUT_L_OUTPUT, "Audio out L");
		configOutput(OUT_R_OUTPUT, "Audio out R");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// SLICE and QUALITY lights are embedded in their button params (tooltips
		// come from configParam); CLOCK_LIGHT is a standalone delay-tick indicator.
		configLight(CLOCK_LIGHT, "Clock");

		// DSP init — also called on sample rate change via onSampleRateChange()
		float sampleRate = APP->engine->getSampleRate();
		auto req = retours_delay_dsp::RetoursProcessor::GetMemoryRequirements(sampleRate);
#if defined(METAMODULE) && !defined(SIMULATOR) && !defined(__APPLE__)
		dsp_memory_ = memalign(req.alignment, req.total_bytes);
#elif defined(_WIN32)
		dsp_memory_ = _aligned_malloc(req.total_bytes, req.alignment);
#else
		if (posix_memalign(&dsp_memory_, req.alignment, req.total_bytes) != 0)
			dsp_memory_ = nullptr;
#endif
		if (!dsp_memory_) {
			// processor_.Init() below already no-ops safely on a null pointer,
			// but a failed allocation of this size is worth surfacing instead
			// of silently running with a dead DSP chain. WARN is defined
			// identically by both hosts' logger.hpp (Rack SDK and MetaModule
			// plugin SDK).
			WARN("Retours: dsp_memory_ allocation failed (%zu bytes)", req.total_bytes);
		}
		processor_.Init(dsp_memory_, req.total_bytes, sampleRate);
	}

	~Retours() {
#ifdef _WIN32
		_aligned_free(dsp_memory_);
#else
		std::free(dsp_memory_);
#endif
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		// Memory requirements are sample-rate-independent (fixed frame budget),
		// so we can reinitialize into the same allocation.
		block_runtime_ = RetoursBlockRuntime<kWrapperBlockSize>{};
		std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
		clock_light_phase_ = 0.f;
		// Re-arm so a possibly-new audio thread gets FTZ configured.
		metamodule_fpu_configured_ = false;
		if (dsp_memory_) {
			auto req = retours_delay_dsp::RetoursProcessor::GetMemoryRequirements(e.sampleRate);
			processor_.Init(dsp_memory_, req.total_bytes, e.sampleRate);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		quality_state_          = 0;
		time_change_mode_       = retours_delay_dsp::TimeChangeMode::kTape;
		envelope_pre_feedback_  = false;
		input_trim_db_          = 0.f;
		slew_seconds_           = retours_delay_dsp::kSlewSecondsDefault;
		random_lfo_hz_          = retours_delay_dsp::kRandomLfoHz;
		block_runtime_          = RetoursBlockRuntime<kWrapperBlockSize>{};
		std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
		clock_light_phase_       = 0.f;
		clear_requested_.store(false);   // reset clears now; drop any queued menu clear
		processor_.ClearBuffer();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "qualityState", json_integer(quality_state_));
		json_object_set_new(root, "timeChangeMode", json_integer(static_cast<int>(time_change_mode_)));
		json_object_set_new(root, "envelopePreFeedback", json_boolean(envelope_pre_feedback_));
		json_object_set_new(root, "inputTrimDb", json_real(input_trim_db_));
		json_object_set_new(root, "slewSeconds", json_real(slew_seconds_));
		json_object_set_new(root, "randomLfoHz", json_real(random_lfo_hz_));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "qualityState")))
			quality_state_ = clamp((int)json_integer_value(j), 0, 3);
		if ((j = json_object_get(root, "timeChangeMode")))
			time_change_mode_ = static_cast<retours_delay_dsp::TimeChangeMode>(
				clamp((int)json_integer_value(j), 0, 1));
		else
			time_change_mode_ = retours_delay_dsp::TimeChangeMode::kTape;
		if ((j = json_object_get(root, "envelopePreFeedback")))
			envelope_pre_feedback_ = json_boolean_value(j);
		else
			envelope_pre_feedback_ = false;
		if ((j = json_object_get(root, "inputTrimDb")))
			input_trim_db_ = clamp((float)json_real_value(j), -12.f, 12.f);
		else
			input_trim_db_ = 0.f;
		if ((j = json_object_get(root, "slewSeconds")))
			slew_seconds_ = clamp((float)json_real_value(j), 0.01f, 1.f);
		else
			slew_seconds_ = retours_delay_dsp::kSlewSecondsDefault;
		if ((j = json_object_get(root, "randomLfoHz")))
			random_lfo_hz_ = clamp((float)json_real_value(j), 0.02f, 2.f);
		else
			random_lfo_hz_ = retours_delay_dsp::kRandomLfoHz;
	}

	void updateSlowParams(bool frozen) {
		params_.density = params[INTERVAL_PARAM].getValue();
		params_.time     = params[TIME_PARAM].getValue();
		{
			float raw = params[PITCH_PARAM].getValue();
			if (raw != cached_pitch_knob_) {
				cached_pitch_knob_      = raw;
				cached_pitch_semitones_ = pitchKnobToSemitones(raw);
			}
			params_.pitch_semitones = cached_pitch_semitones_;
		}
		params_.shape    = params[SHAPE_PARAM].getValue();
		params_.feedback = params[FEEDBACK_PARAM].getValue();
		params_.dry_wet  = params[DRY_WET_PARAM].getValue();

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
		params_.density_cv = sanitizeVoltage(inputs[INTERVAL_CV_INPUT].getVoltage());
		params_.time_cv           = sanitizeVoltage(inputs[TIME_CV_INPUT].getVoltage());
		params_.time_cv_connected = inputs[TIME_CV_INPUT].isConnected();
		params_.pitch_cv           = sanitizeVoltage(inputs[PITCH_CV_INPUT].getVoltage());
		params_.pitch_cv_connected = inputs[PITCH_CV_INPUT].isConnected();
		params_.shape_cv           = sanitizeVoltage(inputs[SHAPE_CV_INPUT].getVoltage());
		params_.shape_cv_connected = inputs[SHAPE_CV_INPUT].isConnected();
		// Feedback/Dry-wet CV pass through their panel attenuverters (bipolar
		// scale here; the DSP then applies its documented /5 V add).
		params_.feedback_cv = sanitizeVoltage(inputs[FEEDBACK_CV_INPUT].getVoltage())
			* params[FEEDBACK_AR_PARAM].getValue();
		params_.dry_wet_cv  = sanitizeVoltage(inputs[DRY_WET_CV_INPUT].getVoltage())
			* params[DRY_WET_AR_PARAM].getValue();

		params_.time_ar  = params[TIME_AR_PARAM].getValue();
		params_.pitch_ar = params[PITCH_AR_PARAM].getValue();
		params_.shape_ar = params[SHAPE_AR_PARAM].getValue();

		params_.clock_tick_offset = block_runtime_.TakeClockTickOffset();
		params_.clock_connected   = inputs[CLOCK_INPUT].isConnected();
		params_.freeze = frozen;

		params_.quality = static_cast<retours_delay_dsp::QualityMode>(quality_state_);
		params_.time_change_mode      = time_change_mode_;
		params_.envelope_pre_feedback = envelope_pre_feedback_;
		params_.input_trim_db         = input_trim_db_;
		params_.slew_seconds          = slew_seconds_;
		params_.random_lfo_hz         = random_lfo_hz_;
	}

	void process(const ProcessArgs& args) override {
		if (!metamodule_fpu_configured_) {
			metamodule_fpu_configured_ = true;
			particules::EnableMetaModuleFlushToZero();
		}
		bool freeze_latch = params[SLICE_PARAM].getValue() > 0.5f;
		freeze_gate_.process(inputs[SLICE_INPUT].getVoltage(), 0.1f, 1.f);
		bool frozen = freeze_latch || freeze_gate_.isHigh();

		// CLOCK: jack rising edge (Schmitt) OR button rising edge both count
		// as ticks; clock_connected reflects only the jack, so button-only
		// taps take the core's tap-tempo path (see RetoursParameters comment).
		bool clock_gate_was_high = clock_gate_.isHigh();
		clock_gate_.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool clock_jack_rising = clock_gate_.isHigh() && !clock_gate_was_high;

		bool clock_button = params[CLOCK_PARAM].getValue() > 0.5f;
		bool clock_button_rising = clock_button && !prev_clock_button_;
		prev_clock_button_ = clock_button;

		block_runtime_.NoteClockEdgeSample(clock_jack_rising || clock_button_rising,
			block_runtime_.InputIndex());

#ifdef METAMODULE
		// MetaModule: Quality is a 4-position switch, so the mode IS the param.
		quality_state_ = std::clamp((int)std::lround(params[QUALITY_PARAM].getValue()), 0, 3);
#else
		// Desktop: momentary button cycles the mode; blocked while frozen.
		bool quality_pressed = params[QUALITY_PARAM].getValue() > 0.5f;
		if (quality_pressed && !prev_quality_button_ && !frozen)
			quality_state_ = (quality_state_ + 1) % 4;
		prev_quality_button_ = quality_pressed;
#endif

		// Output from previously processed block
		retours_delay_dsp::StereoFrame out = block_runtime_.ReadOutputSample();
		bool r_connected = outputs[OUT_R_OUTPUT].isConnected();
		outputs[OUT_L_OUTPUT].setVoltage((r_connected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
		outputs[OUT_R_OUTPUT].setVoltage(out.r * 5.f);

		// Accumulate input
		bool in_r_connected = inputs[IN_R_INPUT].isConnected();
		float l = inputs[IN_L_INPUT].getVoltage() * 0.2f;
		if (!std::isfinite(l)) l = 0.f;
		float r = in_r_connected ? inputs[IN_R_INPUT].getVoltage() * 0.2f : l;
		if (!std::isfinite(r)) r = 0.f;
		block_runtime_.PushInputSample({l, r});
		// Mono detection: no IN_R cable means a mono source, so the recording
		// buffer can drop to 1 channel and double its effective duration for
		// the same byte pool (see RetoursParameters::mono_input).
		params_.mono_input = !in_r_connected;

		// Process when block is full
		if (block_runtime_.BlockReady()) {
			if (clear_requested_.exchange(false))
				processor_.ClearBuffer();
			updateSlowParams(frozen);

			processor_.SetParameters(params_);
			processor_.Process(block_runtime_.InputBuffer(),
							   scratch_output_buf_,
							   kWrapperBlockSize);
			block_runtime_.CommitProcessedBlock(scratch_output_buf_, kWrapperBlockSize);

			// CLOCK_LIGHT blink: phase advances once per block; brightness is
			// high only near the start of each delay period.
			float base_seconds = processor_.BaseTimeSeconds();
			float period_samples = std::max(base_seconds * args.sampleRate, 1.f);
			clock_light_phase_ += static_cast<float>(kWrapperBlockSize) / period_samples;
			if (clock_light_phase_ >= 1.f)
				clock_light_phase_ -= std::floor(clock_light_phase_);
		}

		// Light updates
		lights[SLICE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);

		if (quality_state_ != light_quality_state_) {
			light_quality_state_ = quality_state_;
			lights[QUALITY_R_LIGHT].setBrightness(kQualityColors[quality_state_][0]);
			lights[QUALITY_G_LIGHT].setBrightness(kQualityColors[quality_state_][1]);
			lights[QUALITY_B_LIGHT].setBrightness(kQualityColors[quality_state_][2]);
		}

		lights[CLOCK_LIGHT].setBrightness(clock_light_phase_ < 0.1f ? 1.f : 0.f);
	}
};

std::string RetoursQualityParamQuantity::getDisplayValueString() {
	Retours* m = dynamic_cast<Retours*>(module);
	if (!m) return "";
	static const char* kNames[] = {"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"};
	return kNames[m->quality_state_];
}

// Wrap a context-menu mutation in a whole-module undo snapshot. Because the
// snapshot is the module's full JSON (params + data), one helper covers
// every menu field with no per-item code. VCV-only: MetaModule has no undo
// stack, so there it just runs the mutation.
#ifndef METAMODULE
template <typename F>
static void withMenuUndo(Retours* module, const char* label, F&& mutate) {
	json_t* oldJ = APP->engine->moduleToJson(module);
	mutate();
	history::ModuleChange* h = new history::ModuleChange;
	h->name = label;
	h->moduleId = module->id;
	h->oldModuleJ = oldJ;
	h->newModuleJ = APP->engine->moduleToJson(module);
	APP->history->push(h);
}
#else
template <typename F>
static void withMenuUndo(Retours*, const char*, F&& mutate) {
	mutate();
}
#endif

struct InputTrimQuantity : Quantity {
	Retours* module;
	InputTrimQuantity(Retours* m) : module(m) {}

	void setValue(float value) override {
		if (module) module->input_trim_db_ = clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override { return module ? module->input_trim_db_ : getDefaultValue(); }
	float getMinValue() override { return -12.f; }
	float getMaxValue() override { return 12.f; }
	float getDefaultValue() override { return 0.f; }
	std::string getLabel() override { return "Input trim"; }
	std::string getUnit() override { return " dB"; }
	std::string getDisplayValueString() override {
		return string::f("%.1f", getValue());
	}
};

// Logarithmic menu sliders: the underlying Quantity stores/reads log10(seconds
// or Hz) so ui::Slider's linear drag (which operates on getValue()/getMin/
// MaxValue()) produces a logarithmic sweep of the real unit. Display strings
// still show the real value.
struct SlewQuantity : Quantity {
	Retours* module;
	SlewQuantity(Retours* m) : module(m) {}

	void setValue(float value) override {
		if (!module) return;
		float log_v = clamp(value, getMinValue(), getMaxValue());
		module->slew_seconds_ = std::pow(10.f, log_v);
	}
	float getValue() override {
		return module ? std::log10(module->slew_seconds_) : getDefaultValue();
	}
	float getMinValue() override { return -2.f; }   // log10(0.01)
	float getMaxValue() override { return 0.f; }    // log10(1)
	float getDefaultValue() override { return std::log10(retours_delay_dsp::kSlewSecondsDefault); }
	std::string getLabel() override { return "Doppler slew"; }
	std::string getUnit() override { return " s"; }
	std::string getDisplayValueString() override {
		return string::f("%.3f", module ? module->slew_seconds_ : std::pow(10.f, getDefaultValue()));
	}
};

struct RandomLfoQuantity : Quantity {
	Retours* module;
	RandomLfoQuantity(Retours* m) : module(m) {}

	void setValue(float value) override {
		if (!module) return;
		float log_v = clamp(value, getMinValue(), getMaxValue());
		module->random_lfo_hz_ = std::pow(10.f, log_v);
	}
	float getValue() override {
		return module ? std::log10(module->random_lfo_hz_) : getDefaultValue();
	}
	float getMinValue() override { return std::log10(0.02f); }
	float getMaxValue() override { return std::log10(2.f); }
	float getDefaultValue() override { return std::log10(retours_delay_dsp::kRandomLfoHz); }
	std::string getLabel() override { return "Random LFO rate"; }
	std::string getUnit() override { return " Hz"; }
	std::string getDisplayValueString() override {
		return string::f("%.3f", module ? module->random_lfo_hz_ : std::pow(10.f, getDefaultValue()));
	}
};

#ifndef METAMODULE
struct RetoursMenuSlider : ui::Slider {
	RetoursMenuSlider(Quantity* q) {
		quantity = q;
		box.size.x = 200.0f;
	}
	~RetoursMenuSlider() {
		delete quantity;
	}
};
#endif

#ifdef METAMODULE
// MetaModule-only Quality control. The VCV→MM adapter turns a non-momentary
// SvgSwitch with >=3 frames plus a configSwitch into a labeled FlipSwitch, so
// MM shows the mode name in the Adjust popup and the per-mode colour comes from
// the frame image (metamodule/assets/quality_*.png, reached via the .svg->.png
// asset rule). The desktop build keeps its momentary RGB bezel instead — see
// the #ifndef METAMODULE branch in RetoursWidget and Retours(). The referenced
// .svg files never need to exist: this widget is compiled only for METAMODULE,
// where asset::plugin() rewrites the path to the bundled .png.
struct QualityMmSwitch : SvgSwitch {
	QualityMmSwitch() {
		momentary = false;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_bright.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_cold.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_sunny.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/quality_scorched.svg")));
	}
};
#endif

struct RetoursWidget : ModuleWidget {
	void appendContextMenu(Menu* menu) override {
		Retours* module = dynamic_cast<Retours*>(this->module);
		if (!module) return;

		menu->addChild(new MenuSeparator);

		// --- Quality ---
		menu->addChild(createIndexSubmenuItem("Quality",
			{"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"},
			[=]() { return module->quality_state_; },
			[=](int val) {
				withMenuUndo(module, "change quality",
					[=]() { module->quality_state_ = val; });
			}
		));

		// --- Time change response ---
		menu->addChild(createIndexSubmenuItem("Time change response",
			{"Tape (doppler)", "Crossfade"},
			[=]() { return static_cast<int>(module->time_change_mode_); },
			[=](int val) {
				withMenuUndo(module, "change time-change response", [=]() {
					module->time_change_mode_ = static_cast<retours_delay_dsp::TimeChangeMode>(val);
				});
			}
		));

		// --- Envelope feedback tap ---
		menu->addChild(createIndexSubmenuItem("Envelope feedback tap",
			{"Post-envelope", "Pre-envelope"},
			[=]() { return module->envelope_pre_feedback_ ? 1 : 0; },
			[=](int val) {
				withMenuUndo(module, "change envelope feedback tap", [=]() {
					module->envelope_pre_feedback_ = (val == 1);
				});
			}
		));

#ifndef METAMODULE
		// --- Sliders: desktop only (MM menus can't host slider widgets) ---
		menu->addChild(new MenuSeparator);
		menu->addChild(new RetoursMenuSlider(new InputTrimQuantity(module)));
		menu->addChild(new RetoursMenuSlider(new SlewQuantity(module)));
		menu->addChild(new RetoursMenuSlider(new RandomLfoQuantity(module)));
#endif

		// --- Clear Buffer ---
		// Deferred to the audio thread (process()) to avoid racing the DSP;
		// takes effect at the next block boundary.
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear buffer", "",
			[=]() { module->clear_requested_.store(true); }
		));
	}

	RetoursWidget(Retours* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Retours.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Panel layout: mm centers of the components in res/Retours.svg,
		// carried verbatim into these mm2px(Vec(...)) calls.
		// --- Top row: slice jack/button, quality, tap-tempo button, clock jack ---
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.940f, 15.875f)), module, Retours::SLICE_INPUT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(15.940f, 15.875f)), module, Retours::SLICE_PARAM, Retours::SLICE_BUTTON_LIGHT));
#ifdef METAMODULE
		addParam(createParamCentered<QualityMmSwitch>(mm2px(Vec(27.940f, 15.875f)), module, Retours::QUALITY_PARAM));
#else
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(27.940f, 15.875f)), module, Retours::QUALITY_PARAM, Retours::QUALITY_R_LIGHT));
#endif
		addParam(createParamCentered<VCVButton>(mm2px(Vec(39.940f, 15.875f)), module, Retours::CLOCK_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.940f, 15.875f)), module, Retours::CLOCK_INPUT));

		// --- Primary knobs, CV inputs, attenuverters (Interval has no attenuverter;
		// the clock-tick light sits in that column) ---
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.340f, 42.088f)), module, Retours::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(27.940f, 42.088f)), module, Retours::INTERVAL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(44.540f, 42.088f)), module, Retours::PITCH_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.340f, 53.217f)), module, Retours::TIME_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.940f, 53.217f)), module, Retours::INTERVAL_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(44.540f, 53.217f)), module, Retours::PITCH_CV_INPUT));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.340f, 62.219f)), module, Retours::TIME_AR_PARAM));
		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(27.940f, 62.219f)), module, Retours::CLOCK_LIGHT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.540f, 62.219f)), module, Retours::PITCH_AR_PARAM));

		// --- Secondary knobs, CV inputs, attenuverters ---
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.340f, 82.096f)), module, Retours::SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(27.940f, 82.096f)), module, Retours::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(44.540f, 82.096f)), module, Retours::DRY_WET_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.340f, 93.225f)), module, Retours::SHAPE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.940f, 93.225f)), module, Retours::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(44.540f, 93.225f)), module, Retours::DRY_WET_CV_INPUT));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(11.340f, 102.227f)), module, Retours::SHAPE_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(27.940f, 102.227f)), module, Retours::FEEDBACK_AR_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(44.540f, 102.227f)), module, Retours::DRY_WET_AR_PARAM));

		// --- Audio I/O ---
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.238f, 114.300f)), module, Retours::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(18.938f, 114.300f)), module, Retours::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(36.942f, 114.300f)), module, Retours::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(46.642f, 114.300f)), module, Retours::OUT_R_OUTPUT));
	}
};


Model* modelRetours = createModel<Retours, RetoursWidget>("Retours");
