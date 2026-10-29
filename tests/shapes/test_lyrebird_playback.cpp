// Lyrebird playback tests: transport, speed, idle output, smoothing.
#include "shapes/LyrebirdEngine.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace shapes;

static constexpr double kSr = 48000.0;

struct Rig {
	LyrebirdEngine eng;
	Timebase tb;
	TimebaseInput in;
	LyrebirdControls c;
	Rig() {
		eng.setSampleRate(kSr);
		tb.setSampleRate(kSr);
		in.bpm = 120.0;
	}
	// resetNow fires TRIG on this sample
	LyrebirdStep step(bool resetNow = false) {
		in.resetButton = resetNow;
		return eng.process(c, tb.process(in));
	}
	void load(float a, float b, int n, TakeMode m, double bars, double seconds) {
		Take& t = eng.spare();
		t.clear();
		t.length = n;
		for (int i = 0; i < n; i++)
			t.samples[i] = a + (b - a) * i / (n - 1);
		t.mode = m;
		t.bars = bars;
		t.seconds = seconds;
		t.bpm = 120.0;
		eng.requestLoad();
		step();
	}
};

static void emptyPassesSource() {
	Rig r;
	r.c.source = 2;
	r.c.scale = -1;
	r.c.offset = 1;
	for (int i = 0; i < 100; i++)
		assert(std::fabs(r.step().volts - -1.0) < 1e-9);
}

static void syncLoopLocksToBar() {
	Rig q;
	q.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	LyrebirdStep s{};
	int n = 0;
	while (n < 24000 - 1) {
		s = q.step();
		n++;
	}
	assert(std::fabs(s.volts - -2.5) < 0.05);
	assert(std::fabs(s.playhead - 0.25) < 0.001);

	Rig e;
	e.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	int eocs = 0;
	for (int i = 0; i < 4 * 96000 - 1; i++)
		if (e.step().eoc)
			eocs++;
	assert(eocs == 4);
}

static void syncSpeedSnaps() {
	assert(LyrebirdEngine::syncRatio(0.4) == 1.0);
	assert(LyrebirdEngine::syncRatio(-1.2) == 0.5);
	assert(LyrebirdEngine::syncRatio(9) == 16.0);
	assert(std::fabs(LyrebirdEngine::freeRatio(0.4) - std::exp2(0.4)) < 1e-12);
	Rig r;
	r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.speedOct = 1.0;
	int eocs = 0;
	for (int i = 0; i < 2 * 96000 - 1; i++)
		if (r.step().eoc)
			eocs++;
	assert(eocs == 4);  // two per bar over two bars
}

static void freeLoopUsesOwnRate() {
	for (double oct : {0.0, 1.0}) {
		Rig r;
		r.c.sync = false;
		r.load(-5.f, 5.f, 2000, TakeMode::Free, 0, 2.0);
		int last = -1, gap = 0;
		for (int i = 0; i < 400000; i++) {
			if (r.step().eoc) {
				if (last >= 0) {
					gap = i - last;
					int want = oct == 0.0 ? 96000 : 48000;
					assert(std::abs(gap - want) <= 1);
				}
				last = i;
			}
			if (i == 0)
				r.c.speedOct = oct;
		}
		assert(gap > 0);
	}
}

static void onceRestsOnLastValue() {
	for (int idle = 0; idle < 3; idle++) {
		Rig r;
		r.c.loop = false;
		r.c.idle = (IdleOutput) idle;
		r.c.offset = 0.5;
		r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
		double want = idle == 0 ? 5.5 : idle == 1 ? -4.5 : 0.5;
		for (int i = 0; i < 1000; i++)
			assert(std::fabs(r.step().volts - want) < 1e-4);
		// A pass
		int eocs = 0;
		double v = 0, mid = 0;
		for (int i = 0; i < 96000 + 2000; i++) {
			LyrebirdStep s = r.step(i == 0);
			if (s.eoc)
				eocs++;
			if (i == 48000)
				mid = s.volts;
			v = s.volts;
		}
		assert(eocs == 1);
		assert(std::fabs(mid - 0.5) < 0.05);
		assert(std::fabs(v - want) < 1e-4);
	}
}

static void reverseMirrors() {
	Rig r;
	r.c.reverse = true;
	r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	LyrebirdStep s{};
	for (int i = 0; i < 24000 - 1; i++)
		s = r.step();
	assert(std::fabs(s.volts - 2.5) < 0.05);
}

static void freeTakeInSyncStretches() {
	Take t;
	t.mode = TakeMode::Free;
	t.seconds = 3.9;
	t.bpm = 120.0;
	t.length = 3900;
	assert(LyrebirdEngine::effectiveBars(t) == 2.0);
	t.seconds = 0.4;  // 0.2 bars
	assert(LyrebirdEngine::effectiveBars(t) == 0.25);
	t.mode = TakeMode::Sync;
	t.bars = 4;
	assert(LyrebirdEngine::effectiveBars(t) == 4.0);
	// smoothMs is the full width, so the half-width is a quarter of the take
	// rate per millisecond
	t.seconds = 2.0;
	t.length = 2000;
	assert(std::fabs(LyrebirdEngine::smoothHalfWidth(t, 50) - 25.0) < 1e-9);
}

static void smoothingCentred() {
	Rig q;
	q.c.smoothMs = 50;
	q.load(0.f, 0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	Take& qc = const_cast<Take&>(q.eng.take());
	for (int i = 0; i < 2000; i++)
		qc.samples[i] = i < 1000 ? -5.f : 5.f;
	qc.version++;
	for (int i = 0; i < 5000; i++)
		q.step();
	// restart the bar so we can walk to the edge
	q.step(true);
	LyrebirdStep s{};
	double before = 0, after = 0;
	for (int i = 0; i < 48000 + 1200; i++) {
		s = q.step();
		if (i == 48000 - 1200 - 1)
			before = s.volts;
		if (i == 48000 - 1)
			assert(std::fabs(s.volts) < 0.6);  // midpoint at the edge: no lag
		if (i == 48000 + 1200 - 1)
			after = s.volts;
	}
	assert(before < -4.0 && after > 4.0);
}

static void outputsNeverNonFinite() {
	double nan = std::nan("");
	for (int empty = 0; empty < 2; empty++) {
		Rig r;
		if (!empty)
			r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
		r.c.source = nan;
		r.c.scale = nan;
		r.c.offset = nan;
		r.c.speedOct = nan;
		r.c.smoothMs = nan;
		for (int i = 0; i < 5000; i++) {
			LyrebirdStep s = r.step(i == 100);
			assert(std::isfinite(s.volts) && std::fabs(s.volts) <= 10.0);
			assert(std::isfinite(s.playhead));
		}
		r.c.scale = 1e30;
		r.c.offset = 1e30;
		r.c.source = 5;
		LyrebirdStep s = r.step();
		assert(std::fabs(s.volts) <= 10.0);
	}
}

static void loadUndoSnapshot() {
	Rig r;
	r.load(1.f, 1.f, 100, TakeMode::Sync, 1.0, 2.0);
	assert(r.eng.take().length == 100);
	assert(!r.eng.loadPending());
	r.load(2.f, 2.f, 50, TakeMode::Sync, 1.0, 2.0);
	assert(r.eng.take().length == 50);
	r.eng.requestUndo();
	r.step();
	assert(r.eng.take().length == 100);
	r.eng.requestSnapshot();
	r.step();
	assert(r.eng.snapshotReady() && r.eng.snapshot().length == 100);
	r.eng.releaseSnapshot();
	assert(!r.eng.snapshotReady());
}

static void traceMatches() {
	Rig r;
	r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.scale = 0.5;
	for (int i = 0; i < 5000; i++)
		r.step();
	float raw[11], out[11];
	r.eng.renderTrace(r.c, raw, out, 11);
	assert(std::fabs(raw[0] + 5.f) < 1e-4 && std::fabs(raw[10] - 5.f) < 1e-4);
	assert(std::fabs(out[10] - 2.5f) < 1e-4);
}

int main() {
	emptyPassesSource();
	syncLoopLocksToBar();
	syncSpeedSnaps();
	freeLoopUsesOwnRate();
	onceRestsOnLastValue();
	reverseMirrors();
	freeTakeInSyncStretches();
	smoothingCentred();
	outputsNeverNonFinite();
	loadUndoSnapshot();
	traceMatches();
	printf("lyrebird playback tests passed\n");
	return 0;
}
