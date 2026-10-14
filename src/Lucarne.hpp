#pragma once
#include "plugin.hpp"

// Lucarne: a display-only expander. Placed right of Particules or Retours,
// it draws that module's buffer and grains, or delay taps (see LucarneDisplay
// in Lucarne.cpp). Particules and Retours find it with dynamic_cast and turn
// on capture.
struct Lucarne : Module {
	// Retours' echo scope: true shows the whole buffer at a fixed scale;
	// false zooms to follow the taps. Slice always shows the whole buffer.
	bool scopeWholeBuffer = true;

	Lucarne() { config(0, 0, 0, 0); }

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "scopeWholeBuffer", json_boolean(scopeWholeBuffer));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		// Patches from before the option load with the default.
		json_t* j = json_object_get(rootJ, "scopeWholeBuffer");
		scopeWholeBuffer = j ? json_boolean_value(j) : true;
	}
};
