#include "plugin.hpp"
#include "beadsdelay_dsp/echos_dsp.h"
#include "echos_block_runtime.h"
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
static_assert(kWrapperBlockSize <= beadsdelay_dsp::kMaxBlockSize,
	"Echos wrapper block size must not exceed beadsdelay_dsp::kMaxBlockSize");


struct Echos;

struct EchosQualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};

struct Echos : Module {
	enum ParamId {
		FREEZE_PARAM,
		DENSITY_PARAM,
		TIME_PARAM,
		PITCH_PARAM,
		SHAPE_PARAM,
		FEEDBACK_PARAM,
		DRY_WET_PARAM,
		TIME_AR_PARAM,
		PITCH_AR_PARAM,
		SHAPE_AR_PARAM,
		QUALITY_PARAM,
		SEED_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		DENSITY_CV_INPUT,
		TIME_CV_INPUT,
		PITCH_CV_INPUT,
		SHAPE_CV_INPUT,
		FEEDBACK_CV_INPUT,
		DRY_WET_CV_INPUT,
		SEED_INPUT,
		FREEZE_INPUT,
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
		SEED_LIGHT,
		LIGHTS_LEN
	};

	// DSP
	beadsdelay_dsp::EchosProcessor processor_;
	void* dsp_memory_ = nullptr;

	// Buffer pattern
	EchosBlockRuntime<kWrapperBlockSize> block_runtime_;
	beadsdelay_dsp::StereoFrame scratch_output_buf_[kWrapperBlockSize] = {};

	// Cached params struct (populated by updateSlowParams, consumed by Process)
	beadsdelay_dsp::EchosParameters params_;

	// Module state
	int quality_state_ = 0;   // 0-3, same encoding/colors as Particules
	bool prev_quality_button_ = false;
	dsp::SchmittTrigger freeze_gate_;   // 0.1 V / 1 V hysteresis on FREEZE gate
	dsp::SchmittTrigger seed_gate_;     // same for SEED jack
	bool prev_seed_button_ = false;     // SEED momentary button edge detect
	beadsdelay_dsp::TimeChangeMode time_change_mode_ = beadsdelay_dsp::TimeChangeMode::kTape;
	bool envelope_pre_feedback_ = false;
	float input_trim_db_ = 0.f;
	float slew_seconds_ = beadsdelay_dsp::kSlewSecondsDefault;
	float random_lfo_hz_ = beadsdelay_dsp::kRandomLfoHz;
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
	// SEED_LIGHT blink phase, advanced once per block by
	// blockFrames / (BaseTimeSeconds() * sampleRate); wraps at 1.0.
	float seed_light_phase_       = 0.f;

	Echos() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// configSwitch does NOT disable randomization (only configButton does),
		// so FREEZE needs the explicit flag or Ctrl+R randomly latches freeze.
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"})
			->randomizeEnabled = false;
#ifdef METAMODULE
		// MetaModule: a real 4-position switch so the firmware shows the mode
		// name (the adapter can't label a momentary button). Desktop keeps the
		// momentary cycle button below.
		configSwitch(QUALITY_PARAM, 0.f, 3.f, 0.f, "Quality",
			{"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"});
#else
		configButton<EchosQualityParamQuantity>(QUALITY_PARAM, "Quality");
#endif
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.5f, "Density");
		configParam(TIME_PARAM, 0.f, 1.f, 0.f, "Time");
		configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "Shape");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback");
		configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/wet");
		configParam(TIME_AR_PARAM, -1.f, 1.f, 0.f, "Time CV amount");
		configParam(PITCH_AR_PARAM, -1.f, 1.f, 0.f, "Pitch CV amount");
		configParam(SHAPE_AR_PARAM, -1.f, 1.f, 0.f, "Shape CV amount");
		configButton(SEED_PARAM, "Tap tempo");

		configInput(IN_L_INPUT, "Audio in L");
		configInput(IN_R_INPUT, "Audio in R");
		configInput(DENSITY_CV_INPUT, "Density CV");
		configInput(TIME_CV_INPUT, "Time CV");
		configInput(PITCH_CV_INPUT, "Pitch CV");
		configInput(SHAPE_CV_INPUT, "Shape CV");
		configInput(FEEDBACK_CV_INPUT, "Feedback CV");
		configInput(DRY_WET_CV_INPUT, "Dry/wet CV");
		configInput(SEED_INPUT, "Seed/clock");
		configInput(FREEZE_INPUT, "Freeze gate");
		configOutput(OUT_L_OUTPUT, "Audio out L");
		configOutput(OUT_R_OUTPUT, "Audio out R");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// configLight() is intentionally omitted: the lights are embedded in
		// button params (FREEZE, QUALITY), so their tooltips come from configParam.

		// DSP init — also called on sample rate change via onSampleRateChange()
		float sampleRate = APP->engine->getSampleRate();
		auto req = beadsdelay_dsp::EchosProcessor::GetMemoryRequirements(sampleRate);
#if defined(METAMODULE) && !defined(SIMULATOR)
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
			WARN("Echos: dsp_memory_ allocation failed (%zu bytes)", req.total_bytes);
		}
		processor_.Init(dsp_memory_, req.total_bytes, sampleRate);
	}

	~Echos() {
#ifdef _WIN32
		_aligned_free(dsp_memory_);
#else
		std::free(dsp_memory_);
#endif
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		// Memory requirements are sample-rate-independent (fixed frame budget),
		// so we can reinitialize into the same allocation.
		block_runtime_ = EchosBlockRuntime<kWrapperBlockSize>{};
		std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
		seed_light_phase_ = 0.f;
		// Re-arm so a possibly-new audio thread gets FTZ configured.
		metamodule_fpu_configured_ = false;
		if (dsp_memory_) {
			auto req = beadsdelay_dsp::EchosProcessor::GetMemoryRequirements(e.sampleRate);
			processor_.Init(dsp_memory_, req.total_bytes, e.sampleRate);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		quality_state_          = 0;
		time_change_mode_       = beadsdelay_dsp::TimeChangeMode::kTape;
		envelope_pre_feedback_  = false;
		input_trim_db_          = 0.f;
		slew_seconds_           = beadsdelay_dsp::kSlewSecondsDefault;
		random_lfo_hz_          = beadsdelay_dsp::kRandomLfoHz;
		block_runtime_          = EchosBlockRuntime<kWrapperBlockSize>{};
		std::memset(scratch_output_buf_, 0, sizeof(scratch_output_buf_));
		seed_light_phase_       = 0.f;
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
			time_change_mode_ = static_cast<beadsdelay_dsp::TimeChangeMode>(
				clamp((int)json_integer_value(j), 0, 1));
		else
			time_change_mode_ = beadsdelay_dsp::TimeChangeMode::kTape;
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
			slew_seconds_ = beadsdelay_dsp::kSlewSecondsDefault;
		if ((j = json_object_get(root, "randomLfoHz")))
			random_lfo_hz_ = clamp((float)json_real_value(j), 0.02f, 2.f);
		else
			random_lfo_hz_ = beadsdelay_dsp::kRandomLfoHz;
	}

	void updateSlowParams(bool frozen) {
		params_.density = params[DENSITY_PARAM].getValue();
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
		params_.density_cv = inputs[DENSITY_CV_INPUT].getVoltage();
		params_.time_cv           = inputs[TIME_CV_INPUT].getVoltage();
		params_.time_cv_connected = inputs[TIME_CV_INPUT].isConnected();
		params_.pitch_cv           = inputs[PITCH_CV_INPUT].getVoltage();
		params_.pitch_cv_connected = inputs[PITCH_CV_INPUT].isConnected();
		params_.shape_cv           = inputs[SHAPE_CV_INPUT].getVoltage();
		params_.shape_cv_connected = inputs[SHAPE_CV_INPUT].isConnected();
		params_.feedback_cv = inputs[FEEDBACK_CV_INPUT].getVoltage();
		params_.dry_wet_cv  = inputs[DRY_WET_CV_INPUT].getVoltage();

		params_.time_ar  = params[TIME_AR_PARAM].getValue();
		params_.pitch_ar = params[PITCH_AR_PARAM].getValue();
		params_.shape_ar = params[SHAPE_AR_PARAM].getValue();

		params_.clock_tick_offset = block_runtime_.TakeClockTickOffset();
		params_.clock_connected   = inputs[SEED_INPUT].isConnected();
		params_.freeze = frozen;

		params_.quality = static_cast<beadsdelay_dsp::QualityMode>(quality_state_);
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
		bool freeze_latch = params[FREEZE_PARAM].getValue() > 0.5f;
		freeze_gate_.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
		bool frozen = freeze_latch || freeze_gate_.isHigh();

		// SEED: jack rising edge (Schmitt) OR button rising edge both count
		// as ticks; clock_connected reflects only the jack, so button-only
		// taps take the core's tap-tempo path (see EchosParameters comment).
		bool seed_gate_was_high = seed_gate_.isHigh();
		seed_gate_.process(inputs[SEED_INPUT].getVoltage(), 0.1f, 1.f);
		bool seed_jack_rising = seed_gate_.isHigh() && !seed_gate_was_high;

		bool seed_button = params[SEED_PARAM].getValue() > 0.5f;
		bool seed_button_rising = seed_button && !prev_seed_button_;
		prev_seed_button_ = seed_button;

		block_runtime_.NoteClockEdgeSample(seed_jack_rising || seed_button_rising,
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
		beadsdelay_dsp::StereoFrame out = block_runtime_.ReadOutputSample();
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

			// SEED_LIGHT blink: phase advances once per block; brightness is
			// high only near the start of each delay period.
			float base_seconds = processor_.BaseTimeSeconds();
			float period_samples = std::max(base_seconds * args.sampleRate, 1.f);
			seed_light_phase_ += static_cast<float>(kWrapperBlockSize) / period_samples;
			if (seed_light_phase_ >= 1.f)
				seed_light_phase_ -= std::floor(seed_light_phase_);
		}

		// Light updates
		lights[FREEZE_BUTTON_LIGHT].setBrightness(frozen ? 1.f : 0.f);

		if (quality_state_ != light_quality_state_) {
			light_quality_state_ = quality_state_;
			lights[QUALITY_R_LIGHT].setBrightness(kQualityColors[quality_state_][0]);
			lights[QUALITY_G_LIGHT].setBrightness(kQualityColors[quality_state_][1]);
			lights[QUALITY_B_LIGHT].setBrightness(kQualityColors[quality_state_][2]);
		}

		lights[SEED_LIGHT].setBrightness(seed_light_phase_ < 0.1f ? 1.f : 0.f);
	}
};

std::string EchosQualityParamQuantity::getDisplayValueString() {
	Echos* m = dynamic_cast<Echos*>(module);
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
static void withMenuUndo(Echos* module, const char* label, F&& mutate) {
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
static void withMenuUndo(Echos*, const char*, F&& mutate) {
	mutate();
}
#endif

struct InputTrimQuantity : Quantity {
	Echos* module;
	InputTrimQuantity(Echos* m) : module(m) {}

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
	Echos* module;
	SlewQuantity(Echos* m) : module(m) {}

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
	float getDefaultValue() override { return std::log10(beadsdelay_dsp::kSlewSecondsDefault); }
	std::string getLabel() override { return "Doppler slew"; }
	std::string getUnit() override { return " s"; }
	std::string getDisplayValueString() override {
		return string::f("%.3f", module ? module->slew_seconds_ : std::pow(10.f, getDefaultValue()));
	}
};

struct RandomLfoQuantity : Quantity {
	Echos* module;
	RandomLfoQuantity(Echos* m) : module(m) {}

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
	float getDefaultValue() override { return std::log10(beadsdelay_dsp::kRandomLfoHz); }
	std::string getLabel() override { return "Random LFO rate"; }
	std::string getUnit() override { return " Hz"; }
	std::string getDisplayValueString() override {
		return string::f("%.3f", module ? module->random_lfo_hz_ : std::pow(10.f, getDefaultValue()));
	}
};

#ifndef METAMODULE
struct EchosMenuSlider : ui::Slider {
	EchosMenuSlider(Quantity* q) {
		quantity = q;
		box.size.x = 200.0f;
	}
	~EchosMenuSlider() {
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
// the #ifndef METAMODULE branch in EchosWidget and Echos(). The referenced
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

struct EchosWidget : ModuleWidget {
	void appendContextMenu(Menu* menu) override {
		Echos* module = dynamic_cast<Echos*>(this->module);
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
					module->time_change_mode_ = static_cast<beadsdelay_dsp::TimeChangeMode>(val);
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
		menu->addChild(new EchosMenuSlider(new InputTrimQuantity(module)));
		menu->addChild(new EchosMenuSlider(new SlewQuantity(module)));
		menu->addChild(new EchosMenuSlider(new RandomLfoQuantity(module)));
#endif

		// --- Clear Buffer ---
		// Deferred to the audio thread (process()) to avoid racing the DSP;
		// takes effect at the next block boundary.
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear buffer", "",
			[=]() { module->clear_requested_.store(true); }
		));
	}

	EchosWidget(Echos* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Echos.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Layout generated by vcv-panel-gen from panel-specs/echos.yaml (Task
		// 11); positions below are the tool's emitted mm coordinates, carried
		// over verbatim into these mm2px(Vec(...)) calls.
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(12.160f, 18.757f)), module, Echos::FREEZE_PARAM, Echos::FREEZE_BUTTON_LIGHT));
#ifdef METAMODULE
		addParam(createParamCentered<QualityMmSwitch>(mm2px(Vec(30.480f, 18.757f)), module, Echos::QUALITY_PARAM));
#else
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(30.480f, 18.757f)), module, Echos::QUALITY_PARAM, Echos::QUALITY_R_LIGHT));
#endif
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(48.800f, 18.757f)), module, Echos::SEED_PARAM, Echos::SEED_LIGHT));

		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(12.160f, 38.514f)), module, Echos::DENSITY_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(30.480f, 38.514f)), module, Echos::TIME_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(48.800f, 38.514f)), module, Echos::PITCH_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.160f, 57.771f)), module, Echos::SHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.480f, 57.771f)), module, Echos::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(48.800f, 57.771f)), module, Echos::DRY_WET_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.870f, 79.029f)), module, Echos::DENSITY_CV_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(23.610f, 79.029f)), module, Echos::TIME_AR_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.610f, 90.379f)), module, Echos::TIME_CV_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.350f, 79.029f)), module, Echos::PITCH_AR_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(37.350f, 90.379f)), module, Echos::PITCH_CV_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(51.090f, 79.029f)), module, Echos::SHAPE_AR_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(51.090f, 90.379f)), module, Echos::SHAPE_CV_INPUT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.350f, 99.336f)), module, Echos::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.374f, 99.336f)), module, Echos::DRY_WET_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(37.397f, 99.336f)), module, Echos::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(52.813f, 99.336f)), module, Echos::FREEZE_INPUT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.350f, 114.593f)), module, Echos::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.050f, 114.593f)), module, Echos::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(43.910f, 114.593f)), module, Echos::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(53.610f, 114.593f)), module, Echos::OUT_R_OUTPUT));
	}
};


Model* modelEchos = createModel<Echos, EchosWidget>("Echos");
