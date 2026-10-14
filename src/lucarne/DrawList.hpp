#pragma once
// What Lucarne's layouts produce and Lucarne.cpp paints: colored rectangles
// in the display widget's box units. Pure C++ (no Rack, no nanovg).

#include <cstdint>
#include <vector>

namespace lucarne {

struct Rgb { uint8_t r, g, b; };
struct Rect { float x, y, w, h; Rgb color; };

struct DrawList {
	Rgb background{0x10, 0x14, 0x18};   // Loooop's display background
	Rgb wave_color{0x51, 0x66, 0x7A};   // Loooop's waveform blue-gray
	std::vector<Rect> under;            // behind the waveform, each its own color (ScopeLayout only)
	std::vector<Rect> wave;             // one per peak column, all wave_color
	std::vector<Rect> marks;            // write head, grain lines, cursors (in paint order)

	void Clear() {
		under.clear();
		wave.clear();
		marks.clear();
	}
};

inline Rgb Lighten(Rgb c, float t) {   // t of the way to white
	return {uint8_t(c.r + (255 - c.r) * t + 0.5f), uint8_t(c.g + (255 - c.g) * t + 0.5f),
	        uint8_t(c.b + (255 - c.b) * t + 0.5f)};
}
inline Rgb Mix(Rgb a, Rgb b, float t) {
	return {uint8_t(a.r + (b.r - a.r) * t + 0.5f), uint8_t(a.g + (b.g - a.g) * t + 0.5f),
	        uint8_t(a.b + (b.b - a.b) * t + 0.5f)};
}

}  // namespace lucarne
