#pragma once
#include <rack.hpp>
#include <cmath>
#include "particules/pitch_notch_map.hpp"

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelLoooop;
extern Model* modelLop;
extern Model* modelMF20Filter;
extern Model* modelParticules;

// --- Shared helpers pulled from particules/plugin.hpp (needed by Particules.cpp) ---
struct PitchParamQuantity : ParamQuantity {
	float getDisplayValue() override { return pitchKnobToSemitones(getValue()); }
	void setDisplayValue(float semitones) override { setValue(semitonesToPitchKnob(semitones)); }
	std::string getDisplayValueString() override {
		float st = getDisplayValue();
		if (std::fabs(st - std::round(st)) < 0.05f) return string::f("%d", (int)std::round(st));
		return string::f("%.1f", st);
	}
	std::string getUnit() override { return " st"; }
};

static constexpr float kQualityColors[4][3] = {
	{1.f, 1.f, 1.f}, {0.f, 1.f, 1.f}, {1.f, 0.5f, 0.f}, {1.f, 0.f, 1.f},
};
