#pragma once
#include "plugin.hpp"
#include "LooperModuleDSP.hpp"
#include <string>

// What Loooop and Löp share on VCV: the record/clear transport, a
// playhead's controls, the dry/wet mix, the lights, their widgets and their
// common menu items. M is the module itself; its enums supply the ids, which
// differ between the two (their order matches the MetaModule builds).
template <class M>
struct LooperModule : Module {
	static constexpr float kSmoothingTau = 0.002f;   // pan and dry/wet smoothing, seconds

	LoopEngine engine;
	dsp::SchmittTrigger recordButton, recordTrigger, clearButton, clearTrigger;
	dsp::SchmittTrigger headTrigger[LoopEngine::NUM_HEADS];
	loooop::RecordGateHelper recordGate;
	bool recordGateSynced = false;
	float lastJumpV[LoopEngine::NUM_HEADS] = {};
	loooop::VOctSpeedMemo vOctMemo[LoopEngine::NUM_HEADS];
	robotboy::OnePoleSmoother mixSmoother{1.f, 1.f};   // starts at the Dry/wet default
	float overdubPhase = 0.f;                          // Lock blink phase, [0,1)

	// One playhead's param and input ids.
	struct HeadIds {
		int speed, speedVOct, position, size, jitter, oneShot;
		int speedCv, positionCv, sizeCv, jitterCv, trigger, jump;
	};

	explicit LooperModule(int numHeads) : engine(numHeads) {
		engine.reset(48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
		// The engine keeps its current buffer when it can't allocate a larger
		// one for the new rate: still usable, with a shorter maximum loop.
		// Rack has no user notification for this, so it goes to the log.
		if (engine.consumeBufferShortfall())
			WARN("%s: record buffer allocation for %g Hz failed; max loop time is now %.1f s",
			     model->name.c_str(), e.sampleRate, engine.maxLoopSamples() / (double)e.sampleRate);
		mixSmoother.setAlpha(robotboy::smootherAlpha(e.sampleRate, kSmoothingTau));
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.clear();
	}

	// Overdub mode, crossfade, grid, record and clear. Returns the Overdub
	// index for updateLights().
	int processTransport() {
		int od = (int)std::round(params[M::OVERDUB_PARAM].getValue());
		loooop::applyOverdub(engine, od);
		engine.setCrossfade(params[M::CROSSFADE_PARAM].getValue() < 0.5f);   // 0 = On
		engine.setGrid(loooop::gridSegments((int)std::round(params[M::GRID_PARAM].getValue())));

		// RecordGateHelper does its own edge detection, so it reads the
		// triggers' levels rather than their edges.
		recordButton.process(params[M::RECORD_PARAM].getValue());
		recordTrigger.process(inputs[M::RECORD_TRIG_INPUT].getVoltage(), 0.1f, 2.f);
		if (!recordGateSynced) {
			recordGate.syncTo(recordTrigger.isHigh());   // no phantom edge on patch load
			recordGateSynced = true;
		}
		bool gateMode = params[M::REC_GATE_MODE_PARAM].getValue() > 0.5f;
		auto action = recordGate.step(gateMode, recordTrigger.isHigh(), recordButton.isHigh(),
		                              engine.isRecording());
		if (action != loooop::RecordGateHelper::Action::None)
			engine.toggleRecord(params[M::TRIG_WHEN_REC_PARAM].getValue() > 0.5f);

		bool clearPressed = clearButton.process(params[M::CLEAR_PARAM].getValue());
		bool clearTriggered = clearTrigger.process(inputs[M::CLEAR_TRIG_INPUT].getVoltage(), 0.1f, 2.f);
		if (clearPressed || clearTriggered)
			engine.clear();
		return od;
	}

	// One playhead's knobs, CVs and jacks. 10 V of CV spans a param's range.
	void processHead(int h, const HeadIds& id) {
		float speedKnob = loooop::applySpeedNotch(params[id.speed].getValue());
		float speedCv = inputs[id.speedCv].getVoltage();
		engine.setSpeed(h, params[id.speedVOct].getValue() > 0.5f
			? vOctMemo[h].get(speedKnob, speedCv)
			: loooop::speedFromControls(speedKnob, speedCv));
		engine.setPosition(h, loooop::normalizedControl(
			params[id.position].getValue(), inputs[id.positionCv].getVoltage()));
		engine.setSize(h, loooop::normalizedControl(
			params[id.size].getValue(), inputs[id.sizeCv].getVoltage()));
		engine.setJitter(h, loooop::normalizedControl(
			params[id.jitter].getValue(), inputs[id.jitterCv].getVoltage()));

		bool oneShot = params[id.oneShot].getValue() > 0.5f;
		engine.setOneShot(h, oneShot);
		if (headTrigger[h].process(inputs[id.trigger].getVoltage(), 0.1f, 2.f)) {
			if (oneShot)
				engine.triggerOneShot(h);
			else
				engine.restartHead(h);
		}
		float jumpV = inputs[id.jump].getVoltage();
		if (std::fabs(jumpV - lastJumpV[h]) > 0.05f) {
			engine.jumpHead(h, clamp(jumpV / 10.f, 0.f, 1.f));
			lastJumpV[h] = jumpV;
		}
	}

	// The audio input, scaled to the engine's ±1 for ±5 V.
	loooop::StereoInput readInput() {
		loooop::StereoInput in = loooop::normalledStereo(
			inputs[M::AUDIO_L_INPUT].isConnected(), inputs[M::AUDIO_L_INPUT].getVoltage(),
			inputs[M::AUDIO_R_INPUT].isConnected(), inputs[M::AUDIO_R_INPUT].getVoltage());
		return {in.l * 0.2f, in.r * 0.2f};
	}

	// Dry/wet to the Mix outputs. While the first pass records there is
	// nothing to play back yet, so the wet side monitors the input.
	void writeMix(loooop::StereoInput in, float wetL, float wetR) {
		if (loooop::monitorDryWhileEmpty(engine.isRecording(), engine.hasLoop())) {
			wetL = in.l;
			wetR = in.r;
		}
		float w = mixSmoother.process(loooop::normalizedControl(
			params[M::DRYWET_PARAM].getValue(), inputs[M::DRYWET_CV_INPUT].getVoltage()));
		outputs[M::MIX_L_OUTPUT].setVoltage(loooop::dryWet(in.l, wetL, w) * 5.f);
		outputs[M::MIX_R_OUTPUT].setVoltage(loooop::dryWet(in.r, wetR, w) * 5.f);
	}

	// The Record light, and the Overdub LED's state color (Lock pulses).
	void updateLights(int od, float sampleTime) {
		lights[M::RECORD_LIGHT].setBrightness(engine.isRecording() ? 1.f : 0.f);
		overdubPhase += sampleTime * 2.f;   // 2 Hz
		if (overdubPhase >= 1.f)
			overdubPhase -= 1.f;
		// A sine between ~0.1 (hue still readable) and 1.0.
		float gain = (od == 4) ? 0.55f + 0.45f * std::sin(2.f * float(M_PI) * overdubPhase) : 1.f;
		for (int i = 0; i < 3; i++)
			lights[M::OVERDUB_R_LIGHT + i].setBrightness(loooop::kOverdubColors[od][i] * gain);
	}
};

// Rack has no stock small snapping knob; the loopers use one for Grid.
struct RoundSmallBlackSnapKnob : RoundSmallBlackKnob {
	RoundSmallBlackSnapKnob() {
		snap = true;
	}
};

// Shows the Speed knob's notched value (loooop::applySpeedNotch). Display
// only: the stored value and the drag feel are unchanged.
struct SpeedParamQuantity : ParamQuantity {
	float getDisplayValue() override { return loooop::applySpeedNotch(getValue()); }
	void setDisplayValue(float v) override { setValue(v); }
};

// The Overdub button: an RGB LED bezel, latching, so a click steps the
// switch param through Layer/Decay/Add/Replace/Lock with wraparound.
struct OverdubButton : VCVLightBezel<RedGreenBlueLight> {
	OverdubButton() {
		momentary = false;
	}
};

// Submenu for a menu-only switch param, titled and labelled from its
// configSwitch, with undo.
inline MenuItem* createSwitchParamMenuItem(Module* module, int paramId) {
	SwitchQuantity* q = static_cast<SwitchQuantity*>(module->paramQuantities[paramId]);
	return createIndexSubmenuItem(q->name, q->labels,
		[=]() { return (int)std::round(q->getValue()); },
		[=](int i) {
			withMenuUndo(module, ("change " + string::lowercase(q->name)).c_str(),
				[=]() { q->setValue((float)i); });
		});
}

// Checkmark item for a menu-only on/off param, with undo.
inline MenuItem* createBoolParamMenuItem(const std::string& text, Module* module, int paramId) {
	ParamQuantity* q = module->paramQuantities[paramId];
	return createBoolMenuItem(text, "",
		[=]() { return q->getValue() > 0.5f; },
		[=](bool on) {
			withMenuUndo(module, ("change " + string::lowercase(q->name)).c_str(),
				[=]() { q->setValue(on ? 1.f : 0.f); });
		});
}

// The menu items both loopers start with.
template <class M>
void appendLooperMenu(Menu* menu, M* module) {
	menu->addChild(new MenuSeparator);
	menu->addChild(createSwitchParamMenuItem(module, M::REC_GATE_MODE_PARAM));
	menu->addChild(createSwitchParamMenuItem(module, M::TRIG_WHEN_REC_PARAM));
	// The stored value is inverted (0 = on), to match the MetaModule
	// alt-param, which zero-inits.
	ParamQuantity* crossfade = module->paramQuantities[M::CROSSFADE_PARAM];
	menu->addChild(createBoolMenuItem("Crossfade", "",
		[=]() { return crossfade->getValue() < 0.5f; },
		[=](bool on) {
			withMenuUndo(module, "change crossfade", [=]() { crossfade->setValue(on ? 0.f : 1.f); });
		}));
	menu->addChild(new MenuSeparator);
}
