// Skylark engine tests: transforms against playground vectors, shape
// latching, Fit range guarantee and display table consistency.
#include "reference.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/SkylarkEngine.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace shapes;

static int failures = 0;

static void check(bool ok, const char* what, int a = 0, int b = 0) {
	if (!ok) {
		if (failures < 20)
			std::printf("  FAIL %s [%d][%d]\n", what, a, b);
		failures++;
	}
}

static SkylarkControls defaults() {
	SkylarkControls c{};
	c.shape = 0;
	c.variation = 0.5;
	return c;
}

static void transformsMatch() {
	for (int n = 0; n < 6; n++) {
		const double* t = ref::transformCases[n];
		SkylarkControls c = defaults();
		c.shape = (int) t[0];
		c.variation = t[1];
		c.timing = t[2];
		c.lift = t[3];
		c.symmetry = t[4];
		c.tilt = t[5];
		c.reverse = t[6] > 0.5;
		SkylarkEngine e;
		e.setControls(c);
		for (int k = 0; k < 65; k++) {
			double got = e.raw(k / 64.0 * 0.99999, 0, k == 0);
			double want = ref::transformRaw[n][k];
			if (!(std::fabs(got - want) <= 1e-6)) {
				if (failures < 20)
					std::printf("  transform case %d k %d: got %.17g want %.17g\n", n, k, got, want);
				failures++;
			}
		}
	}
}

static void shapeLatchesAtCycleBoundary() {
	SkylarkControls c = defaults();
	c.shape = 0;
	SkylarkEngine e;
	e.setControls(c);
	e.raw(0.1, 0, true);
	check(e.activeShape() == 0, "first shape");
	c.shape = 5;
	e.setControls(c);
	e.raw(0.2, 0, false);
	check(e.activeShape() == 0, "not latched mid-cycle");
	e.raw(0.9, 0, false);
	check(e.activeShape() == 0, "still not latched");
	e.raw(0.0, 1, true);
	check(e.activeShape() == 5, "latched on wrap");
	c.shape = 7;
	e.setControls(c);
	e.raw(0.5, 1, false);
	check(e.activeShape() == 5, "pending again");
	e.reset();
	e.raw(0.5, 0, false);
	check(e.activeShape() == 7, "latched after reset");
}

static void fitNeverExceedsRange() {
	static SkylarkEngine e;
	for (int r = 0; r < kNumRecipes; r++) {
		for (int mode = 0; mode < 2; mode++) {
			SkylarkControls c = defaults();
			c.shape = r;
			if (mode == 0)
				c.tilt = 1;
			else
				c.lift = 1;
			e.reset();
			e.setControls(c);
			e.raw(0.0, 0, true);
			for (int i = 0; i < 1024; i++) {
				double x = e.raw(i / 1024.0, 0, false);
				double y = treat(x, Treat::Fit, e.peak());
				check(std::fabs(y) <= 1 + 1e-9 && std::isfinite(y), "fit range", r, i);
			}
		}
	}
}

static void renderCycleMatchesRaw() {
	SkylarkControls c = defaults();
	c.shape = 3;
	c.tilt = 0.4;
	c.timing = 0.3;
	SkylarkEngine e;
	e.setControls(c);
	e.raw(0.0, 0, true);
	const int n = 128;
	std::vector<float> out(n);
	e.renderCycle(out.data(), n, Treat::Soft, Range::Bipolar5);
	for (int i = 0; i < n; i++) {
		double want = toVolts(treat(e.raw((double) i / n, 0, false), Treat::Soft, e.peak()), Range::Bipolar5);
		check(std::fabs(out[i] - want) <= 1e-6, "render", i);
	}
}

static void driftRebuildsPerCycle() {
	SkylarkControls c = defaults();
	c.shape = 15;  // nodes
	c.drift = 0.7;
	SkylarkEngine e;
	e.setControls(c);
	double a = e.raw(0.37, 0, true);
	double b = e.raw(0.37, 1, true);
	check(a != b, "drift changes the cycle");
	double a2 = e.raw(0.37, 0, true);
	check(a == a2, "drift replays");
}

int main() {
	transformsMatch();
	shapeLatchesAtCycleBoundary();
	fitNeverExceedsRange();
	renderCycleMatchesRaw();
	driftRebuildsPerCycle();
	if (failures) {
		std::printf("%d failures\n", failures);
		return 1;
	}
	std::printf("skylark engine ok\n");
	return 0;
}
