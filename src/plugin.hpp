#pragma once
#include <rack.hpp>
#include <cmath>
#include "host/controls.h"
#include "loooop/LooperModuleDSP.hpp"
#include "classic_mode.h"
#include "particules/pitch_notch_map.hpp"

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelLoooop;
extern Model* modelLop;
extern Model* modelMF20Filter;
extern Model* modelOnbetap;
extern Model* modelParticules;
extern Model* modelOndes;
extern Model* modelVespid;
extern Model* modelRetours;
extern Model* modelLucarne;

// Rack has no stock small snap knob; Loooop and Löp both use it for Grid.
struct RoundSmallBlackSnapKnob : RoundSmallBlackKnob {
	RoundSmallBlackSnapKnob() {
		snap = true;
	}
};

// Pitch knob of Particules and Retours, shown in semitones: Beads' notched
// +-24 semitone law (robotboy_beads::DetuneLaw), or in classic mode the v2.1
// notch map (pitch_notch_map.hpp).
struct PitchParamQuantity : ParamQuantity {
	bool classic() {
		auto* s = dynamic_cast<robotboy::ClassicModeSource*>(module);
		return s && s->classicMode();
	}
	float getDisplayValue() override {
		return classic() ? pitchKnobToSemitones(getValue()) : robotboy_beads::DetuneLaw(getValue());
	}
	void setDisplayValue(float semitones) override {
		setValue(classic() ? semitonesToPitchKnob(semitones) : robotboy_beads::InverseDetuneLaw(semitones));
	}
	std::string getDisplayValueString() override {
		float st = getDisplayValue();
		if (std::fabs(st - std::round(st)) < 0.05f) return string::f("%d", (int)std::round(st));
		return string::f("%.1f", st);
	}
	std::string getUnit() override { return " st"; }
};

// Keeps the tooltip in sync with the engine's speed-knob notch (see
// loooop::applySpeedNotch): display-only, doesn't change the stored
// value or the knob's drag feel.
struct SpeedParamQuantity : ParamQuantity {
	float getDisplayValue() override { return loooop::applySpeedNotch(getValue()); }
	void setDisplayValue(float v) override { setValue(v); }
};

#include "particules/quality_colors.h"
