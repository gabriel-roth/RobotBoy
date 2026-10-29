#pragma once
#include "Spline.hpp"
#include <cmath>
#include <cstdint>

// Recipes: structured one-cycle shapes (a bounce, a ratchet, a fractal
// line...), each built from a few settings that one Variation knob sweeps
// together. Rack-free; building and evaluating never allocate.
namespace shapes {

constexpr int kNumRecipes = 18;
constexpr int kMaxRecipeParams = 8;

const char* recipeName(int recipe);
const char* recipeFamily(int recipe);

// One built recipe: its settings, the tables its build drew from the seed's
// random stream, and the scale that normalizes its peak to 1.
struct RecipeInstance {
	int recipe = -1;
	double vals[kMaxRecipeParams];
	// 1 / peak of |f| over 512 points (1 when the shape is near silent).
	double scale;

	union Tables {
		struct {
			int count;
			double start[16], dur[16], height[16];
			double total;
		} bounce;
		struct {
			double onset[17];
		} ratchet;
		struct {
			double amp[24], phase[24];
		} harm;
		struct {
			double phase[1025];
			double k;
		} chirp;
		struct {
			double ph1, ph2;
		} beat;
		struct {
			double level[12];
		} arp;
		struct {
			double level[6];
		} ringing;
		Spline nodes;
		struct {
			double table[256];
		} fractal;
		struct {
			double edges[25], level[24], burst[24];
			double salt;
		} glitch;
	} tab;

	// The normalized shape at phase p in [0, 1). Never NaN or infinite.
	double eval(double p) const;
	// The shape before normalization.
	double raw(double p) const;
};

// The recipe's settings at a Variation knob position (0..1), after mapping
// onto the recipe's curated Variation range. Drift (0..1) nudges each
// setting per cycle from a hash, so a reset replays the same drift. Unused
// slots are zeroed.
void recipeVals(int recipe, double variation, double drift, int64_t cycle, double out[kMaxRecipeParams]);

void buildRecipe(RecipeInstance& out, int recipe, double variation, double drift, int64_t cycle);

// buildRecipe() in parts, so a caller can spread the costly ones: the
// tables (scale left at 1), then recipeTableSteps() table steps (Chirp's
// phase table; none for the others) in order, then kNormPoints points of
// |raw|, then the scale from their peak. Together they give exactly what
// buildRecipe() does.
constexpr int kNormPoints = 512;
constexpr int kChirpSteps = 1024;
void buildRecipeTables(RecipeInstance& out, int recipe, double variation, double drift, int64_t cycle);
int recipeTableSteps(const RecipeInstance& inst);
void recipeTableStep(RecipeInstance& inst, int i);
inline double normPoint(const RecipeInstance& inst, int i) {
	return std::fabs(inst.raw(i / (double) kNormPoints));
}
inline void setRecipeScale(RecipeInstance& inst, double peak) {
	inst.scale = peak > 1e-3 ? 1.0 / peak : 1.0;
}

}  // namespace shapes
