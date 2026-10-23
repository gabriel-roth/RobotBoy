#include "plugin.hpp"
#include "host/balance_meter.h"
#include "host/brume_engine.h"
#include "host/envelope_follower.h"
#include "host/flush_to_zero.h"

#include <atomic>
#include <cstdlib>

// Brume: a stereo reverb on Mutable Instruments' Beads and Clouds tanks
// (robotboy_beads::BrumeEngine), with send-side filter, smear and wow, and an
// envelope follower that drives every unpatched
// trimpot. The engine is Rack-free; this module reads the panel, applies the
// CV and envelope modulation, and runs the follower.

namespace rb = robotboy_beads;

// Menu and tooltip names. ASCII only (MetaModule font).
static const std::vector<std::string> kModeNames = {"Beads", "Clouds"};
static const std::vector<std::string> kQualityNames = {"Bright", "Cold", "Scorched"};
// Bezel colors, as in the spec's Mode and Quality tables.
static constexpr float kModeColors[2][3] = {{0.f, 1.f, 0.35f}, {0.25f, 0.45f, 1.f}};
static constexpr float kQualityColors[3][3] = {{1.f, 1.f, 1.f}, {0.f, 1.f, 1.f}, {1.f, 0.f, 1.f}};

// Desktop Mode / Quality buttons: the tooltip shows the current setting.
struct SettingQuantity : ParamQuantity {
	const std::vector<std::string>* names = nullptr;
	const int* index = nullptr;
	std::string getDisplayValueString() override {
		return names && index ? (*names)[*index] : "";
	}
};

// RISE / FALL: the follower's time constant, 10 ms .. 5 s. Typed values are
// seconds, or milliseconds with an "ms" suffix.
struct FollowerTimeQuantity : ParamQuantity {
	float getDisplayValue() override { return rb::FollowerTime(getValue()); }
	void setDisplayValue(float seconds) override {
		if (!(seconds > 0.f))
			return;
		setValue(std::log(seconds / 0.01f) / std::log(500.f));
	}
	std::string getDisplayValueString() override {
		const float s = getDisplayValue();
		return s < 1.f ? string::f("%.0f ms", s * 1000.f) : string::f("%.2f s", s);
	}
	void setDisplayValueString(std::string s) override {
		const float v = std::strtof(s.c_str(), nullptr);
		setDisplayValue(s.find("ms") != std::string::npos ? v / 1000.f : v);
	}
};

// DECAY: percent, and "Hold" over the last 5% of travel.
struct DecayQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		return getValue() > 0.95f ? "Hold" : ParamQuantity::getDisplayValueString();
	}
	std::string getUnit() override { return getValue() > 0.95f ? "" : "%"; }
};

// DEPTH and WOW: square laws, shown as a multiple of the original Beads
// setting. Typed values are multiples too.
struct SquareLawQuantity : ParamQuantity {
	float scale = 1.f;
	float getDisplayValue() override {
		const float v = getValue();
		return scale * v * v;
	}
	void setDisplayValue(float x) override {
		if (!std::isfinite(x))
			return;
		setValue(std::sqrt(std::max(x, 0.f) / scale));
	}
};

// SPEED: the LFO rate multiplier, 0.1x .. 16x with 1x at noon. Typed values
// are multiples.
struct SpeedQuantity : ParamQuantity {
	float getDisplayValue() override { return rb::SpeedMultiplier(getValue()); }
	void setDisplayValue(float x) override {
		if (!std::isfinite(x))
			return;
		x = clamp(x, 0.1f, 16.f);
		setValue(x <= 1.f ? std::log10(x / 0.1f) / 2.f : (std::log(x) / std::log(16.f) + 1.f) / 2.f);
	}
};

// SIZE: the delay-length scale, 0.5x .. 2x with 1x at noon. Typed values
// are multiples.
struct SizeQuantity : ParamQuantity {
	float getDisplayValue() override { return rb::SizeScale(getValue()); }
	void setDisplayValue(float x) override {
		if (!std::isfinite(x))
			return;
		x = clamp(x, 0.5f, 2.f);
		setValue((std::log2(x) + 1.f) / 2.f);
	}
};

// PREDELAY: milliseconds, 0 .. 500 on a square law. Typed values are
// milliseconds.
struct PredelayQuantity : ParamQuantity {
	float getDisplayValue() override { return rb::PredelaySeconds(getValue()) * 1000.f; }
	void setDisplayValue(float ms) override {
		if (!std::isfinite(ms))
			return;
		ms = clamp(ms, 0.f, rb::kMaxPredelaySeconds * 1000.f);
		setValue(std::sqrt(ms / (rb::kMaxPredelaySeconds * 1000.f)));
	}
};

struct Brume : Module {
	enum ParamId {
		MODE_PARAM,
		QUALITY_PARAM,
		CLEAR_PARAM,
		RISE_PARAM,
		FALL_PARAM,
		SEND_PARAM,
		FILTER_PARAM,
		SMEAR_PARAM,
		PREDELAY_PARAM,
		WOW_PARAM,
		SIZE_PARAM,
		DECAY_PARAM,
		DAMP_PARAM,
		DIFFUSE_PARAM,
		SPEED_PARAM,
		DEPTH_PARAM,
		WIDTH_PARAM,
		DRY_WET_PARAM,
		SEND_CV_PARAM,
		FILTER_CV_PARAM,
		SMEAR_CV_PARAM,
		PREDELAY_CV_PARAM,
		WOW_CV_PARAM,
		SIZE_CV_PARAM,
		DECAY_CV_PARAM,
		DAMP_CV_PARAM,
		DIFFUSE_CV_PARAM,
		SPEED_CV_PARAM,
		DEPTH_CV_PARAM,
		WIDTH_CV_PARAM,
		DRY_WET_CV_PARAM,
		// Appended, so older patches keep their params.
		SOURCE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		CLEAR_INPUT,
		SIDE_INPUT,
		SEND_INPUT,
		FILTER_INPUT,
		SMEAR_INPUT,
		PREDELAY_INPUT,
		WOW_INPUT,
		SIZE_INPUT,
		DECAY_INPUT,
		DAMP_INPUT,
		DIFFUSE_INPUT,
		SPEED_INPUT,
		DEPTH_INPUT,
		WIDTH_INPUT,
		DRY_WET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		ENV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		MODE_R_LIGHT,
		MODE_G_LIGHT,
		MODE_B_LIGHT,
		QUALITY_R_LIGHT,
		QUALITY_G_LIGHT,
		QUALITY_B_LIGHT,
		ENV_LIGHT,
		BALANCE_R_LIGHT,
		BALANCE_G_LIGHT,
		BALANCE_B_LIGHT,
		LIGHTS_LEN
	};

	// The 13 plate controls, in panel order: knob SEND_PARAM + i,
	// trimpot SEND_CV_PARAM + i, jack SEND_INPUT + i.
	static constexpr int kNumControls = 13;
	static_assert(DRY_WET_PARAM - SEND_PARAM == kNumControls - 1, "plate knobs");
	static_assert(DRY_WET_CV_PARAM - SEND_CV_PARAM == kNumControls - 1, "plate trimpots");
	static_assert(DRY_WET_INPUT - SEND_INPUT == kNumControls - 1, "plate jacks");

	rb::BrumeEngine engine;
	rb::EnvelopeFollower follower;
	rb::BalanceMeter balance;

	// Saved settings. Mode and Quality are the switches' params on
	// MetaModule; these mirror them there.
	int mode = 0;      // rb::BrumeMode
	int quality = 0;   // rb::BrumeQuality
	int wowScope = 1;   // rb::WowScope, Reverb only
	bool followerAutoGain = true;

	float sampleRate = 48000.f;
	// Set by the first process(). Until then a patch's saved Mode and
	// Quality are latched by re-running Init, rather than reaching the
	// engine as a change (with a fade or spillover) at load.
	std::atomic<bool> started{false};
	bool fpuConfigured = false;

	// The follower's source: 0 nothing patched, 1 the input, 2 SIDE, 3 the
	// wet. A change restarts its auto-gain learning.
	int followerSource = 0;
	float appliedRise = -1.f;
	float appliedFall = -1.f;
	float envelope = 0.f;

	dsp::SchmittTrigger clearGate;
	dsp::BooleanTrigger clearButton;
	dsp::BooleanTrigger modeButton;
	dsp::BooleanTrigger qualityButton;
	dsp::ClockDivider lightDivider;

	Brume() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
#ifdef METAMODULE
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", kModeNames)->randomizeEnabled = false;
		configSwitch(QUALITY_PARAM, 0.f, 2.f, 0.f, "Quality", kQualityNames)->randomizeEnabled = false;
#else
		SettingQuantity* q = configButton<SettingQuantity>(MODE_PARAM, "Mode");
		q->names = &kModeNames;
		q->index = &mode;
		q = configButton<SettingQuantity>(QUALITY_PARAM, "Quality");
		q->names = &kQualityNames;
		q->index = &quality;
#endif
		configButton(CLEAR_PARAM, "Clear");
		configParam<FollowerTimeQuantity>(RISE_PARAM, 0.f, 1.f, 0.25f, "Rise");
		configParam<FollowerTimeQuantity>(FALL_PARAM, 0.f, 1.f, 0.5f, "Fall");
		// Up (1) is In, the default.
		configSwitch(SOURCE_PARAM, 0.f, 1.f, 1.f, "Follower source", {"Out", "In"})->description =
			"Out follows the reverb before Mix. A patched Side overrides both";

		// Percentages where there is no natural unit; SEND and WIDTH read
		// 100% at noon (unity).
		configParam(SEND_PARAM, 0.f, 1.f, robotboy_beads::kSendDefault, "Send", "%", 0.f, 100.f);
		configParam(FILTER_PARAM, 0.f, 1.f, 0.5f, "Filter", "%", 0.f, 200.f, -100.f)->description =
			"Lowpass below noon, highpass above";
		configParam(SMEAR_PARAM, 0.f, 1.f, 0.f, "Smear", "%", 0.f, 100.f);
		configParam<PredelayQuantity>(PREDELAY_PARAM, 0.f, 1.f, rb::kPredelayDefault, "Pre-delay", " ms");
		// 1x is Beads' wow and flutter at Scorched.
		configParam<SquareLawQuantity>(WOW_PARAM, 0.f, 1.f, 0.f, "Wow & flutter", "x")->scale = 3.f;
		configParam<SizeQuantity>(SIZE_PARAM, 0.f, 1.f, rb::kSizeDefault, "Size", "x");
		configParam<DecayQuantity>(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", "%", 0.f, 100.f);
		// Defaults reproduce the original Beads reverb (rb::k*Default).
		configParam(DAMP_PARAM, 0.f, 1.f, rb::kDampDefault, "Damp", "%", 0.f, 100.f);
		configParam(DIFFUSE_PARAM, 0.f, 1.f, rb::kDiffuseDefault, "Diffuse", "%", 0.f, 100.f);
		configParam<SpeedQuantity>(SPEED_PARAM, 0.f, 1.f, rb::kSpeedDefault, "Mod speed", "x");
		configParam<SquareLawQuantity>(DEPTH_PARAM, 0.f, 1.f, rb::kDepthDefault, "Mod depth", "x")->scale = 4.f;
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.5f, "Width", "%", 0.f, 200.f);
		configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f);

		static const char* const kNames[kNumControls] = {
			"Send", "Filter", "Smear", "Pre-delay", "Wow & flutter",
			"Size", "Decay", "Damp", "Diffuse", "Mod speed", "Mod depth", "Width", "Mix",
		};
		for (int i = 0; i < kNumControls; i++) {
			ParamQuantity* trim = configParam(SEND_CV_PARAM + i, -1.f, 1.f, 0.f,
				string::f("%s CV amount", kNames[i]), "%", 0.f, 100.f);
			trim->description = "Envelope amount while the CV jack is unpatched";
			configInput(SEND_INPUT + i, string::f("%s CV", kNames[i]));
		}

		configInput(IN_L_INPUT, "Audio L");
		configInput(IN_R_INPUT, "Audio R");
		configInput(CLEAR_INPUT, "Clear trigger");
		configInput(SIDE_INPUT, "Sidechain");
		configOutput(OUT_L_OUTPUT, "Audio L");
		configOutput(OUT_R_OUTPUT, "Audio R");
		configOutput(ENV_OUTPUT, "Envelope");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// The Mode and Quality lights sit in their buttons.
		configLight(ENV_LIGHT, "Envelope");
		configLight(BALANCE_R_LIGHT, "Dry/wet balance")->description =
			"Cyan: wet louder. Amber: dry louder. White: even.";

		lightDivider.setDivision(32);
		sampleRate = APP->engine->getSampleRate();
		follower.Init(sampleRate);
		balance.Init(sampleRate);
		applySettingsToEngine();
		// The engine allocates both tanks' memory; if that fails it outputs
		// silence. (WARN is defined by both hosts' logger.hpp.)
		if (!engine.Init(sampleRate))
			WARN("Brume: could not allocate the reverb memory");
	}

	// Mode, Quality and wow scope as requests; the engine applies them at
	// its next block, or at Init.
	void applySettingsToEngine() {
		engine.SetMode(static_cast<rb::BrumeMode>(mode));
		engine.SetQuality(static_cast<rb::BrumeQuality>(quality));
		engine.SetWowScope(static_cast<rb::WowScope>(wowScope));
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		engine.SetHostRate(sampleRate);
		follower.SetSampleRate(sampleRate);
		balance.Init(sampleRate);
		fpuConfigured = false;
	}

	void process(const ProcessArgs& args) override {
		// Re-armed on a sample rate change, which may come with a new audio
		// thread.
		if (!fpuConfigured) {
			fpuConfigured = true;
			rb::EnableMetaModuleFlushToZero();
		}
		started.store(true, std::memory_order_relaxed);

#ifdef METAMODULE
		mode = clamp((int)(params[MODE_PARAM].getValue() + 0.5f), 0, 1);
		quality = clamp((int)(params[QUALITY_PARAM].getValue() + 0.5f), 0, 2);
#else
		if (modeButton.process(params[MODE_PARAM].getValue() > 0.5f))
			mode = (mode + 1) % 2;
		if (qualityButton.process(params[QUALITY_PARAM].getValue() > 0.5f))
			quality = (quality + 1) % 3;
#endif
		applySettingsToEngine();

		// Clear: the button, or a rising edge at Beads' gate thresholds.
		const bool clearEdge = clearGate.process(inputs[CLEAR_INPUT].getVoltage(), 0.1f, 1.f);
		if (clearButton.process(params[CLEAR_PARAM].getValue() > 0.5f) || clearEdge)
			engine.RequestClear();

		// One patched input feeds both sides.
		const bool lPatched = inputs[IN_L_INPUT].isConnected();
		const bool rPatched = inputs[IN_R_INPUT].isConnected();
		const rb::BrumeInput in = rb::NormalizeInputs(lPatched, rPatched,
			inputs[IN_L_INPUT].getVoltage() * 0.2f, inputs[IN_R_INPUT].getVoltage() * 0.2f);

		// Envelope follower: SIDE if patched (5 V = 1.0), else the input or,
		// with SOURCE at Out, the previous sample's wet.
		const bool sidePatched = inputs[SIDE_INPUT].isConnected();
		const bool followWet = params[SOURCE_PARAM].getValue() < 0.5f;
		const int source = sidePatched ? 2 : followWet ? 3 : (lPatched || rPatched) ? 1 : 0;
		if (source != followerSource) {
			followerSource = source;
			follower.Restart();
		}
		const rb::BrumeFrame wet = engine.wet();
		float rectified = sidePatched ? std::fabs(inputs[SIDE_INPUT].getVoltage()) * 0.2f
			: followWet ? std::max(std::fabs(wet.l), std::fabs(wet.r))
			: std::max(std::fabs(in.l), std::fabs(in.r));
		if (!std::isfinite(rectified))
			rectified = 0.f;
		const float rise = params[RISE_PARAM].getValue();
		const float fall = params[FALL_PARAM].getValue();
		if (rise != appliedRise || fall != appliedFall) {
			appliedRise = rise;
			appliedFall = fall;
			follower.set_times(rb::FollowerTime(rise), rb::FollowerTime(fall));
		}
		follower.set_auto_gain(followerAutoGain);
		envelope = follower.Process(rectified);

		// Each plate control: knob + CV x trimpot, or + envelope x trimpot
		// while its jack is unpatched.
		float values[kNumControls];
		for (int i = 0; i < kNumControls; i++) {
			const float knob = params[SEND_PARAM + i].getValue();
			const bool patched = inputs[SEND_INPUT + i].isConnected();
			const float volts = inputs[SEND_INPUT + i].getVoltage();
			const float trim = params[SEND_CV_PARAM + i].getValue();
			values[i] = rb::ModulatedKnob(knob, patched, volts, trim, envelope);
		}
		rb::BrumeControls controls;
		controls.send = values[SEND_PARAM - SEND_PARAM];
		controls.filter = values[FILTER_PARAM - SEND_PARAM];
		controls.smear = values[SMEAR_PARAM - SEND_PARAM];
		controls.predelay = values[PREDELAY_PARAM - SEND_PARAM];
		controls.wow = values[WOW_PARAM - SEND_PARAM];
		controls.size = values[SIZE_PARAM - SEND_PARAM];
		controls.decay = values[DECAY_PARAM - SEND_PARAM];
		controls.damp = values[DAMP_PARAM - SEND_PARAM];
		controls.diffuse = values[DIFFUSE_PARAM - SEND_PARAM];
		controls.speed = values[SPEED_PARAM - SEND_PARAM];
		controls.depth = values[DEPTH_PARAM - SEND_PARAM];
		controls.width = values[WIDTH_PARAM - SEND_PARAM];
		controls.dry_wet = values[DRY_WET_PARAM - SEND_PARAM];
		engine.SetControls(controls);

		const rb::BrumeFrame out = engine.Process(in.l, in.r);
		const rb::BrumeFrame dryMix = engine.dry_mix();
		const rb::BrumeFrame wetMix = engine.wet_mix();
		balance.Process(dryMix.l, dryMix.r, wetMix.l, wetMix.r);
		// With Out R unpatched, Out L carries the mono sum.
		const bool rConnected = outputs[OUT_R_OUTPUT].isConnected();
		outputs[OUT_L_OUTPUT].setVoltage((rConnected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
		outputs[OUT_R_OUTPUT].setVoltage(out.r * 5.f);
		outputs[ENV_OUTPUT].setVoltage(envelope * 10.f);

		if (lightDivider.process()) {
			const int m = static_cast<int>(engine.active_mode());
			const int q = static_cast<int>(engine.active_quality());
			for (int c = 0; c < 3; c++) {
				lights[MODE_R_LIGHT + c].setBrightness(kModeColors[m][c]);
				lights[QUALITY_R_LIGHT + c].setBrightness(kQualityColors[q][c]);
			}
			lights[ENV_LIGHT].setBrightness(envelope);
			float rgb[3];
			balance.Color(rgb);
			for (int c = 0; c < 3; c++)
				lights[BALANCE_R_LIGHT + c].setBrightness(rgb[c]);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		mode = 0;
		quality = 0;
		wowScope = 1;
		followerAutoGain = true;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "quality", json_integer(quality));
		json_object_set_new(rootJ, "wowScope", json_integer(wowScope));
		json_object_set_new(rootJ, "followerAutoGain", json_boolean(followerAutoGain));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// A setting the patch doesn't have goes back to its default.
		json_t* j = json_object_get(rootJ, "mode");
		mode = j ? clamp((int)json_integer_value(j), 0, 1) : 0;
		j = json_object_get(rootJ, "quality");
		quality = j ? clamp((int)json_integer_value(j), 0, 2) : 0;
		j = json_object_get(rootJ, "wowScope");
		wowScope = j ? clamp((int)json_integer_value(j), 0, 1) : 1;
		j = json_object_get(rootJ, "followerAutoGain");
		followerAutoGain = j ? json_boolean_value(j) : true;

		// Loading a patch: start the engine in the saved Mode and Quality.
		// Rack calls this before the module first runs (and, later, under
		// the engine lock), so re-initializing here can't race process().
		applySettingsToEngine();
		if (!started.load(std::memory_order_relaxed) && engine.ok()
				&& (static_cast<int>(engine.active_mode()) != mode
					|| static_cast<int>(engine.active_quality()) != quality))
			engine.Init(sampleRate);
	}
};

#ifdef METAMODULE
// The MetaModule adapter (firmware/vcv_plugin/internal/make_element.cc)
// turns a non-momentary SvgSwitch into a FlipSwitch whose position count
// comes from the configSwitch range and whose position names come from its
// labels; with 3 or more frames it is never treated as momentary, and with 2
// it is as long as momentary is false. So both switches show their setting's
// name. Each frame's color comes from metamodule/assets/*.png
// (asset::plugin() maps .svg to .png there, so the .svg files never exist).
struct BrumeModeMmSwitch : SvgSwitch {
	BrumeModeMmSwitch() {
		momentary = false;
		for (const char* name : {"beads", "clouds"})
			addFrame(Svg::load(asset::plugin(pluginInstance, string::f("res/mode_%s.svg", name))));
	}
};

struct BrumeQualityMmSwitch : SvgSwitch {
	BrumeQualityMmSwitch() {
		momentary = false;
		for (const char* name : {"bright", "cold", "scorched"})
			addFrame(Svg::load(asset::plugin(pluginInstance, string::f("res/quality_%s.svg", name))));
	}
};
#endif

struct BrumeWidget : ModuleWidget {
	BrumeWidget(Brume* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Brume.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Plate controls (panel-specs/brume.yaml): knob, CV jack, trimpot.
		struct Cell { float x, knobY, jackY, trimY; };
		static const Cell kCells[Brume::kNumControls] = {
			{31.732f, 30.637f, 44.138f, 54.138f},  // SEND
			{73.660f, 30.637f, 44.138f, 54.138f},  // FILTER
			{94.624f, 30.637f, 44.138f, 54.138f},  // SMEAR
			{115.588f, 30.637f, 44.138f, 54.138f},  // PREDELAY
			{52.696f, 30.637f, 44.138f, 54.138f},  // WOW, after SEND on the panel
			{29.985f, 77.688f, 91.188f, 101.188f},  // SIZE
			{47.455f, 77.688f, 91.188f, 101.188f},  // DECAY
			{64.925f, 77.688f, 91.188f, 101.188f},  // DAMP
			{82.395f, 77.688f, 91.188f, 101.188f},  // DIFFUSE
			{99.865f, 77.688f, 91.188f, 101.188f},  // SPEED
			{117.335f, 77.688f, 91.188f, 101.188f},  // DEPTH
			{136.945f, 24.518f, 36.518f, 46.018f},  // WIDTH
			{136.945f, 67.561f, 79.561f, 89.061f},  // MIX
		};
		for (int i = 0; i < Brume::kNumControls; i++) {
			const Cell c = kCells[i];
			addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c.x, c.knobY)), module, Brume::SEND_PARAM + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(c.x, c.jackY)), module, Brume::SEND_INPUT + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(c.x, c.trimY)), module, Brume::SEND_CV_PARAM + i));
		}

		// FOLLOW column.
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.375, 27.000)), module, Brume::RISE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.375, 46.150)), module, Brume::FALL_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(7.515, 63.045)), module, Brume::SOURCE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.375, 75.010)), module, Brume::SIDE_INPUT));
		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(10.375, 11.000)), module, Brume::ENV_LIGHT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.375, 95.000)), module, Brume::ENV_OUTPUT));

		// Output column: WIDTH and MIX are plate controls; the balance light
		// sits at the foot, on the bottom plate's trimpot row.
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(136.945, 101.188)), module, Brume::BALANCE_R_LIGHT));

		// Bottom row.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.150, 114.3)), module, Brume::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.850, 114.3)), module, Brume::IN_R_INPUT));
#ifdef METAMODULE
		addParam(createParamCentered<BrumeModeMmSwitch>(mm2px(Vec(44.871, 114.3)), module, Brume::MODE_PARAM));
		addParam(createParamCentered<BrumeQualityMmSwitch>(mm2px(Vec(69.477, 114.3)), module, Brume::QUALITY_PARAM));
#else
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(
			mm2px(Vec(44.871, 114.3)), module, Brume::MODE_PARAM, Brume::MODE_R_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(
			mm2px(Vec(69.477, 114.3)), module, Brume::QUALITY_PARAM, Brume::QUALITY_R_LIGHT));
#endif
		addParam(createParamCentered<VCVButton>(mm2px(Vec(93.533, 114.3)), module, Brume::CLEAR_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(102.033, 114.3)), module, Brume::CLEAR_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(127.470, 114.3)), module, Brume::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(137.170, 114.3)), module, Brume::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Brume* module = getModule<Brume>();
		if (!module)
			return;

		menu->addChild(new MenuSeparator);
#ifndef METAMODULE
		// On MetaModule Mode and Quality are panel switches, which would
		// override these.
		menu->addChild(createIndexSubmenuItem("Mode", kModeNames,
			[=]() { return module->mode; },
			[=](int i) { withMenuUndo(module, "change mode", [=]() { module->mode = i; }); }));
		menu->addChild(createIndexSubmenuItem("Quality", kQualityNames,
			[=]() { return module->quality; },
			[=](int i) { withMenuUndo(module, "change quality", [=]() { module->quality = i; }); }));
#endif

		menu->addChild(createIndexSubmenuItem("Wow & flutter", {"Input", "Reverb only"},
			[=]() { return module->wowScope; },
			[=](int i) { withMenuUndo(module, "change wow scope", [=]() { module->wowScope = i; }); }));

		// Switching it back on restarts the follower's level learning.
		menu->addChild(createBoolMenuItem("Follower auto-gain", "",
			[=]() { return module->followerAutoGain; },
			[=](bool on) {
				withMenuUndo(module, on ? "enable follower auto-gain" : "disable follower auto-gain",
					[=]() { module->followerAutoGain = on; });
			}));
	}
};

Model* modelBrume = createModel<Brume, BrumeWidget>("Brume");
