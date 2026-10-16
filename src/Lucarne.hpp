#pragma once
#include "plugin.hpp"

struct BeadsModule;

// Lucarne: a display-only expander. Placed next to Particules or Retours, on
// either side, it draws that module's buffer and grains, or delay taps (see
// LucarneDisplay in Lucarne.cpp). Particules and Retours find it with
// dynamic_cast and turn on capture while it shows them.
struct Lucarne : Module {
#ifndef METAMODULE
	// TEMPORARY zoom-tuning knobs (VCV only), over the Retours key: one per
	// lucarne::ZoomTuning field. Remove once the defaults are chosen.
	enum ParamId {
		ZOOM_GAP_PARAM, ZOOM_FAR_PARAM, ZOOM_BAND_PARAM, ZOOM_DEADBAND_PARAM,
		ZOOM_HOLD_PARAM, ZOOM_RELEASE_PARAM, ZOOM_KNOB_GLIDE_PARAM, ZOOM_MOD_GLIDE_PARAM,
		NUM_PARAMS
	};
	Lucarne();
#else
	Lucarne() { config(0, 0, 0, 0); }
#endif
	// The module it shows: Particules or Retours on the left, else on the
	// right, else null. Called from the audio and UI threads.
	BeadsModule* source() const;
};
