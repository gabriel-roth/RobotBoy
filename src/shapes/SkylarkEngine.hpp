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
	// Steps of the shape build and peak scan done per call to raw(). 0, the
	// default, does each in the call that needs it. Spread, a new shape (or
	// Variation, or Drift's next cycle) takes about 512 / stepsPerCall calls
	// to build (Chirp 768), the old one playing meanwhile (Drift builds the
	// next cycle ahead, so it lands on time), then the scan about
	// 300 / stepsPerCall calls, during which Fit also uses the last peak.
	void setScanSpread(int stepsPerCall) {
		scanSpread = stepsPerCall > 0 ? stepsPerCall : 0;
	}

private:
	// The expensive work (rebuild, peak rescan) runs at most this often, and
	// not while a spread scan is running; the transforms themselves apply
	// every sample.
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
	// Spread mode (scanSpread > 0): new instances are built in the spare a
	// step at a time, then swapped in
	void spreadUpdate(int shape, int64_t cycle);
	void startBuild(int shape, int64_t cycle);
	void stepBuild(int budget);
	void swapIn();
	bool spareIs(int shape, int64_t cycle) const;
	// Starts a peak scan of the instance with the built transforms
	void startPeakScan();
	// Does up to budget steps of it (one or two shape evaluations each)
	void stepPeakScan(int budget);
	bool transformChanged() const;

	SkylarkControls controls;
	SkylarkControls built;  // controls the instance and peak were computed from
	RecipeInstance inst;
	bool haveInstance = false;
	int currentShape = 0;
	int64_t builtCycle = 0;
	int sinceCheck = 0;
	double peakValue = 1.0;

	// The peak scan: 256 points, then the three highest local maxima refined
	static constexpr int kScan = 256;
	static constexpr int kRefineRounds = 3;
	static constexpr int kRefineIterations = 14;
	enum class Scan { Idle, Coarse, Select, Refine };
	int scanSpread = 0;
	Scan scan = Scan::Idle;
	double scanPoints[kScan] = {};
	int scanIndex = 0;
	int scanRound = 0;
	int scanIteration = 0;
	double scanMax = 0.0;
	double scanStartPeak = 1.0;
	double scanDone[kRefineRounds] = {};
	double scanLo = 0.0, scanHi = 0.0;

	// The spare instance being built (spread mode)
	static constexpr int kTableStepsPerUnit = 4;
	enum class Build { Idle, Normalizing, Ready };
	Build build = Build::Idle;
	RecipeInstance spare;
	SkylarkControls spareFor;  // shape, variation and drift it's built from
	int64_t spareCycle = 0;
	int buildIndex = 0;
	double buildPeak = 0.0;
};

}  // namespace shapes
