#pragma once
#include "plugin.hpp"

struct BeadsModule;

// Lucarne: a display-only expander. Placed next to Particules or Retours, on
// either side, it draws that module's buffer and grains, or delay taps (see
// LucarneDisplay in Lucarne.cpp). Particules and Retours find it with
// dynamic_cast and turn on capture while it shows them.
// Each Lucarne keeps one color scheme per module it can show.
enum LucarneSlot { kSlotParticules = 0, kSlotRetours = 1, kNumSlots = 2 };
extern int lucarneDefaultScheme[kNumSlots];   // VCV: the "Make default" schemes, kept in settings.json

struct Lucarne : Module {
	int colorScheme[kNumSlots];   // indexes into lucarne::kSchemes; saved with the patch
	Lucarne() {
		config(0, 0, 0, 0);
		for (int i = 0; i < kNumSlots; i++) colorScheme[i] = lucarneDefaultScheme[i];
	}
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;
#ifndef METAMODULE
	// Rack calls dataFromJson only when the patch has a "data" object, so a
	// Lucarne saved before this setting existed would keep the user's default
	// scheme. A patch without a scheme always means the first one.
	void fromJson(json_t* rootJ) override {
		for (int i = 0; i < kNumSlots; i++) colorScheme[i] = 0;
		Module::fromJson(rootJ);
	}
#endif
	// The module it shows: Particules or Retours on the left, else on the
	// right, else null. Called from the audio and UI threads.
	BeadsModule* source() const;
};
