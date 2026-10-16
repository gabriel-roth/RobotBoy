#pragma once
#include "plugin.hpp"

struct BeadsModule;

// Lucarne: a display-only expander. Placed next to Particules or Retours, on
// either side, it draws that module's buffer and grains, or delay taps (see
// LucarneDisplay in Lucarne.cpp). Particules and Retours find it with
// dynamic_cast and turn on capture while it shows them.
struct Lucarne : Module {
	Lucarne() { config(0, 0, 0, 0); }
	// The module it shows: Particules or Retours on the left, else on the
	// right, else null. Called from the audio and UI threads.
	BeadsModule* source() const;
};
