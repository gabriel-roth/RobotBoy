#pragma once
#include "Combine.hpp"
#include <cmath>

// Weaver's factory presets. Preset 1 is also the module's default state.
namespace shapes {

struct WeaverPreset {
	const char* name;
	CombineControls controls;
};

constexpr int kNumWeaverPresets = 15;

namespace detail {
// Every preset has phase 0 and neutral Input settings
constexpr OperatorControls op(Wave w, int ratio, double param, double level) {
	return OperatorControls{w, ratio, param, 0.0, level, 0.0, 1.0};
}
}  // namespace detail

// Ratio indices: 3 x1, 4 x2, 5 x3, 6 x4, 8 x6, 10 x8, 11 x12, 12 x16, 13 x24
inline constexpr WeaverPreset kWeaverPresets[kNumWeaverPresets] = {
	{"Envelope + triangle x16", {{detail::op(Envelope, 3, 0.12, 1), detail::op(Triangle, 12, 0.5, 0.3), detail::op(Square, 6, 0.5, 0)}, Mix, 0.0}},
	{"Ramp switched by square", {{detail::op(RampUp, 3, 0.5, 1), detail::op(Square, 10, 0.5, 1), detail::op(Sine, 3, 0.5, 0)}, Scale, 0.0}},
	{"Swell with gated ripple", {{detail::op(Triangle, 12, 0.5, 0.45), detail::op(Square, 6, 0.35, 1), detail::op(Envelope, 3, 0.7, 0.8)}, Scale, 0.0}},
	{"Phase-bent sine", {{detail::op(Sine, 3, 0.5, 1), detail::op(Triangle, 5, 0.5, 0.8), detail::op(SmoothRandom, 4, 0.4, 0.15)}, Bend, 0.0}},
	{"Max with random steps", {{detail::op(Sine, 3, 0.3, 1), detail::op(Triangle, 5, 0.8, 0.8), detail::op(SteppedRandom, 4, 0.5, 0.25)}, Max, 0.0}},
	{"Sampled ramp in a window", {{detail::op(RampDown, 5, 0.3, 1), detail::op(Square, 11, 0.5, 1), detail::op(Sine, 3, 0.5, 0)}, SampleHold, 0.7}},
	{"Struck ring", {{detail::op(Envelope, 3, 0.05, 1), detail::op(Sine, 10, 0.5, 1), detail::op(Sine, 3, 0.5, 0)}, Ring, 0.0}},
	{"Grouped swells, rising", {{detail::op(Sine, 11, 0.5, 1), detail::op(Triangle, 5, 0.5, 1), detail::op(RampUp, 3, 0.5, 0.8)}, Scale2, 0.0}},
	{"Random accents, fading", {{detail::op(Sine, 12, 0.5, 1), detail::op(SteppedRandom, 3, 0.6, 1), detail::op(Envelope, 3, 0.02, 0.75)}, Scale2, 0.0}},
	{"Bitten sine", {{detail::op(Sine, 3, 0.5, 1), detail::op(RampDown, 8, 0.5, 0.7), detail::op(Sine, 3, 0.5, 0)}, Min, 0.0}},
	{"Square to sine and back", {{detail::op(Square, 8, 0.5, 1), detail::op(Sine, 3, 0.5, 1), detail::op(Triangle, 3, 0.5, 1)}, Xfade, 0.0}},
	{"Folded rising ramp", {{detail::op(RampUp, 3, 0.5, 1), detail::op(Sine, 5, 0.5, 1), detail::op(Sine, 3, 0.5, 0)}, Fold, 0.0}},
	{"Random stutter", {{detail::op(Triangle, 13, 0.5, 1), detail::op(SteppedRandom, 4, 0.6, 1), detail::op(Sine, 3, 0.5, 0)}, Gate, 0.0}},
	{"Breathing triangle", {{detail::op(Triangle, 6, 0.5, 1), detail::op(Sine, 3, 0.5, 0.8), detail::op(Sine, 3, 0.5, 0)}, Bend, 0.0}},
	{"Random swell", {{detail::op(SmoothRandom, 6, 0.6, 1), detail::op(SteppedRandom, 4, 0.5, 0.5), detail::op(Sine, 12, 0.5, 0.2)}, Mix, 1.0}},
};

// Index of the preset these settings match, or -1. Input-only fields are ignored.
inline int presetMatches(const CombineControls& c) {
	constexpr double eps = 1e-4;
	for (int n = 0; n < kNumWeaverPresets; n++) {
		const CombineControls& p = kWeaverPresets[n].controls;
		bool same = p.algorithm == c.algorithm && std::fabs(p.window - c.window) <= eps;
		for (int i = 0; i < 3 && same; i++) {
			const OperatorControls& a = p.op[i];
			const OperatorControls& b = c.op[i];
			same = a.wave == b.wave && a.ratio == b.ratio && std::fabs(a.param - b.param) <= eps &&
			       std::fabs(a.phase - b.phase) <= eps && std::fabs(a.level - b.level) <= eps;
		}
		if (same)
			return n;
	}
	return -1;
}

}  // namespace shapes
