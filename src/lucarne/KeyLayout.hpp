#pragma once
// The VCV panel key's colored shapes, painted in the active color scheme by
// LucarneKey (src/Lucarne.cpp). Pure C++ (no Rack).
//
// Positions are panel mm, copied from the generated panels: kParticulesKey
// from res/Lucarne.svg (panel-specs/lucarne.yaml; glyphs/lucarne/pan.svg and
// pitch.svg), kRetoursKey from res/Lucarne-Retours.svg
// (panel-specs/lucarne-retours.yaml; glyphs/lucarne/tap-markers.svg). The
// glyphs keep these shapes as invisible placeholders (fill-opacity="0",
// lines stroke-opacity="0") in today's Lucarne colors;
// tests/test_lucarne_key.py fails if the two drift apart. After moving a
// glyph, regenerate the panel and copy the placeholders' new coordinates here.

#include "ColorScheme.hpp"

namespace lucarne {

enum class KeyRole { Wave, PanL, PanLC, PanC, PanCR, PanR, LineA, LineB, LineDry, BarA, BarB, BarDry };

// stroke == 0: a rect with corners (x1, y1) and (x2, y2). Otherwise a
// round-capped line from (x1, y1) to (x2, y2), stroke wide.
struct KeyShape {
	KeyRole role;
	float x1, y1, x2, y2;
	float stroke;
};

// In SVG paint order.
inline constexpr KeyShape kParticulesKey[] = {
	{KeyRole::PanL, 38.725f, 86.3f, 41.2688f, 88.5f, 0.f},
	{KeyRole::PanLC, 41.5438f, 86.3f, 44.0875f, 88.5f, 0.f},
	{KeyRole::PanC, 44.3625f, 86.3f, 46.9062f, 88.5f, 0.f},
	{KeyRole::PanCR, 47.1812f, 86.3f, 49.725f, 88.5f, 0.f},
	{KeyRole::PanR, 50.f, 86.3f, 52.5438f, 88.5f, 0.f},
	{KeyRole::Wave, 13.1f, 104.95f, 24.1f, 106.05f, 0.f},
};

// Waveform bars, then each marker line with its flash bar.
inline constexpr KeyShape kRetoursKey[] = {
	{KeyRole::Wave, 8.26f, 92.413f, 8.948f, 101.437f, 0.f},
	{KeyRole::Wave, 9.291f, 91.5f, 9.978f, 102.35f, 0.f},
	{KeyRole::Wave, 10.322f, 93.221f, 11.01f, 100.629f, 0.f},
	{KeyRole::Wave, 11.353f, 91.418f, 12.041f, 102.432f, 0.f},
	{KeyRole::Wave, 12.384f, 90.124f, 13.071f, 103.726f, 0.f},
	{KeyRole::Wave, 13.415f, 91.873f, 14.103f, 101.977f, 0.f},
	{KeyRole::Wave, 14.446f, 94.877f, 15.134f, 98.973f, 0.f},
	{KeyRole::Wave, 15.477f, 93.575f, 16.164f, 100.275f, 0.f},
	{KeyRole::Wave, 16.508f, 94.263f, 17.196f, 99.587f, 0.f},
	{KeyRole::Wave, 17.539f, 95.588f, 18.227f, 98.262f, 0.f},
	{KeyRole::Wave, 18.57f, 94.084f, 19.257f, 99.766f, 0.f},
	{KeyRole::Wave, 19.601f, 90.505f, 20.289f, 103.345f, 0.f},
	{KeyRole::Wave, 20.632f, 90.299f, 21.32f, 103.551f, 0.f},
	{KeyRole::Wave, 21.663f, 92.868f, 22.35f, 100.982f, 0.f},
	{KeyRole::Wave, 22.694f, 92.137f, 23.382f, 101.713f, 0.f},
	{KeyRole::Wave, 23.725f, 91.169f, 24.412f, 102.681f, 0.f},
	{KeyRole::Wave, 24.756f, 94.741f, 25.443f, 99.109f, 0.f},
	{KeyRole::Wave, 25.787f, 95.588f, 26.474f, 98.262f, 0.f},
	{KeyRole::Wave, 26.818f, 94.512f, 27.506f, 99.338f, 0.f},
	{KeyRole::Wave, 27.849f, 93.936f, 28.536f, 99.914f, 0.f},
	{KeyRole::Wave, 28.88f, 94.512f, 29.567f, 99.338f, 0.f},
	{KeyRole::Wave, 29.911f, 94.593f, 30.599f, 99.257f, 0.f},
	{KeyRole::Wave, 30.942f, 94.982f, 31.629f, 98.868f, 0.f},
	{KeyRole::Wave, 31.973f, 95.37f, 32.66f, 98.48f, 0.f},
	{KeyRole::Wave, 33.004f, 94.787f, 33.692f, 99.063f, 0.f},
	{KeyRole::Wave, 34.035f, 91.507f, 34.722f, 102.343f, 0.f},
	{KeyRole::Wave, 35.066f, 91.154f, 35.753f, 102.696f, 0.f},
	{KeyRole::Wave, 36.097f, 95.402f, 36.784f, 98.448f, 0.f},
	{KeyRole::Wave, 37.128f, 95.588f, 37.815f, 98.262f, 0.f},
	{KeyRole::Wave, 38.159f, 93.746f, 38.846f, 100.104f, 0.f},
	{KeyRole::Wave, 39.19f, 93.703f, 39.877f, 100.147f, 0.f},
	{KeyRole::Wave, 40.221f, 94.519f, 40.908f, 99.331f, 0.f},
	{KeyRole::Wave, 41.252f, 92.755f, 41.939f, 101.095f, 0.f},
	{KeyRole::Wave, 42.283f, 89.914f, 42.97f, 103.936f, 0.f},
	{KeyRole::Wave, 43.314f, 90.816f, 44.001f, 103.034f, 0.f},
	{KeyRole::Wave, 44.345f, 93.054f, 45.032f, 100.796f, 0.f},
	{KeyRole::Wave, 45.376f, 92.837f, 46.064f, 101.013f, 0.f},
	{KeyRole::Wave, 46.407f, 93.622f, 47.094f, 100.228f, 0.f},
	{KeyRole::Wave, 47.438f, 93.233f, 48.125f, 100.617f, 0.f},
	{KeyRole::Wave, 48.469f, 92.844f, 49.156f, 101.006f, 0.f},
	{KeyRole::LineA, 12.728f, 84.25f, 12.728f, 109.6f, 0.3f},
	{KeyRole::BarA, 12.384f, 90.124f, 13.071f, 103.726f, 0.f},
	{KeyRole::LineB, 32.317f, 84.25f, 32.317f, 109.6f, 0.3f},
	{KeyRole::BarB, 31.973f, 95.37f, 32.66f, 98.48f, 0.f},
	{KeyRole::LineDry, 49.84f, 84.25f, 49.84f, 109.6f, 0.3f},
	{KeyRole::BarDry, 49.496f, 92.844f, 50.184f, 101.006f, 0.f},
};

inline constexpr bool isPanChip(KeyRole role) {
	return role == KeyRole::PanL || role == KeyRole::PanLC || role == KeyRole::PanC ||
	       role == KeyRole::PanCR || role == KeyRole::PanR;
}

// Pan chips: the three pan colors with a half mix between each pair. Flash
// bars: the tap or dry color a tenth of the way to hi.
inline Rgb keyColor(KeyRole role, const ColorScheme& s) {
	switch (role) {
		case KeyRole::Wave:    return s.keyWave;
		case KeyRole::PanL:    return s.panL;
		case KeyRole::PanLC:   return mix(s.panC, s.panL, 0.5f);
		case KeyRole::PanC:    return s.panC;
		case KeyRole::PanCR:   return mix(s.panC, s.panR, 0.5f);
		case KeyRole::PanR:    return s.panR;
		case KeyRole::LineA:   return s.keyLineA;
		case KeyRole::LineB:   return s.keyLineB;
		case KeyRole::LineDry: return s.keyLineDry;
		case KeyRole::BarA:    return mix(s.tapA, s.hi, 0.1f);
		case KeyRole::BarB:    return mix(s.tapB, s.hi, 0.1f);
		case KeyRole::BarDry:  return mix(s.dry, s.hi, 0.1f);
	}
	return s.keyWave;
}

}  // namespace lucarne
