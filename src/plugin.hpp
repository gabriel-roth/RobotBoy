#pragma once
#include <rack.hpp>
#include <cmath>
#include "loooop/LooperModuleDSP.hpp"

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

// Keeps the tooltip in sync with the engine's speed-knob notch (see
// loooop::applySpeedNotch): display-only, doesn't change the stored
// value or the knob's drag feel.
struct SpeedParamQuantity : ParamQuantity {
	float getDisplayValue() override { return loooop::applySpeedNotch(getValue()); }
	void setDisplayValue(float v) override { setValue(v); }
};

// Wraps a context-menu change in a whole-module undo step: the snapshot is the
// module's full JSON (params and data), so one helper covers every menu
// setting. MetaModule has no undo, so there it just makes the change.
template <typename F>
void withMenuUndo(Module* module, const char* name, F&& change) {
#ifdef METAMODULE
	change();
#else
	json_t* oldModuleJ = APP->engine->moduleToJson(module);
	change();
	history::ModuleChange* h = new history::ModuleChange;
	h->name = name;
	h->moduleId = module->id;
	h->oldModuleJ = oldModuleJ;
	h->newModuleJ = APP->engine->moduleToJson(module);
	APP->history->push(h);
#endif
}
