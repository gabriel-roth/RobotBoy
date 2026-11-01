#pragma once
#include "shapes/Prng.hpp"
#include <cmath>
#include <cstdint>

// What a CV trim does with nothing in its jack, turned counter-clockwise: a
// random offset chosen per cycle. The values come from the cycle count since
// reset, as Drift's do, so a Reset replays the same sequence.
namespace shapes {

// Around the knob, or only up from it (for controls whose knobs rest at the
// bottom of their range)
enum class TrimSpread { Bipolar, Upward };

// Uniform in [0, 1) for one control's stream and one cycle
inline double trimRandom(uint32_t stream, int64_t cycle) {
	return hash3Bits(0x7b1e5eedu, stream, (uint32_t) cycle) / 4294967296.0;
}

// The offset at t cycles since reset at full trim, as a fraction of the
// control's range: -0.5..0.5 (Bipolar) or 0..1 (Upward). Without glide each
// cycle holds its value; with glide the offset eases from one cycle's value
// to the next's across the cycle, so it never jumps.
inline double trimRandomOffset(uint32_t stream, double t, TrimSpread spread, bool glide) {
	if (!std::isfinite(t))
		t = 0.0;
	double cycle = std::floor(t);
	double u = trimRandom(stream, (int64_t) cycle);
	if (glide) {
		double next = trimRandom(stream, (int64_t) cycle + 1);
		double ease = (1.0 - std::cos(3.14159265358979323846 * (t - cycle))) / 2.0;
		u += (next - u) * ease;
	}
	return spread == TrimSpread::Upward ? u : u - 0.5;
}

}  // namespace shapes
