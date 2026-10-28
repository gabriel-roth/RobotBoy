// Output stage tests: out-of-range treatment, volts, inversion, slew, trigger.
#include "reference.hpp"
#include "shapes/OutputStage.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace shapes;

static void treatMatchesPlayground() {
	const Treat modes[5] = {Treat::Fit, Treat::Clip, Treat::Soft, Treat::Fold, Treat::Wrap};
	for (int m = 0; m < 5; m++)
		for (int i = 0; i < 9; i++)
			assert(std::fabs(treat(ref::treatIn[i], modes[m], 1.6) - ref::treatOut[m][i]) < 1e-9);
	// Fit leaves in-range cycles alone
	assert(treat(0.5, Treat::Fit, 0.8) == 0.5);
}

static void toVoltsRanges() {
	assert(toVolts(1.0, Range::Bipolar5) == 5.0);
	assert(toVolts(-1.0, Range::Unipolar10) == 0.0);
	assert(toVolts(1.0, Range::Unipolar10) == 10.0);
}

static void invertMirrorsWithinRange() {
	assert(invertVolts(3.0, Range::Bipolar5) == -3.0);
	assert(invertVolts(3.0, Range::Unipolar10) == 7.0);
}

static void slewOffIsPassthrough() {
	Slew s;
	s.set(0.0, 48000.0);
	assert(s.process(3.0) == 3.0);
	assert(s.process(-2.0) == -2.0);
}

static void slew5msSettles() {
	Slew s;
	s.set(5.0, 48000.0);
	s.reset(0.0);
	double y = 0.0;
	for (int i = 0; i < 1440; i++)  // 30 ms
		y = s.process(1.0);
	assert(y > 0.99 && y < 1.0);
}

static void triggerIsOneMs() {
	Trigger t;
	t.fire();
	int high = 0;
	for (int i = 0; i < 200; i++) {
		float v = t.process(1.0 / 48000.0);
		if (v > 0.f) {
			assert(v == 10.f);
			high++;
		}
	}
	assert(std::abs(high - 48) <= 1);
}

static void sanitizeReplacesNonFinite() {
	const double inf = std::numeric_limits<double>::infinity();
	assert(sanitize(std::nan(""), 2.0) == 2.0);
	assert(sanitize(inf, 2.0) == 2.0);
	assert(sanitize(-inf, 2.0) == 2.0);
	assert(sanitize(1.5, 2.0) == 1.5);
}

int main() {
	treatMatchesPlayground();
	toVoltsRanges();
	invertMirrorsWithinRange();
	slewOffIsPassthrough();
	slew5msSettles();
	triggerIsOneMs();
	sanitizeReplacesNonFinite();
	printf("test_output_stage OK\n");
	return 0;
}
