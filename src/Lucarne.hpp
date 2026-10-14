#pragma once
#include "plugin.hpp"

// Lucarne: a display-only expander. Placed right of Particules or Retours,
// it draws that module's buffer and grains, or delay taps (see LucarneDisplay
// in Lucarne.cpp). Particules and Retours find it with dynamic_cast and turn
// on capture.
struct Lucarne : Module {
	Lucarne() { config(0, 0, 0, 0); }
};
