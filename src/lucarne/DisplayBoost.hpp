#pragma once
// MetaModule's small LCD flattens Lucarne's colors, so on MetaModule every
// color goes through this correction before it's drawn: a contrast curve on
// its brightness (luma), then a saturation boost. Both keep the hue exactly
// and stop where a channel would clip. VCV draws the schemes as they are.
// Pure C++ (no Rack).

#include "DrawList.hpp"

#include <algorithm>
#include <cmath>

namespace lucarne {

class DisplayBoost {
public:
	static constexpr int kLevels = 5;   // 0 = off
	// Luma (0-1) the contrast curve turns on: just above the dark schemes'
	// backgrounds, so they sink and their waveforms and marks rise.
	static constexpr float kPivot = 0.1f;

	explicit DisplayBoost(int level = 0) { setLevel(level); }

	int level() const { return level_; }

	void setLevel(int level) {
		level_ = std::min(std::max(level, 0), kLevels - 1);
		const float t = 0.15f * float(level_);
		// Contrast: slope 1 + t at the pivot, black and white stay put.
		// Darks below the pivot sink, everything above it rises.
		const float k = 1.f + t;
		for (int i = 0; i < 256; ++i) {
			const float v = float(i) / 255.f;
			const float out = v < kPivot
				? kPivot * std::pow(v / kPivot, k)
				: kPivot + (1.f - kPivot) * (1.f - std::pow(1.f - (v - kPivot) / (1.f - kPivot), k));
			curve_[i] = out * 255.f;
		}
		saturation_ = 1.f + 1.25f * t;
	}

	Rgb apply(Rgb c) const {
		if (level_ == 0) return c;
		float ch[3] = {float(c.r), float(c.g), float(c.b)};
		// Scale the whole color to the curve's luma, no further than its
		// brightest channel allows.
		const float y0 = luma(ch);
		if (y0 > 0.f) {
			const int i = std::min(int(y0), 254);
			const float target = curve_[i] + (curve_[i + 1] - curve_[i]) * (y0 - float(i));
			const float mx = std::max({ch[0], ch[1], ch[2]});
			const float gain = std::min(target / y0, 255.f / mx);
			for (float& v : ch) v *= gain;
		}
		// Push each channel away from the luma, scaled down if needed so no
		// channel clips.
		const float y = luma(ch);
		float s = saturation_;
		for (float v : ch) {
			if (v > y + 1e-3f) s = std::min(s, (255.f - y) / (v - y));
			else if (v < y - 1e-3f) s = std::min(s, y / (y - v));
		}
		s = std::max(s, 1.f);
		for (float& v : ch) v = std::min(std::max(y + (v - y) * s + 0.5f, 0.f), 255.f);
		return {uint8_t(ch[0]), uint8_t(ch[1]), uint8_t(ch[2])};
	}

private:
	static float luma(const float* ch) { return 0.2126f * ch[0] + 0.7152f * ch[1] + 0.0722f * ch[2]; }
	int level_ = 0;
	float curve_[256] = {};
	float saturation_ = 1.f;
};

}  // namespace lucarne
