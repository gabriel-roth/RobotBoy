#pragma once
#include "plugin.hpp"

struct BeadsModule;

// Lucarne: a display-only expander. Placed next to Particules or Retours, on
// either side, it draws that module's buffer and grains, or delay taps (see
// LucarneDisplay in Lucarne.cpp). Particules and Retours find it with
// dynamic_cast and turn on capture while it shows them.
extern int lucarneDefaultScheme;   // VCV: the "Make default" scheme, kept in settings.json

struct Lucarne : Module {
	int colorScheme;   // index into lucarne::kSchemes; saved with the patch
	Lucarne() {
		config(0, 0, 0, 0);
		colorScheme = lucarneDefaultScheme;
	}
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;
#ifndef METAMODULE
	// Rack calls dataFromJson only when the patch has a "data" object, so a
	// Lucarne saved before this setting existed would keep the user's default
	// scheme. A patch without a scheme always means the first one.
	void fromJson(json_t* rootJ) override {
		colorScheme = 0;
		Module::fromJson(rootJ);
	}
#endif
	// The module it shows: Particules or Retours on the left, else on the
	// right, else null. Called from the audio and UI threads.
	BeadsModule* source() const;
};
