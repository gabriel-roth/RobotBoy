#include "plugin.hpp"
#include "host/brume_engine.h"
#include "host/envelope_follower.h"
#include "host/flush_to_zero.h"
#include "host/pitch_lock.h"

#include <atomic>
#include <cstdlib>

// Brume: a stereo reverb on Mutable Instruments' Beads and Clouds tanks
// (robotboy_beads::BrumeEngine), with send-side filter, smear and wow, a
// shimmer loop, and an envelope follower that drives every unpatched
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
		WOW_PARAM,
		SHIMMER_PARAM,
		INTERVAL_PARAM,
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
		WOW_CV_PARAM,
		SHIMMER_CV_PARAM,
		INTERVAL_CV_PARAM,
		DECAY_CV_PARAM,
		DAMP_CV_PARAM,
		DIFFUSE_CV_PARAM,
		SPEED_CV_PARAM,
		DEPTH_CV_PARAM,
		WIDTH_CV_PARAM,
		DRY_WET_CV_PARAM,
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
		WOW_INPUT,
		SHIMMER_INPUT,
		INTERVAL_INPUT,
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
		LIGHTS_LEN
	};

	// The 13 plate controls, in panel order: knob SEND_PARAM + i,
	// trimpot SEND_CV_PARAM + i, jack SEND_INPUT + i.
	static constexpr int kNumControls = 13;
	static constexpr int kIntervalIndex = INTERVAL_PARAM - SEND_PARAM;
	static_assert(DRY_WET_PARAM - SEND_PARAM == kNumControls - 1, "plate knobs");
	static_assert(DRY_WET_CV_PARAM - SEND_CV_PARAM == kNumControls - 1, "plate trimpots");
	static_assert(DRY_WET_INPUT - SEND_INPUT == kNumControls - 1, "plate jacks");

	rb::BrumeEngine engine;
	rb::EnvelopeFollower follower;
	rb::ScalePitchLock pitchLock;

	// Saved settings. Mode and Quality are the switches' params on
	// MetaModule; these mirror them there.
	int mode = 0;      // rb::BrumeMode
	int quality = 0;   // rb::BrumeQuality
	// Lock pitch: an rb::PitchLockMode; the root applies to the scale modes
	// only. The menu sets scaleChanged; process() applies it.
	int pitchScale = rb::kPitchOctaves;
	int pitchRoot = 0;
	std::atomic<bool> scaleChanged{true};
	int wowScope = 0;   // rb::WowScope
	bool followerAutoGain = true;

	float sampleRate = 48000.f;
	// Set by the first process(). Until then a patch's saved Mode and
	// Quality are latched by re-running Init, rather than reaching the
	// engine as a change (with a fade or spillover) at load.
	std::atomic<bool> started{false};
	bool fpuConfigured = false;

	// The follower's source: 0 nothing patched, 1 the input, 2 SIDE. A
	// change restarts its auto-gain learning.
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

		// Percentages where there is no natural unit; SEND and WIDTH read
		// 100% at noon (unity).
		configParam(SEND_PARAM, 0.f, 1.f, 0.5f, "Send", "%", 0.f, 200.f);
		configParam(FILTER_PARAM, 0.f, 1.f, 0.5f, "Filter", "%", 0.f, 200.f, -100.f)->description =
			"Lowpass below noon, highpass above";
		configParam(SMEAR_PARAM, 0.f, 1.f, 0.f, "Smear", "%", 0.f, 100.f);
		configParam(WOW_PARAM, 0.f, 1.f, 0.f, "Wow & flutter", "%", 0.f, 100.f);
		configParam(SHIMMER_PARAM, 0.f, 1.f, 0.f, "Shimmer", "%", 0.f, 100.f);
		configParam(INTERVAL_PARAM, 0.f, 1.f, 0.75f, "Interval", " st", 0.f, 48.f, -24.f)->displayPrecision = 3;
		configParam<DecayQuantity>(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", "%", 0.f, 100.f);
		configParam(DAMP_PARAM, 0.f, 1.f, 4.f / 7.f, "Damp", "%", 0.f, 100.f);
		configParam(DIFFUSE_PARAM, 0.f, 1.f, 0.65f, "Diffuse", "%", 0.f, 100.f);
		// 4^(2v - 1) = 0.25 * 16^v.
		configParam(SPEED_PARAM, 0.f, 1.f, 0.5f, "Mod speed", "x", 16.f, 0.25f);
		configParam(DEPTH_PARAM, 0.f, 1.f, 1.f, "Mod depth", "%", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.5f, "Width", "%", 0.f, 200.f);
		configParam(DRY_WET_PARAM, 0.f, 1.f, 0.5f, "Dry/wet", "%", 0.f, 100.f);

		static const char* const kNames[kNumControls] = {
			"Send", "Filter", "Smear", "Wow & flutter", "Shimmer", "Interval",
			"Decay", "Damp", "Diffuse", "Mod speed", "Mod depth", "Width", "Dry/wet",
		};
		for (int i = 0; i < kNumControls; i++) {
			ParamQuantity* trim = configParam(SEND_CV_PARAM + i, -1.f, 1.f, 0.f,
				string::f("%s CV amount", kNames[i]), "%", 0.f, 100.f);
			trim->description = "Envelope amount while the CV jack is unpatched";
			configInput(SEND_INPUT + i, string::f("%s CV", kNames[i]));
		}
		inputInfos[INTERVAL_INPUT]->description = "1V/octave at full trimpot";

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

		lightDivider.setDivision(32);
		sampleRate = APP->engine->getSampleRate();
		follower.Init(sampleRate);
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
		if (scaleChanged.exchange(false))
			pitchLock.Set(pitchScale, pitchRoot);

		// Clear: the button, or a rising edge at Beads' gate thresholds.
		const bool clearEdge = clearGate.process(inputs[CLEAR_INPUT].getVoltage(), 0.1f, 1.f);
		if (clearButton.process(params[CLEAR_PARAM].getValue() > 0.5f) || clearEdge)
			engine.RequestClear();

		// One patched input feeds both sides.
		const bool lPatched = inputs[IN_L_INPUT].isConnected();
		const bool rPatched = inputs[IN_R_INPUT].isConnected();
		const rb::BrumeInput in = rb::NormalizeInputs(lPatched, rPatched,
			inputs[IN_L_INPUT].getVoltage() * 0.2f, inputs[IN_R_INPUT].getVoltage() * 0.2f);

		// Envelope follower: SIDE if patched, else the input (5 V = 1.0).
		const bool sidePatched = inputs[SIDE_INPUT].isConnected();
		const int source = sidePatched ? 2 : (lPatched || rPatched) ? 1 : 0;
		if (source != followerSource) {
			followerSource = source;
			follower.Restart();
		}
		float rectified = sidePatched
			? std::fabs(inputs[SIDE_INPUT].getVoltage()) * 0.2f
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
			if (i == kIntervalIndex) {
				float st = rb::ModulatedInterval(rb::IntervalSemitones(knob), patched, volts, trim, envelope);
				values[i] = pitchLock.active() ? pitchLock.Quantize(st) : st;
			}
			else {
				values[i] = rb::ModulatedKnob(knob, patched, volts, trim, envelope);
			}
		}
		rb::BrumeControls controls;
		controls.send = values[SEND_PARAM - SEND_PARAM];
		controls.filter = values[FILTER_PARAM - SEND_PARAM];
		controls.smear = values[SMEAR_PARAM - SEND_PARAM];
		controls.wow = values[WOW_PARAM - SEND_PARAM];
		controls.shimmer = values[SHIMMER_PARAM - SEND_PARAM];
		controls.interval_semitones = values[INTERVAL_PARAM - SEND_PARAM];
		controls.decay = values[DECAY_PARAM - SEND_PARAM];
		controls.damp = values[DAMP_PARAM - SEND_PARAM];
		controls.diffuse = values[DIFFUSE_PARAM - SEND_PARAM];
		controls.speed = values[SPEED_PARAM - SEND_PARAM];
		controls.depth = values[DEPTH_PARAM - SEND_PARAM];
		controls.width = values[WIDTH_PARAM - SEND_PARAM];
		controls.dry_wet = values[DRY_WET_PARAM - SEND_PARAM];
		engine.SetControls(controls);

		const rb::BrumeFrame out = engine.Process(in.l, in.r);
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
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		mode = 0;
		quality = 0;
		pitchScale = rb::kPitchOctaves;
		pitchRoot = 0;
		scaleChanged = true;
		wowScope = 0;
		followerAutoGain = true;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "mode", json_integer(mode));
		json_object_set_new(rootJ, "quality", json_integer(quality));
		json_object_set_new(rootJ, "pitchScale", json_integer(pitchScale));
		json_object_set_new(rootJ, "pitchRoot", json_integer(pitchRoot));
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
		j = json_object_get(rootJ, "pitchScale");
		pitchScale = j ? clamp((int)json_integer_value(j), 0, 7) : rb::kPitchOctaves;
		j = json_object_get(rootJ, "pitchRoot");
		pitchRoot = j ? clamp((int)json_integer_value(j), 0, 11) : 0;
		scaleChanged = true;
		j = json_object_get(rootJ, "wowScope");
		wowScope = j ? clamp((int)json_integer_value(j), 0, 1) : 0;
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
			{32.383f, 30.637f, 44.138f, 54.138f},  // SEND
			{53.508f, 30.637f, 44.138f, 54.138f},  // FILTER
			{74.632f, 30.637f, 44.138f, 54.138f},  // SMEAR
			{95.757f, 30.637f, 44.138f, 54.138f},  // WOW
			{117.695f, 30.637f, 44.138f, 54.138f},  // SHIMMER
			{136.445f, 30.637f, 44.138f, 54.138f},  // INTERVAL
			{30.27f, 77.688f, 91.188f, 101.188f},  // DECAY
			{47.17f, 77.688f, 91.188f, 101.188f},  // DAMP
			{64.07f, 77.688f, 91.188f, 101.188f},  // DIFFUSE
			{80.97f, 77.688f, 91.188f, 101.188f},  // SPEED
			{97.87f, 77.688f, 91.188f, 101.188f},  // DEPTH
			{117.695f, 77.688f, 91.188f, 101.188f},  // WIDTH
			{136.445f, 77.688f, 91.188f, 101.188f},  // DRY/WET
		};
		for (int i = 0; i < Brume::kNumControls; i++) {
			const Cell c = kCells[i];
			addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(c.x, c.knobY)), module, Brume::SEND_PARAM + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(c.x, c.jackY)), module, Brume::SEND_INPUT + i));
			addParam(createParamCentered<Trimpot>(mm2px(Vec(c.x, c.trimY)), module, Brume::SEND_CV_PARAM + i));
		}

		// FOLLOW column.
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.66, 38.072)), module, Brume::RISE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.66, 58.983)), module, Brume::FALL_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.66, 79.109)), module, Brume::SIDE_INPUT));
		addChild(createLightCentered<MediumLight<WhiteLight>>(mm2px(Vec(10.66, 20.361)), module, Brume::ENV_LIGHT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.66, 95.000)), module, Brume::ENV_OUTPUT));

		// Bottom row.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.150, 114.3)), module, Brume::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(19.850, 114.3)), module, Brume::IN_R_INPUT));
#ifdef METAMODULE
		addParam(createParamCentered<BrumeModeMmSwitch>(mm2px(Vec(44.871, 114.3)), module, Brume::MODE_PARAM));
		addParam(createParamCentered<BrumeQualityMmSwitch>(mm2px(Vec(69.478, 114.3)), module, Brume::QUALITY_PARAM));
#else
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(
			mm2px(Vec(44.871, 114.3)), module, Brume::MODE_PARAM, Brume::MODE_R_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(
			mm2px(Vec(69.478, 114.3)), module, Brume::QUALITY_PARAM, Brume::QUALITY_R_LIGHT));
#endif
		addParam(createParamCentered<VCVButton>(mm2px(Vec(93.534, 114.3)), module, Brume::CLEAR_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(102.034, 114.3)), module, Brume::CLEAR_INPUT));
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

		menu->addChild(createIndexSubmenuItem("Lock pitch",
			{"Off", "Octaves", "Octaves + 5ths", "Chromatic", "Major", "Minor", "Major pentatonic", "Minor pentatonic"},
			[=]() { return module->pitchScale; },
			[=](int i) {
				withMenuUndo(module, "change pitch lock", [=]() {
					module->pitchScale = i;
					module->scaleChanged = true;
				});
			}));
		// Greyed out unless a scale mode was selected when the menu opened.
		MenuItem* rootItem = createIndexSubmenuItem("Root",
			{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
			[=]() { return module->pitchRoot; },
			[=](int i) {
				withMenuUndo(module, "change scale root", [=]() {
					module->pitchRoot = i;
					module->scaleChanged = true;
				});
			});
		rootItem->disabled = module->pitchScale < robotboy_beads::kPitchChromatic;
		menu->addChild(rootItem);

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
