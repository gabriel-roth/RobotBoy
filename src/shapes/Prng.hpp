#pragma once
#include <cmath>
#include <cstdint>

// Hashes and random streams with exact JavaScript 32-bit integer semantics
// (Math.imul, >>>, |0), so seeds give the same shapes as the original engine.
namespace shapes {

// JavaScript ToInt32 (x | 0), returned as its 32-bit pattern.
inline uint32_t toInt32(double x) {
	if (!std::isfinite(x))
		return 0;
	double t = std::trunc(x);
	double m = std::fmod(t, 4294967296.0);
	if (m < 0.0)
		m += 4294967296.0;
	return (uint32_t) m;
}

inline uint32_t hash3Bits(uint32_t a, uint32_t b, uint32_t c) {
	uint32_t h = (a * 0x27d4eb2du) ^ ((b + 0x3c6ef372u) * 0x165667b1u) ^ ((c + 0x7f4a7c15u) * 0x9e3779b1u);
	h ^= h >> 15;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return h;
}

// Uniform in [0, 1) from three integers.
inline double hash3(double a, double b, double c) {
	return hash3Bits(toInt32(a), toInt32(b), toInt32(c)) / 4294967296.0;
}

struct Mulberry32 {
	uint32_t a;

	double next() {
		a += 0x6D2B79F5u;
		uint32_t t = (a ^ (a >> 15)) * (1u | a);
		t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
		return (t ^ (t >> 14)) / 4294967296.0;
	}
};

inline Mulberry32 seedRng(int seed, int salt) {
	uint32_t s = ((uint32_t) seed + 1u) * 2654435761u ^ ((uint32_t) salt + 1u) * 0x5bd1e995u;
	return Mulberry32{s};
}

}  // namespace shapes
