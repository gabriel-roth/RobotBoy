#include <type_traits>
// Sanity checks on the generated playground reference vectors.
#include "reference.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

template <typename T, size_t N>
static void checkFinite(const T (&a)[N]) {
	if constexpr (std::is_array_v<T>) {
		for (size_t i = 0; i < N; i++)
			checkFinite(a[i]);
	}
	else {
		for (size_t i = 0; i < N; i++)
			assert(std::isfinite(a[i]));
	}
}

int main() {
	checkFinite(ref::hash3Cases);
	checkFinite(ref::mulberryFirst);
	checkFinite(ref::recipeSeed);
	checkFinite(ref::recipeVals);
	checkFinite(ref::recipeNorm);
	checkFinite(ref::recipePeak);
	checkFinite(ref::driftNorm);
	checkFinite(ref::transformCases);
	checkFinite(ref::transformRaw);
	checkFinite(ref::waveRaw);
	checkFinite(ref::presetValues);
	checkFinite(ref::presetRatios);
	checkFinite(ref::presetRaw);
	checkFinite(ref::presetParts);
	checkFinite(ref::treatIn);
	checkFinite(ref::treatOut);

	assert(sizeof(ref::recipeIds) / sizeof(ref::recipeIds[0]) == 18);
	assert(std::strcmp(ref::recipeIds[17], "glitch") == 0);
	assert(std::strcmp(ref::presetNames[6], "Struck ring") == 0);

	for (int r = 0; r < 18; r++) {
		double mx = 0;
		for (double v : ref::recipeNorm[r])
			mx = std::fmax(mx, std::fabs(v));
		// The 65-point row may miss the true peak; recipePeak is the 512-point one.
		assert(mx >= 0.85 && mx <= 1.0001);
		assert(ref::recipePeak[r] >= 0.999 && ref::recipePeak[r] <= 1.0001);
	}
	std::printf("shapes reference sanity: PASS\n");
	return 0;
}
