#pragma once
#include "plugin.hpp"

// Lucarne: a display-only expander. Placed right of Particules, it draws
// Particules' buffer and grains (see LucarneDisplay in Lucarne.cpp).
// Particules finds it with dynamic_cast and turns on grain capture.
struct Lucarne : Module {
	Lucarne() { config(0, 0, 0, 0); }
};
