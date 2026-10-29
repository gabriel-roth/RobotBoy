// TakeSmoother tests: zero-phase triangular smoothing of takes.
#include "shapes/Take.hpp"
#include "shapes/TakeSmoother.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace shapes;

static void build(TakeSmoother& s, const Take& t) {
	s.invalidate();
	int guard = 0;
	while (!s.rebuildStep(t, 4096))
		assert(++guard < 1000);
	assert(s.ready());
}

static Take makeTake(int n, float v) {
	Take t;
	t.length = n;
	for (int i = 0; i < n; i++)
		t.samples[i] = v;
	return t;
}

static void zeroWidthIsRaw() {
	Take t = makeTake(200, 0.f);
	for (int i = 0; i < 200; i++)
		t.samples[i] = std::sin(i * 0.3f) * 5.f;
	TakeSmoother s;
	build(s, t);
	for (double idx = 3.0; idx < 190.0; idx += 0.37) {
		int i = (int)idx;
		double f = idx - i;
		double want = t.samples[i] * (1 - f) + t.samples[i + 1] * f;
		assert(std::fabs(s.at(t, idx, 0.0, false) - want) < 1e-9);
		assert(std::fabs(s.at(t, idx, 0.5, true) - want) < 1e-9);
	}
}

static void constantCheck(int n, double tol) {
	Take t = makeTake(n, 3.f);
	TakeSmoother s;
	build(s, t);
	const double widths[] = {0.0, 0.7, 1.0, 2.5, 10.0, 77.3, 500.0, 5000.0};
	for (double w : widths)
		for (int wrap = 0; wrap < 2; wrap++) {
			const double idxs[] = {0.0, 0.5, 1.0, n / 2 + 0.25, n - 1.5, n - 1.0};
			for (double idx : idxs)
				assert(std::fabs(s.at(t, idx, w, wrap) - 3.0) < tol);
		}
}

static void constantStaysConstant() {
	constantCheck(1000, 1e-9);
}

static void constantAtFullLength() {
	constantCheck(kMaxTakeSamples, 1e-6);
}

static void zeroPhaseImpulse() {
	Take t = makeTake(1000, 0.f);
	t.samples[500] = 1.f;
	TakeSmoother s;
	build(s, t);
	double peak = s.at(t, 500.0, 50.0, false);
	assert(peak > 0.0);
	for (int i = 0; i < 1000; i++)
		assert(s.at(t, i, 50.0, false) <= peak + 1e-12);
	for (int d = 0; d <= 60; d++)
		assert(std::fabs(s.at(t, 500.0 - d, 50.0, false) - s.at(t, 500.0 + d, 50.0, false)) < 1e-9);
	assert(s.at(t, 560.0, 50.0, false) == 0.0);
}

static void stepMidpointIsHalf() {
	Take t = makeTake(1000, 0.f);
	for (int i = 500; i < 1000; i++)
		t.samples[i] = 1.f;
	TakeSmoother s;
	build(s, t);
	assert(std::fabs(s.at(t, 499.5, 40.0, false) - 0.5) < 1e-6);
}

static void wrapUsesOtherEnd() {
	Take t = makeTake(1000, 0.f);
	for (int i = 500; i < 1000; i++)
		t.samples[i] = 1.f;
	TakeSmoother s;
	build(s, t);
	assert(std::fabs(s.at(t, 0.0, 100.0, true) - 0.5) < 0.01);
	assert(s.at(t, 0.0, 100.0, false) < 0.01);
}

static void widthLargerThanTake() {
	Take t = makeTake(1000, 0.f);
	for (int i = 500; i < 1000; i++)
		t.samples[i] = 1.f;
	TakeSmoother s;
	build(s, t);
	for (double idx = 0; idx < 1000; idx += 111.1)
		assert(std::fabs(s.at(t, idx, 5000.0, true) - 0.5) < 1e-6);
	double c = s.at(t, 300.0, 5000.0, false);
	assert(c >= 0.0 && c <= 1.0);
}

static void rebuildIsIncremental() {
	Take t = makeTake(1000, 1.f);
	TakeSmoother s;
	s.invalidate();
	assert(!s.ready());
	int calls = 0;
	bool done = false;
	while (!done) {
		done = s.rebuildStep(t, 256);
		calls++;
		assert(calls < 100);
		if (!done)
			assert(!s.ready());
	}
	assert(calls >= 1000 / 256);
	assert(s.ready());
	// Not ready: falls back to raw
	s.invalidate();
	assert(!s.ready());
	assert(std::fabs(s.at(t, 10.5, 50.0, false) - 1.0) < 1e-9);
}

int main() {
	zeroWidthIsRaw();
	constantStaysConstant();
	constantAtFullLength();
	zeroPhaseImpulse();
	stepMidpointIsHalf();
	wrapUsesOtherEnd();
	widthLargerThanTake();
	rebuildIsIncremental();
	printf("take smoother tests passed\n");
	return 0;
}
