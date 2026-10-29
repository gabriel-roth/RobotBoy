#include "SkylarkEngine.hpp"
#include <algorithm>
#include <cmath>

namespace shapes {

double SkylarkEngine::shapeAt(double p, const SkylarkControls& v) const {
	if (!haveInstance)
		return 0.0;
	if (v.reverse)
		p = p > 0.0 ? 1.0 - p : 0.0;
	double k = 0.5 + v.timing * 0.48;
	p = p < k ? 0.5 * p / k : 0.5 + 0.5 * (p - k) / (1.0 - k);
	double y = inst.eval(p);
	if (v.symmetry > 0.0 && p > 0.5)
		y += (inst.eval(1.0 - p) - y) * v.symmetry;
	if (v.lift != 0.0) {
		double u = std::clamp((y + 1.0) * 0.5, 0.0, 1.0);
		y = std::pow(u, std::pow(2.0, -v.lift * 2.5)) * 2.0 - 1.0;
	}
	return sanitize(y + v.tilt * (2.0 * p - 1.0), 0.0);
}

bool SkylarkEngine::transformChanged() const {
	return std::fabs(controls.timing - built.timing) > kEpsilon || std::fabs(controls.lift - built.lift) > kEpsilon
	       || std::fabs(controls.symmetry - built.symmetry) > kEpsilon || std::fabs(controls.tilt - built.tilt) > kEpsilon
	       || controls.reverse != built.reverse;
}

void SkylarkEngine::startPeakScan() {
	scan = Scan::Coarse;
	scanIndex = 0;
	scanMax = 0.0;
	scanStartPeak = peakValue;
}

void SkylarkEngine::stepPeakScan(int budget) {
	// A 256-point scan can step over a narrow peak, so the three highest
	// local maxima are refined; Fit then keeps the full cycle in range.
	for (; budget > 0 && scan != Scan::Idle; budget--) {
		switch (scan) {
			case Scan::Coarse: {
				scanPoints[scanIndex] = std::fabs(shapeAt((double) scanIndex / kScan, built));
				scanMax = std::max(scanMax, scanPoints[scanIndex]);
				if (++scanIndex == kScan) {
					scan = Scan::Select;
					scanRound = 0;
				}
				break;
			}
			case Scan::Select: {
				int best = -1;
				if (scanRound < kRefineRounds) {
					for (int i = 0; i < kScan; i++) {
						bool taken = false;
						for (int j = 0; j < scanRound; j++)
							taken = taken || (std::fabs(i - scanDone[j]) <= 2.0);
						if (taken)
							continue;
						const double* a = scanPoints;
						bool localMax = (i == 0 || a[i] >= a[i - 1]) && (i == kScan - 1 || a[i] >= a[i + 1]);
						if (localMax && (best < 0 || a[i] > a[best]))
							best = i;
					}
				}
				if (best < 0) {
					scan = Scan::Idle;
					break;
				}
				scanDone[scanRound] = best;
				scanLo = std::max(0.0, (best - 1.0) / kScan);
				scanHi = std::min(1.0 - 1e-9, (best + 1.0) / kScan);
				scanIteration = 0;
				scan = Scan::Refine;
				break;
			}
			case Scan::Refine: {
				double m1 = scanLo + (scanHi - scanLo) / 3.0, m2 = scanHi - (scanHi - scanLo) / 3.0;
				double y1 = std::fabs(shapeAt(m1, built)), y2 = std::fabs(shapeAt(m2, built));
				scanMax = std::max(scanMax, std::max(y1, y2));
				if (y1 < y2)
					scanLo = m1;
				else
					scanHi = m2;
				if (++scanIteration == kRefineIterations) {
					scanRound++;
					scan = Scan::Select;
				}
				break;
			}
			case Scan::Idle: break;
		}
	}
	// Mid-scan, the last peak too, so Fit doesn't jump while the scan catches up
	peakValue = scan == Scan::Idle ? scanMax : std::max(scanStartPeak, scanMax);
}

void SkylarkEngine::rebuild(int64_t cycle) {
	buildRecipe(inst, currentShape, controls.variation, controls.drift, cycle);
	haveInstance = true;
	builtCycle = cycle;
	built = controls;
	built.shape = currentShape;
	startPeakScan();
}

void SkylarkEngine::startBuild(int shape, int64_t cycle) {
	buildRecipeTables(spare, shape, controls.variation, controls.drift, cycle);
	spareFor = controls;
	spareFor.shape = shape;
	spareCycle = cycle;
	buildIndex = 0;
	buildPeak = 0.0;
	build = Build::Normalizing;
}

void SkylarkEngine::stepBuild(int budget) {
	// Table steps are cheaper than normalization points: kTableStepsPerUnit to a step of budget
	int tableSteps = recipeTableSteps(spare);
	for (; budget > 0 && buildIndex < tableSteps; budget--)
		for (int k = 0; k < kTableStepsPerUnit && buildIndex < tableSteps; k++, buildIndex++)
			recipeTableStep(spare, buildIndex);
	for (; budget > 0 && buildIndex < tableSteps + kNormPoints; budget--, buildIndex++) {
		double y = normPoint(spare, buildIndex - tableSteps);
		if (y > buildPeak)
			buildPeak = y;
	}
	if (buildIndex >= tableSteps + kNormPoints) {
		setRecipeScale(spare, buildPeak);
		build = Build::Ready;
	}
}

void SkylarkEngine::swapIn() {
	inst = spare;
	haveInstance = true;
	currentShape = spareFor.shape;
	builtCycle = spareCycle;
	built = controls;
	built.shape = spareFor.shape;
	built.variation = spareFor.variation;
	built.drift = spareFor.drift;
	build = Build::Idle;
	sinceCheck = 0;
	startPeakScan();
}

bool SkylarkEngine::spareIs(int shape, int64_t cycle) const {
	return spareFor.shape == shape && (spareFor.drift > 0.0 ? spareCycle == cycle : true);
}

void SkylarkEngine::spreadUpdate(int shape, int64_t cycle) {
	// A new shape, or a new cycle with Drift, is built at once in the spare
	// and swapped in when done; the playing instance carries on meanwhile
	bool urgent = shape != built.shape || (built.drift > 0.0 && cycle != builtCycle);
	if (urgent) {
		if (build == Build::Ready && spareIs(shape, cycle))
			swapIn();
		else if (build == Build::Idle || !spareIs(shape, cycle))
			startBuild(shape, cycle);
	}
	else if (build != Build::Normalizing && scan == Scan::Idle && ++sinceCheck >= kCheckInterval) {
		sinceCheck = 0;
		bool inputs = std::fabs(controls.variation - built.variation) > kEpsilon || controls.drift != built.drift;
		if (inputs)
			startBuild(shape, cycle);
		else if (transformChanged()) {
			built.timing = controls.timing;
			built.lift = controls.lift;
			built.symmetry = controls.symmetry;
			built.tilt = controls.tilt;
			built.reverse = controls.reverse;
			startPeakScan();
		}
	}
	// With Drift, the next cycle's shape is built ahead and swapped in at its start
	if (build == Build::Idle && scan == Scan::Idle && built.drift > 0.0 && !urgent)
		startBuild(built.shape, builtCycle + 1);
	if (build == Build::Normalizing) {
		stepBuild(scanSpread);
		// Built for now (not ahead): play it
		if (build == Build::Ready && spareCycle <= cycle)
			swapIn();
	}
}

double SkylarkEngine::raw(double cyclePos, int64_t cycle) {
	int shape = std::clamp(controls.shape, 0, kNumRecipes - 1);
	if (scanSpread > 0 && haveInstance) {
		spreadUpdate(shape, cycle);
		if (scan != Scan::Idle)
			stepPeakScan(scanSpread);
		return shapeAt(cyclePos, controls);
	}
	bool shapeChanged = shape != currentShape;
	currentShape = shape;
	bool newCycle = built.drift > 0.0 && cycle != builtCycle;
	if (!haveInstance || shapeChanged || newCycle) {
		rebuild(cycle);
		sinceCheck = 0;
	} else if (scan == Scan::Idle && ++sinceCheck >= kCheckInterval) {
		sinceCheck = 0;
		bool inputs = std::fabs(controls.variation - built.variation) > kEpsilon || controls.drift != built.drift;
		if (inputs)
			rebuild(cycle);
		else if (transformChanged()) {
			built.timing = controls.timing;
			built.lift = controls.lift;
			built.symmetry = controls.symmetry;
			built.tilt = controls.tilt;
			built.reverse = controls.reverse;
			startPeakScan();
		}
	}
	if (scan != Scan::Idle)
		stepPeakScan(scanSpread > 0 ? scanSpread : 1 << 30);
	// The current transforms, not the ones last scanned, so a knob or a
	// Reverse gate is heard on the next sample
	return shapeAt(cyclePos, controls);
}

void SkylarkEngine::renderCycle(float* out, int n, Treat treatMode, Range range) const {
	for (int i = 0; i < n; i++) {
		// Just short of 1, so the last point ends this cycle rather than
		// starting the next
		double p = std::min((double) i / (n - 1), 1.0 - 1e-9);
		double y = treat(shapeAt(p, controls), treatMode, peakValue);
		out[i] = (float) sanitize(toVolts(y, range), 0.0);
	}
}

void SkylarkEngine::reset() {
	haveInstance = false;
	sinceCheck = 0;
	peakValue = 1.0;
	scan = Scan::Idle;
	build = Build::Idle;
}

}  // namespace shapes
