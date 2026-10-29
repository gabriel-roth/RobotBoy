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
		// Serviced without moving the transport, so tests count exact samples
		eng.process(c, Timebase::Step{0.0, 0, false, false});
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
	while (n < 24000) {
		s = q.step();
		n++;
	}
	assert(std::fabs(s.volts - -2.5) < 0.05);
	assert(std::fabs(s.playhead - 0.25) < 0.001);

	Rig e;
	e.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	int eocs = 0;
	for (int i = 0; i < 4 * 96000; i++)
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
	for (int i = 0; i < 2 * 96000; i++)
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
	for (int i = 0; i < 24000; i++)
		s = r.step();
	assert(std::fabs(s.volts - 2.5) < 0.05);
	assert(std::fabs(s.playhead - 0.75) < 0.001);
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
		// The step sits between samples 999 and 1000, i.e. at playhead
		// 999.5 / 2000, reached on audio sample 47975: the midpoint, no lag
		if (i == 47975)
			assert(std::fabs(s.volts) < 0.02);
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

static int countEoc(Rig& r, int n, int* firstAt = nullptr) {
	int eocs = 0;
	for (int i = 0; i < n; i++)
		if (r.step().eoc) {
			if (!eocs && firstAt)
				*firstAt = i;
			eocs++;
		}
	return eocs;
}

static void reversePlayhead() {
	{
		Rig r;
		r.c.reverse = true;
		r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
		LyrebirdStep s{};
		for (int i = 0; i < 12000; i++)
			s = r.step();
		assert(std::fabs(s.playhead - 0.875) < 0.001);
	}
	// Resting ONCE: playhead is where the output reads from, mirrored
	for (int rev = 0; rev < 2; rev++)
		for (int idle = 0; idle < 3; idle++) {
			Rig r;
			r.c.loop = false;
			r.c.reverse = rev;
			r.c.idle = (IdleOutput) idle;
			r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
			LyrebirdStep s = r.step();
			double want = idle == 1 ? 0.0 : 1.0;  // First : Last/Zero
			if (rev)
				want = 1.0 - want;
			assert(std::fabs(s.playhead - want) < 1e-9);
		}
}

static void noFalseEoc() {
	// Load mid-transport
	{
		Rig r;
		r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
		for (int i = 0; i < 144000; i++)  // 1.5 bars
			r.step();
		Take& t = r.eng.spare();
		t.clear();
		t.length = 100;
		for (int i = 0; i < 100; i++)
			t.samples[i] = 0.f;
		t.seconds = 2.0;
		r.eng.requestLoad();
		int at = -1;
		int n = countEoc(r, 48000, &at);  // up to the bar boundary at 2.0
		assert(n == 1 && at == 47999);
	}
	// SPEED change x1 -> x2 across a would-be wrap, then a real wrap
	{
		Rig r;
		r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
		for (int i = 0; i < 67200; i++)  // bar position 0.7
			r.step();
		r.c.speedOct = 1.0;
		int at = -1;
		int n = countEoc(r, 28800, &at);  // to the boundary at 1.0
		assert(n == 1 && at == 28799);
	}
}

static void freeOnce() {
	Rig r;
	r.c.sync = false;
	r.c.loop = false;
	r.load(-5.f, 5.f, 2000, TakeMode::Free, 0, 2.0);
	assert(countEoc(r, 5000) == 0);
	r.step(true);
	int at = -1;
	int n = countEoc(r, 100000, &at);
	assert(n == 1 && std::abs(at - 95999) <= 2);
	LyrebirdStep s = r.step();
	assert(std::fabs(s.volts - 5.0) < 1e-4);
}

static void trigRestartsLoopMidBar() {
	Rig r;
	r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	for (int i = 0; i < 57600; i++)  // 0.6 bar
		r.step();
	LyrebirdStep s = r.step(true);
	assert(!s.eoc && s.playhead < 1e-6 && std::fabs(s.volts + 5.0) < 1e-4);
	for (int i = 0; i < 24000; i++)
		s = r.step();
	assert(std::fabs(s.playhead - 0.25) < 0.001);
	int at = -1;
	assert(countEoc(r, 96000 - 24000, &at) == 1 && at == 96000 - 24000 - 1);
}

static void syncTakeInFree() {
	// Recorded at 120 BPM over 2 bars: 4 s whatever the clock says
	for (double oct : {0.0, 1.0}) {
		Rig r;
		r.c.sync = false;
		r.load(-5.f, 5.f, 4000, TakeMode::Sync, 2.0, 4.0);
		r.c.speedOct = oct;
		int at = -1;
		int n = countEoc(r, 200000, &at);
		int want = oct == 0.0 ? 192000 : 96000;
		assert(n >= 1 && std::abs(at + 1 - want) <= 2);
	}
}

static void reverseOnce() {
	Rig r;
	r.c.loop = false;
	r.c.reverse = true;
	r.load(-5.f, 5.f, 2000, TakeMode::Sync, 1.0, 2.0);
	// Rests at the end of the reversed pass: the take's first value
	assert(std::fabs(r.step().volts + 5.0) < 1e-4);
	LyrebirdStep s = r.step(true);
	assert(std::fabs(s.volts - 5.0) < 1e-4);
	for (int i = 0; i < 48000; i++)
		s = r.step();
	assert(std::fabs(s.volts) < 0.05);
	int n = countEoc(r, 48100);
	assert(n == 1);
	assert(std::fabs(r.step().volts + 5.0) < 1e-4);
}

int main() {
	reversePlayhead();
	noFalseEoc();
	freeOnce();
	trigRestartsLoopMidBar();
	syncTakeInFree();
	reverseOnce();
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
