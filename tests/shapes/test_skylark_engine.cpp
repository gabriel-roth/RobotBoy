// Skylark engine tests: transforms against reference vectors, shape
// changes, Fit range guarantee and display table consistency.
#include "reference.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/SkylarkEngine.hpp"
#include <algorithm>
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
			double got = e.raw(k / 64.0 * 0.99999, 0);
			double want = ref::transformRaw[n][k];
			if (!(std::fabs(got - want) <= 1e-6)) {
				if (failures < 20)
					std::printf("  transform case %d k %d: got %.17g want %.17g\n", n, k, got, want);
				failures++;
			}
		}
	}
}

static void shapeChangesImmediately() {
	SkylarkControls c = defaults();
	c.shape = 0;
	SkylarkEngine e;
	e.setControls(c);
	e.raw(0.1, 0);
	check(e.activeShape() == 0, "first shape");
	c.shape = 5;
	e.setControls(c);
	double got = e.raw(0.2, 0);
	check(e.activeShape() == 5, "changed mid-cycle");
	SkylarkEngine fresh;
	fresh.setControls(c);
	check(got == fresh.raw(0.2, 0), "new shape on the next call");
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
			e.raw(0.0, 0);
			for (int i = 0; i < 1024; i++) {
				double x = e.raw(i / 1024.0, 0);
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
	e.raw(0.0, 0);
	const int n = 128;
	std::vector<float> out(n);
	e.renderCycle(out.data(), n, Treat::Soft, Range::Bipolar5);
	for (int i = 0; i < n; i++) {
		double p = std::min((double) i / (n - 1), 1.0 - 1e-9);
		double want = toVolts(treat(e.raw(p, 0), Treat::Soft, e.peak()), Range::Bipolar5);
		check(std::fabs(out[i] - want) <= 1e-6, "render", i);
	}
}

static void driftRebuildsPerCycle() {
	SkylarkControls c = defaults();
	c.shape = 15;  // nodes
	c.drift = 0.7;
	SkylarkEngine e;
	e.setControls(c);
	double a = e.raw(0.37, 0);
	double b = e.raw(0.37, 1);
	check(a != b, "drift changes the cycle");
	double a2 = e.raw(0.37, 0);
	check(a == a2, "drift replays");
}

// Reverse (and the other transforms) apply on the next call, not after the
// 64-sample check.
static void reverseTakesEffectImmediately() {
	SkylarkControls c = defaults();
	c.shape = 3;
	SkylarkEngine e;
	e.setControls(c);
	e.raw(0.0, 0);
	double forward = e.raw(0.3, 0);
	c.reverse = true;
	e.setControls(c);
	double reversed = e.raw(0.3, 0);
	SkylarkEngine fresh;
	fresh.setControls(c);
	fresh.raw(0.0, 0);
	double want = fresh.raw(0.3, 0);
	check(reversed == want, "reverse on the next call");
	check(reversed != forward, "reverse changes the value");
	c.reverse = false;
	c.tilt = 0.5;
	e.setControls(c);
	fresh.setControls(c);
	check(e.raw(0.4, 0) == fresh.raw(0.4, 0), "tilt on the next call");
}

// A spread peak scan ends on the same peak as one done at once, after a
// rebuild and after a transform change; mid-scan, Fit stays no lower than
// the last peak.
static void spreadScanMatches() {
	static SkylarkEngine whole, spread;
	for (int r = 0; r < kNumRecipes; r++) {
		SkylarkControls c = defaults();
		c.shape = r;
		whole.reset();
		spread.reset();
		whole.setScanSpread(0);
		spread.setScanSpread(1);
		int i = 0;
		auto run = [&](int calls) {
			for (int k = 0; k < calls; k++, i++) {
				double p = (i % 997) / 997.0;
				whole.setControls(c);
				spread.setControls(c);
				check(whole.raw(p, 0) == spread.raw(p, 0), "same output", r, i);
			}
		};
		run(400);
		check(whole.peak() == spread.peak(), "spread peak after a rebuild", r);
		double before = spread.peak();
		c.lift = 0.8;
		c.tilt = -0.5;
		// The change is picked up at the next check, then scanned
		for (int k = 0; k < 64; k++)
			run(1);
		check(spread.peak() >= before, "mid-scan peak holds", r);
		run(400);
		check(whole.peak() == spread.peak(), "spread peak after a transform change", r);
	}
}

int main() {
	spreadScanMatches();
	transformsMatch();
	shapeChangesImmediately();
	fitNeverExceedsRange();
	renderCycleMatchesRaw();
	driftRebuildsPerCycle();
	reverseTakesEffectImmediately();
	if (failures) {
		std::printf("%d failures\n", failures);
		return 1;
	}
	std::printf("skylark engine ok\n");
	return 0;
}
