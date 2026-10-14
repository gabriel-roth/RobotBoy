#pragma once
#include "plugin.hpp"
#include "host/controls.h"
#include "host/engine_host.h"
#include "host/grain_feed.h"

#include <atomic>

// The harness Particules and Retours share around the Mutable Instruments
// Beads processor (vendored in src/beads/vendor, driven by
// robotboy_beads::EngineHost): Quality, input gain, tape hiss, the Freeze
// and Seed gates, the audio I/O, and Lucarne's display feed. Each module adds
// its own enums, its control mapping (ReadControls) and its widget.
//
// The audio thread owns the host. The menu changes settings through plain
// members, as Rack modules do, and sends one-shot commands through the
// atomic request flags, which ReadControls applies at the next engine block.
struct BeadsModule : Module, robotboy_beads::ControlSource {
	// Ids both panels share. Each module's own enums start with these, in
	// this order (static_asserts there check it).
	static constexpr int FREEZE_PARAM = 0, QUALITY_PARAM = 1;
	static constexpr int IN_L_INPUT = 0, IN_R_INPUT = 1, FREEZE_INPUT = 2, SEED_INPUT = 3;
	static constexpr int OUT_L_OUTPUT = 0, OUT_R_OUTPUT = 1, GATE_OUTPUT = 2;
	static constexpr int QUALITY_R_LIGHT = 0, QUALITY_G_LIGHT = 1, QUALITY_B_LIGHT = 2, FREEZE_LIGHT = 3;

	robotboy_beads::EngineHost host;
	// Filled by the host while a Lucarne sits to the right; read by Lucarne's
	// widget on the UI thread.
	robotboy_beads::GrainFeed grainFeed;
	const bool delayMode;   // Retours: Lucarne shows its echo scope

	// Saved settings.
	int quality = 0;   // 0-3: Bright digital, Cold digital, Sunny tape, Scorched cassette
	bool autoGain = true;
	float manualGainDb = 0.f;
	bool tapeHiss = true;   // Scorched cassette's synthetic hiss

	// Commands from the menu or a reset.
	std::atomic<bool> clearRequested{false};
	std::atomic<bool> recalibrateRequested{false};
	// Retours: the delay moves on its own (Time attenurandomizer off noon,
	// or Interval CV patched), so Lucarne shows the whole buffer. Particules
	// never sets it.
	std::atomic<bool> tapsModulated{false};

	// Input level for the gain menu (+-5 V = 1.0).
	float inputPeak = 0.f;

	dsp::ClockDivider lightDivider;
	dsp::ClockDivider expanderDivider;
	dsp::SchmittTrigger freezeGate;
	dsp::SchmittTrigger seedGate;
	dsp::BooleanTrigger qualityButton;
	bool fpuConfigured = false;

	struct Gates {
		bool frozen;   // Freeze button or gate
		bool seed;     // Seed input high
	};

	explicit BeadsModule(beads::Processor::ModeLock mode);

	// Call from the constructor, after config().
	void configQuality();

	// A module's process() calls processGates(), handles its own buttons,
	// then calls processEngine(), which runs the host and writes the outputs.
	Gates processGates();
	robotboy_beads::HostFrameOut processEngine(const ProcessArgs& args, Gates gates);
	// Freeze and Quality lights, at the light rate.
	void updateSharedLights(bool frozen);

	// The request flags and the gain setting; call from ReadControls.
	void readSharedControls(robotboy_beads::BlockControls* c);

	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onReset(const ResetEvent& e) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
};

// Pitch knob: Beads' notched +-24 semitone law, shown in semitones.
struct PitchParamQuantity : ParamQuantity {
	float getDisplayValue() override { return robotboy_beads::DetuneLaw(getValue()); }
	void setDisplayValue(float semitones) override { setValue(robotboy_beads::InverseDetuneLaw(semitones)); }
	std::string getDisplayValueString() override {
		float st = getDisplayValue();
		if (std::fabs(st - std::round(st)) < 0.05f)
			return string::f("%d", (int)std::round(st));
		return string::f("%.1f", st);
	}
	std::string getUnit() override { return " st"; }
};

// Panel and menu pieces both modules use.
ParamWidget* createQualityParamCentered(math::Vec pos, BeadsModule* module);
void appendBeadsMenu(ui::Menu* menu, BeadsModule* module);   // Quality on VCV, gain, tape hiss
MenuItem* createClearBufferItem(BeadsModule* module);
