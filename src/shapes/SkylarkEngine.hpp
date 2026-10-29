#pragma once
#include "OutputStage.hpp"
#include "Recipes.hpp"
#include <cstdint>

// Skylark: one Recipes shape per cycle with the transform controls applied.
// Rack-free; raw() and setControls() never allocate or lock.
namespace shapes {

// Values already clamped by the module.
struct SkylarkControls {
	int shape = 0;
	double variation = 0.5;
	double timing = 0.0;
	double lift = 0.0;
	double symmetry = 0.0;
	double tilt = 0.0;
	double drift = 0.0;
	bool reverse = false;
};

class SkylarkEngine {
public:
	void setControls(const SkylarkControls& c) {
		controls = c;
	}

	// The transformed shape at cyclePos in [0, 1) of the given cycle. A new
	// shape takes effect on the next call.
	double raw(double cyclePos, int64_t cycle);
	// Max |raw| over the current cycle (1 before the first call).
	double peak() const {
		return peakValue;
	}
	int activeShape() const {
		return currentShape;
	}
	// Fills out with n treated, scaled points of the current cycle, the first
	// at its start and the last at its end (an open trace).
	void renderCycle(float* out, int n, Treat treatMode, Range range) const;
	void reset();

private:
	// The expensive work (rebuild, peak rescan) runs at most this often; the
	// transforms themselves apply every sample.
#ifdef METAMODULE
	// Modulated Variation made this check dominate the cost (about 10x static
	// on the desktop bench), so the MetaModule build runs it less often.
	static constexpr int kCheckInterval = 256;
#else
	static constexpr int kCheckInterval = 64;
#endif
	static constexpr double kEpsilon = 1e-4;

	// The instance at p with the transforms in v applied.
	double shapeAt(double p, const SkylarkControls& v) const;
	void rebuild(int64_t cycle);
	void updatePeak();
	bool transformChanged() const;

	SkylarkControls controls;
	SkylarkControls built;  // controls the instance and peak were computed from
	RecipeInstance inst;
	bool haveInstance = false;
	int currentShape = 0;
	int64_t builtCycle = 0;
	int sinceCheck = 0;
	double peakValue = 1.0;
};

}  // namespace shapes
