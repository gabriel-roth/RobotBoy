// TrimMod tests: ranges, repeatability, jumps and glides.
#include "shapes/TrimMod.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace shapes;

int main() {
	// Ranges, and both halves of the bipolar range get used
	bool below = false, above = false;
	for (int c = 0; c < 2000; c++) {
		double b = trimRandomOffset(3, c + 0.25, TrimSpread::Bipolar, false);
		double u = trimRandomOffset(3, c + 0.25, TrimSpread::Upward, false);
		assert(b >= -0.5 && b < 0.5);
		assert(u >= 0.0 && u < 1.0);
		below |= b < -0.4;
		above |= b > 0.4;
	}
	assert(below && above);

	// The same stream and cycle give the same value; streams differ
	assert(trimRandom(1, 7) == trimRandom(1, 7));
	int same = 0;
	for (int c = 0; c < 100; c++)
		same += trimRandom(1, c) == trimRandom(2, c);
	assert(same == 0);

	// Without glide a cycle holds one value, and changes at the next cycle
	double a0 = trimRandomOffset(5, 4.0, TrimSpread::Bipolar, false);
	assert(trimRandomOffset(5, 4.999, TrimSpread::Bipolar, false) == a0);
	assert(trimRandomOffset(5, 5.0, TrimSpread::Bipolar, false) != a0);

	// With glide it starts at the cycle's value, ends at the next one's, and
	// is continuous across the cycle start
	for (int c = 0; c < 50; c++) {
		double start = trimRandomOffset(9, c, TrimSpread::Upward, true);
		assert(std::fabs(start - trimRandom(9, c)) < 1e-12);
		double before = trimRandomOffset(9, c + 1 - 1e-9, TrimSpread::Upward, true);
		double after = trimRandomOffset(9, c + 1, TrimSpread::Upward, true);
		assert(std::fabs(before - after) < 1e-6);
		double mid = trimRandomOffset(9, c + 0.5, TrimSpread::Upward, true);
		assert(std::fabs(mid - (trimRandom(9, c) + trimRandom(9, c + 1)) / 2) < 1e-12);
	}

	// Garbage time doesn't escape the range
	double n = trimRandomOffset(1, NAN, TrimSpread::Bipolar, true);
	assert(n >= -0.5 && n < 0.5);

	std::printf("test_trim_mod: ok\n");
	return 0;
}
