#include "BeadsModule.hpp"
#include "Lucarne.hpp"
#include "host/flush_to_zero.h"

#include <algorithm>
#include <cmath>

static const std::vector<std::string> kQualityNames = {
	"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette",
};
// The Quality bezel's color for each setting: white, cyan, amber, magenta.
static constexpr float kQualityColors[4][3] = {
	{1.f, 1.f, 1.f}, {0.f, 1.f, 1.f}, {1.f, 0.5f, 0.f}, {1.f, 0.f, 1.f},
};

// Beads' manual gain byte for a gain in dB: the firmware maps byte g to
// 1 + 39 * (g / 255)^4 (gain_control.cc); 0 means auto gain.
static uint8_t manualGainByte(float db) {
	const float gain = std::pow(10.f, clamp(db, 0.f, 32.f) / 20.f);
	const float g = 255.f * std::pow(std::max(gain - 1.f, 0.f) / 39.f, 0.25f);
	return (uint8_t)clamp((int)std::lround(g), 1, 255);
}

BeadsModule::BeadsModule(beads::Processor::ModeLock mode)
	: delayMode(mode == beads::Processor::MODE_DELAY) {
	lightDivider.setDivision(32);
	expanderDivider.setDivision(256);
	// The host allocates Beads' delay memory; if that fails it outputs
	// silence. (WARN is defined by both hosts' logger.hpp.)
	if (!host.Init(mode, true, APP->engine->getSampleRate()))
		WARN("%s: could not allocate the Beads delay memory", delayMode ? "Retours" : "Particules");
	host.SetGrainFeed(&grainFeed);
}

// Desktop: a button that steps through the four settings, its bezel lit in
// the setting's color. MetaModule: a 4-position switch, so the firmware can
// show the setting's name.
struct QualityParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		BeadsModule* m = dynamic_cast<BeadsModule*>(module);
		return m ? kQualityNames[m->quality] : "";
	}
};

void BeadsModule::configQuality() {
#ifdef METAMODULE
	configSwitch(QUALITY_PARAM, 0.f, 3.f, 0.f, "Quality", kQualityNames)->randomizeEnabled = false;
#else
	configButton<QualityParamQuantity>(QUALITY_PARAM, "Quality");
#endif
}

BeadsModule::Gates BeadsModule::processGates() {
	// 0.1 V / 1 V hysteresis, as Beads' gate inputs.
	freezeGate.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
	seedGate.process(inputs[SEED_INPUT].getVoltage(), 0.1f, 1.f);
	return {params[FREEZE_PARAM].getValue() > 0.5f || freezeGate.isHigh(), seedGate.isHigh()};
}

robotboy_beads::HostFrameOut BeadsModule::processEngine(const ProcessArgs& args, Gates gates) {
	// Is a Lucarne attached on the right, and not bypassed? dynamic_cast,
	// not model ==: MetaModule SDK < 2.4 leaves model unset on adapter modules.
	if (expanderDivider.process()) {
		Lucarne* lucarne = dynamic_cast<Lucarne*>(rightExpander.module);
		grainFeed.attached.store(lucarne && !lucarne->isBypassed(), std::memory_order_relaxed);
	}
	// Re-armed on a sample rate change, which may come with a new audio thread.
	if (!fpuConfigured) {
		fpuConfigured = true;
		robotboy_beads::EnableMetaModuleFlushToZero();
	}

#ifdef METAMODULE
	quality = clamp((int)(params[QUALITY_PARAM].getValue() + 0.5f), 0, 3);
#else
	// The button is ignored while frozen, so a press can't promise a change
	// the host holds back until Freeze releases. (The menu's Quality item
	// isn't blocked; the host defers that change the same way.)
	if (qualityButton.process(params[QUALITY_PARAM].getValue() > 0.5f) && !gates.frozen)
		quality = (quality + 1) % 4;
#endif
	host.SetQuality(static_cast<robotboy_beads::Quality>(quality));
	host.SetHissEnabled(tapeHiss);
	host.SetInputs(inputs[IN_L_INPUT].isConnected(), inputs[IN_R_INPUT].isConnected());

	const float l = inputs[IN_L_INPUT].getVoltage() * 0.2f;
	const float r = inputs[IN_R_INPUT].getVoltage() * 0.2f;
	if (std::isfinite(l) && std::isfinite(r))
		inputPeak = std::max(std::max(std::fabs(l), std::fabs(r)), inputPeak * (1.f - 2.f * args.sampleTime));

	const robotboy_beads::HostFrameOut out = host.Process({l, r, gates.frozen, gates.seed}, this);

	// With Out R unpatched, Out L carries the mono sum.
	const bool rConnected = outputs[OUT_R_OUTPUT].isConnected();
	outputs[OUT_L_OUTPUT].setVoltage((rConnected ? out.l : (out.l + out.r) * 0.5f) * 5.f);
	outputs[OUT_R_OUTPUT].setVoltage(out.r * 5.f);
	// Beads' gate stream (what its gate-output mode puts on the right
	// output): high for the first half of each grain or repeat.
	outputs[GATE_OUTPUT].setVoltage(out.gate ? 10.f : 0.f);
	return out;
}

void BeadsModule::updateSharedLights(bool frozen) {
	lights[FREEZE_LIGHT].setBrightness(frozen ? 1.f : 0.f);
	lights[QUALITY_R_LIGHT].setBrightness(kQualityColors[quality][0]);
	lights[QUALITY_G_LIGHT].setBrightness(kQualityColors[quality][1]);
	lights[QUALITY_B_LIGHT].setBrightness(kQualityColors[quality][2]);
}

void BeadsModule::readSharedControls(robotboy_beads::BlockControls* c) {
	if (clearRequested.exchange(false))
		host.RequestClear();
	if (recalibrateRequested.exchange(false))
		host.RecalibrateGain();
	c->gain_setting = autoGain ? 0 : manualGainByte(manualGainDb);
}

void BeadsModule::onSampleRateChange(const SampleRateChangeEvent& e) {
	host.SetHostRate(e.sampleRate);
	fpuConfigured = false;
}

void BeadsModule::onReset(const ResetEvent& e) {
	Module::onReset(e);
	quality = 0;
	autoGain = true;
	manualGainDb = 0.f;
	tapeHiss = true;
	clearRequested = true;
	recalibrateRequested = true;
}

json_t* BeadsModule::dataToJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "qualityState", json_integer(quality));
	json_object_set_new(rootJ, "autoGain", json_boolean(autoGain));
	json_object_set_new(rootJ, "manualGainDb", json_real(manualGainDb));
	json_object_set_new(rootJ, "tapeHiss", json_boolean(tapeHiss));
	return rootJ;
}

void BeadsModule::dataFromJson(json_t* rootJ) {
	// A setting the patch doesn't have goes back to its default.
	json_t* j = json_object_get(rootJ, "qualityState");
	quality = j ? clamp((int)json_integer_value(j), 0, 3) : 0;
	j = json_object_get(rootJ, "autoGain");
	autoGain = j ? json_boolean_value(j) : true;
	j = json_object_get(rootJ, "manualGainDb");
	manualGainDb = j ? clamp((float)json_real_value(j), 0.f, 32.f) : 0.f;
	j = json_object_get(rootJ, "tapeHiss");
	tapeHiss = j ? json_boolean_value(j) : true;
}

#ifdef METAMODULE
// The MetaModule adapter turns a non-momentary SvgSwitch with 3 or more
// frames plus a configSwitch into a labeled switch; each frame's color comes
// from metamodule/assets/quality_*.png (asset::plugin() maps .svg to .png
// there, so the .svg files never exist).
struct QualityMmSwitch : SvgSwitch {
	QualityMmSwitch() {
		momentary = false;
		for (const char* name : {"bright", "cold", "sunny", "scorched"})
			addFrame(Svg::load(asset::plugin(pluginInstance, string::f("res/quality_%s.svg", name))));
	}
};
#endif

ParamWidget* createQualityParamCentered(math::Vec pos, BeadsModule* module) {
#ifdef METAMODULE
	return createParamCentered<QualityMmSwitch>(pos, module, BeadsModule::QUALITY_PARAM);
#else
	return createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(
		pos, module, BeadsModule::QUALITY_PARAM, BeadsModule::QUALITY_R_LIGHT);
#endif
}

// The input level, linear (+-5 V = 1.0), in dB; at or below -60 dB, and for
// NaN, "silent".
static std::string formatInputLevel(float level) {
	if (!(level > 0.001f))
		return "silent";
	return string::f("%.1f dB", 20.f * std::log10(level));
}

#ifndef METAMODULE
struct ManualGainQuantity : Quantity {
	BeadsModule* module;
	ManualGainQuantity(BeadsModule* module) : module(module) {}
	void setValue(float value) override { module->manualGainDb = clamp(value, getMinValue(), getMaxValue()); }
	float getValue() override { return module->manualGainDb; }
	float getMinValue() override { return 0.f; }
	float getMaxValue() override { return 32.f; }
	float getDefaultValue() override { return 0.f; }
	std::string getLabel() override { return "Manual gain"; }
	std::string getUnit() override { return " dB"; }
	std::string getDisplayValueString() override { return string::f("%.1f", getValue()); }
};

struct ManualGainSlider : ui::Slider {
	ManualGainSlider(BeadsModule* module) {
		quantity = new ManualGainQuantity(module);
		box.size.x = 200.f;
	}
	~ManualGainSlider() {
		delete quantity;
	}
};
#endif

// Gain: shows the gain auto gain applies, or opens the manual setting. The
// state is also set when the menu opens, because MetaModule menus don't
// call step().
struct GainItem : MenuItem {
	BeadsModule* module;

	void refresh() {
		disabled = module->autoGain;
		rightText = module->autoGain
			? string::f("%.1f dB", module->host.gain_db())
			: string::f("%.1f dB ▸", module->manualGainDb);
	}

	void step() override {
		refresh();
		MenuItem::step();
	}

	Menu* createChildMenu() override {
		if (module->autoGain)
			return nullptr;
		Menu* menu = new Menu;
#ifdef METAMODULE
		BeadsModule* m = module;
		for (int db = 0; db <= 32; db++) {
			menu->addChild(createCheckMenuItem(string::f("%d dB", db), "",
				[m, db]() { return (int)std::round(m->manualGainDb) == db; },
				[m, db]() { m->manualGainDb = (float)db; }));
		}
#else
		menu->addChild(new ManualGainSlider(module));
#endif
		return menu;
	}
};

// Input: the live input level (display only; refreshed by step() on VCV,
// set once when the menu opens on MetaModule).
struct InputLevelItem : MenuItem {
	BeadsModule* module;

	void step() override {
		rightText = formatInputLevel(module->inputPeak);
		MenuItem::step();
	}
};

void appendBeadsMenu(ui::Menu* menu, BeadsModule* module) {
#ifndef METAMODULE
	// On MetaModule Quality is a panel switch, which would override this.
	menu->addChild(createIndexSubmenuItem("Quality", kQualityNames,
		[=]() { return module->quality; },
		[=](int i) { withMenuUndo(module, "change quality", [=]() { module->quality = i; }); }));
#endif

	// Turning auto gain on (again) restarts Beads' level learning; off
	// switches to the manual gain.
	menu->addChild(createBoolMenuItem("Auto gain", "",
		[=]() { return module->autoGain; },
		[=](bool on) {
			withMenuUndo(module, on ? "enable auto gain" : "disable auto gain", [=]() { module->autoGain = on; });
			if (on)
				module->recalibrateRequested = true;
		}));

	GainItem* gainItem = createMenuItem<GainItem>("Gain");
	gainItem->module = module;
	gainItem->refresh();
	menu->addChild(gainItem);

	InputLevelItem* inputItem = createMenuItem<InputLevelItem>("Input", formatInputLevel(module->inputPeak));
	inputItem->module = module;
	inputItem->disabled = true;
	menu->addChild(inputItem);

	menu->addChild(createBoolMenuItem("Tape hiss (Scorched cassette)", "",
		[=]() { return module->tapeHiss; },
		[=](bool on) {
			withMenuUndo(module, on ? "enable tape hiss" : "disable tape hiss", [=]() { module->tapeHiss = on; });
		}));
}

MenuItem* createClearBufferItem(BeadsModule* module) {
	// Applied by the engine at its next block.
	return createMenuItem("Clear buffer", "",
		[=]() { module->clearRequested = true; },
		module->host.buffer_empty());
}
