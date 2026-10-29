// Recipes engine tests: PRNG, settings, normalized shapes and drift against
// reference vectors generated from the original JS engine.
#include "reference.hpp"
#include "shapes/Prng.hpp"
#include "shapes/Recipes.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace shapes;

static_assert(std::is_trivially_copyable_v<RecipeInstance>);

static int failures = 0;

static void expectNear(double got, double want, double tol, const char* what, int a, int b, int c) {
	if (!(std::fabs(got - want) <= tol)) {
		if (failures < 20)
			std::printf("  %s [%d][%d][%d]: got %.17g want %.17g\n", what, a, b, c, got, want);
		failures++;
	}
}

static void prngMatches() {
	for (int n = 0; n < 20; n++) {
		const double* r = ref::hash3Cases[n];
		expectNear(hash3(r[0], r[1], r[2]), r[3], 1e-12, "hash3", n, 0, 0);
	}
	const int seeds[5][2] = {{0, 10}, {0, 11}, {140, 10}, {72, 11}, {11, 10}};
	for (int n = 0; n < 5; n++) {
		Mulberry32 g = seedRng(seeds[n][0], seeds[n][1]);
		for (int k = 0; k < 8; k++)
			expectNear(g.next(), ref::mulberryFirst[n][k], 1e-12, "mulberry", n, k, 0);
	}
}

static void namesMatch() {
	assert(kNumRecipes == 18);
	assert(std::strcmp(recipeName(0), "Bounce") == 0);
	assert(std::strcmp(recipeName(14), "Devil's staircase") == 0);
	assert(std::strcmp(recipeFamily(17), "Chaos") == 0);
	assert(std::strcmp(recipeFamily(7), "Cycles") == 0);
}

static void valsMatch() {
	const double vs[3] = {0.0, 0.5, 1.0};
	for (int r = 0; r < kNumRecipes; r++) {
		for (int j = 0; j < 3; j++) {
			double out[8];
			recipeVals(r, vs[j], 0.0, 0, out);
			for (int i = 0; i < 8; i++)
				expectNear(out[i], ref::recipeVals[r][j][i], 1e-9, "vals", r, j, i);
		}
	}
}

static void normMatches() {
	for (int r = 0; r < kNumRecipes; r++) {
		RecipeInstance inst;
		buildRecipe(inst, r, 0.5, 0.0, 0);
		for (int k = 0; k <= 64; k++)
			expectNear(inst.eval(k / 64.0), ref::recipeNorm[r][k], 1e-6, "norm", r, k, 0);
		// The 512-point peak is what eval divides by, so the normalized peak is 1.
		double pk = 0.0;
		for (int k = 0; k < 512; k++)
			pk = std::fmax(pk, std::fabs(inst.eval(k / 512.0)));
		expectNear(pk, ref::recipePeak[r], 1e-6, "peak", r, 0, 0);
	}
}

static void driftMatches() {
	for (int c = 0; c < 4; c++) {
		RecipeInstance inst;
		buildRecipe(inst, 15, 0.5, 0.7, c);
		for (int k = 0; k <= 32; k++)
			expectNear(inst.eval(k / 32.0), ref::driftNorm[c][k], 1e-6, "drift", c, k, 0);
	}
}

static void outputsFinite() {
	const double vs[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
	for (int r = 0; r < kNumRecipes; r++) {
		for (double v : vs) {
			RecipeInstance inst;
			buildRecipe(inst, r, v, 1.0, 12345);
			for (int k = 0; k <= 1024; k++) {
				double y = inst.eval(k / 1024.0);
				if (!std::isfinite(y)) {
					if (failures < 20)
						std::printf("  finite: recipe %d v %g k %d -> %g\n", r, v, k, y);
					failures++;
				}
			}
			// Out-of-range and non-finite phases must not index outside the tables.
			assert(std::isfinite(inst.eval(-0.25)));
			assert(std::isfinite(inst.eval(1.5)));
			assert(std::isfinite(inst.eval(NAN)));
		}
	}
}

static void instanceIsTriviallyCopyable() {
	RecipeInstance a;
	buildRecipe(a, 10, 0.3, 0.0, 0);
	RecipeInstance b = a;
	assert(b.eval(0.37) == a.eval(0.37));
}

int main() {
	prngMatches();
	namesMatch();
	valsMatch();
	normMatches();
	driftMatches();
	outputsFinite();
	instanceIsTriviallyCopyable();
	if (failures) {
		std::printf("test_recipes: %d failures\n", failures);
		return 1;
	}
	std::printf("test_recipes: all passed\n");
	return 0;
}
