#pragma once
// Color helpers shared by Lucarne's layouts.

#include <cstdint>

#include "GrainLayout.hpp"

namespace lucarne {

inline Rgb Lighten(Rgb c, float t) {   // t of the way to white
	return {uint8_t(c.r + (255 - c.r) * t + 0.5f), uint8_t(c.g + (255 - c.g) * t + 0.5f),
	        uint8_t(c.b + (255 - c.b) * t + 0.5f)};
}
inline Rgb Mix(Rgb a, Rgb b, float t) {
	return {uint8_t(a.r + (b.r - a.r) * t + 0.5f), uint8_t(a.g + (b.g - a.g) * t + 0.5f),
	        uint8_t(a.b + (b.b - a.b) * t + 0.5f)};
}

}  // namespace lucarne
