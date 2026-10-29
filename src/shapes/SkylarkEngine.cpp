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

void SkylarkEngine::updatePeak() {
	// A 256-point scan can step over a narrow peak, so the three highest
	// local maxima are refined; Fit then keeps the full cycle in range.
	constexpr int kScan = 256;
	double a[kScan];
	double m = 0.0;
	for (int i = 0; i < kScan; i++) {
		a[i] = std::fabs(shapeAt((double) i / kScan, built));
		m = std::max(m, a[i]);
	}
	double done[3] = {-1.0, -1.0, -1.0};
	for (int r = 0; r < 3; r++) {
		int best = -1;
		for (int i = 0; i < kScan; i++) {
			bool taken = false;
			for (int j = 0; j < r; j++)
				taken = taken || (std::fabs(i - done[j]) <= 2.0);
			if (taken)
				continue;
			bool localMax = (i == 0 || a[i] >= a[i - 1]) && (i == kScan - 1 || a[i] >= a[i + 1]);
			if (localMax && (best < 0 || a[i] > a[best]))
				best = i;
		}
		if (best < 0)
			break;
		done[r] = best;
		double lo = std::max(0.0, (best - 1.0) / kScan), hi = std::min(1.0 - 1e-9, (best + 1.0) / kScan);
		for (int it = 0; it < 14; it++) {
			double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
			double y1 = std::fabs(shapeAt(m1, built)), y2 = std::fabs(shapeAt(m2, built));
			m = std::max(m, std::max(y1, y2));
			if (y1 < y2)
				lo = m1;
			else
				hi = m2;
		}
	}
	peakValue = m;
}

void SkylarkEngine::rebuild(int64_t cycle) {
	buildRecipe(inst, currentShape, controls.variation, controls.drift, cycle);
	haveInstance = true;
	builtCycle = cycle;
	built = controls;
	built.shape = currentShape;
	updatePeak();
}

double SkylarkEngine::raw(double cyclePos, int64_t cycle) {
	int shape = std::clamp(controls.shape, 0, kNumRecipes - 1);
	bool shapeChanged = shape != currentShape;
	currentShape = shape;
	bool newCycle = built.drift > 0.0 && cycle != builtCycle;
	if (!haveInstance || shapeChanged || newCycle) {
		rebuild(cycle);
		sinceCheck = 0;
	} else if (++sinceCheck >= kCheckInterval) {
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
			updatePeak();
		}
	}
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
}

}  // namespace shapes
