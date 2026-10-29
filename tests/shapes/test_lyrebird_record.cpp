// Lyrebird recording tests: REC, punch-in, load, undo and snapshots.
#include "shapes/LyrebirdEngine.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

using namespace shapes;

static constexpr double kSr = 48000.0;

struct Rig {
	LyrebirdEngine eng;
	Timebase tb;
	TimebaseInput in;
	LyrebirdControls c;
	Timebase::Step last{};
	explicit Rig(double sr = kSr) {
		eng.setSampleRate(sr);
		tb.setSampleRate(sr);
		in.bpm = 120.0;
		c.bpm = 120.0;
	}
	// Transport position in bars after the sample about to run
	LyrebirdStep step() {
		last = tb.process(in);
		in.resetButton = false;
		return eng.process(c, last);
	}
	double bars() const {
		return (double) last.cycle + last.pos;
	}
	// Runs n samples; edit(i, controls) may change the controls before each one
	LyrebirdStep run(int n, const std::function<void(int)>& edit = nullptr) {
		LyrebirdStep s{};
		for (int i = 0; i < n; i++) {
			if (edit)
				edit(i);
			s = step();
		}
		return s;
	}
	// One Toggle press of REC or PUNCH (a single high sample)
	LyrebirdStep pressRec() {
		c.rec = true;
		LyrebirdStep s = step();
		c.rec = false;
		return s;
	}
	LyrebirdStep pressPunch() {
		c.punch = true;
		LyrebirdStep s = step();
		c.punch = false;
		return s;
	}
	// Runs until the transport has passed bar position x (unwrapped)
	LyrebirdStep runTo(double x) {
		LyrebirdStep s{};
		while (bars() < x)
			s = step();
		return s;
	}
	void fillSpare(float v, int n, TakeMode m, double barsLen, double seconds, const char* name = "") {
		Take& t = eng.spare();
		t.clear();
		t.length = n;
		for (int i = 0; i < n; i++)
			t.samples[i] = v;
		t.mode = m;
		t.bars = barsLen;
		t.seconds = seconds;
		t.bpm = 120.0;
		snprintf(t.name, sizeof t.name, "%s", name);
	}
	// Loads without moving the transport
	void load(float v, int n, TakeMode m, double barsLen, double seconds, const char* name = "") {
		fillSpare(v, n, m, barsLen, seconds, name);
		eng.requestLoad();
		eng.process(c, Timebase::Step{last.pos, last.cycle, false, false});
	}
	// A Free take of constant v, about ms long
	void recordFree(double v, int ms) {
		bool sync = c.sync;
		c.sync = false;
		c.source = v;
		step();  // REC low first, so the press is an edge
		pressRec();
		run((int) (ms * kSr / 1000.0) - 1);
		pressRec();
		c.sync = sync;
	}
};

static void syncTakeStartsMidBarAndWraps() {
	Rig r;
	r.c.lengthIndex = 2;  // 1 bar
	r.runTo(0.5);
	// Source is the unwrapped bar position, so each sample shows when it was taken
	LyrebirdStep s = r.run(96000 + 200, [&](int i) {
		r.c.source = r.tb.position() + 0.5 / 96000.0;
		r.c.rec = i == 0;
	});
	assert(r.eng.take().length == 2000);
	assert(s.status == Status::Idle);
	const Take& t = r.eng.take();
	assert(t.mode == TakeMode::Sync && t.bars == 1.0);
	assert(std::fabs(t.bpm - 120.0) < 1e-6);
	assert(std::fabs(t.seconds - 2.0) < 1e-6);
	assert(std::fabs(t.samples[0] - 1.0) < 0.002);
	for (int i = 0; i < 2000; i++) {
		double want = i >= 1000 ? 0.5 + (i - 1000) / 2000.0 : 1.0 + i / 2000.0;
		assert(std::fabs(t.samples[i] - want) < 0.002);
	}
}

static void syncTakeStopsItself() {
	Rig r;
	r.run(100);
	r.c.source = 1;
	int recording = 0;
	LyrebirdStep s = r.pressRec();
	assert(s.status == Status::Recording);
	recording++;
	for (int i = 0; i < 100000; i++)
		if (r.step().status == Status::Recording)
			recording++;
	assert(recording == 96000);
	assert(r.eng.take().length == 2000);
}

static void syncTakeFullUnderTempoChange() {
	// Tempo change, then TRIG mid-take: every index still gets written
	for (int variant = 0; variant < 2; variant++) {
		Rig r;
		r.runTo(0.3);
		r.c.source = 2;
		r.pressRec();
		r.runTo(0.7);
		if (variant == 0)
			r.in.bpm = r.c.bpm = 180.0;
		else
			r.in.resetButton = true;
		LyrebirdStep s{};
		for (int i = 0; i < 200000 && (s = r.step()).status == Status::Recording; i++) {
		}
		assert(s.status == Status::Idle);
		const Take& t = r.eng.take();
		assert(t.length == 2000);
		assert(std::fabs(t.bpm - 120.0) < 1e-6);
		for (int i = 0; i < 2000; i++)
			assert(std::fabs(t.samples[i] - 2.f) < 1e-6);
	}
}

static void syncEarlyStopHoldsLast() {
	Rig r;
	r.c.source = 1;
	r.pressRec();
	r.runTo(0.25);
	r.c.source = 4;
	r.runTo(0.5);
	LyrebirdStep s = r.pressRec();
	assert(s.status == Status::Idle);
	const Take& t = r.eng.take();
	assert(t.length == 2000);
	assert(std::fabs(t.samples[250] - 1.f) < 1e-6);
	assert(std::fabs(t.samples[750] - 4.f) < 1e-6);
	for (int i = 1002; i < 2000; i++)
		assert(std::fabs(t.samples[i] - 4.f) < 1e-6);
}

static void freeTakeToggleAndHold() {
	Rig r;
	r.c.sync = false;
	r.c.source = 1;
	r.run(10);
	assert(r.pressRec().status == Status::Recording);
	r.run(72000 - 1);
	assert(r.pressRec().status == Status::Idle);
	const Take& t = r.eng.take();
	assert(t.mode == TakeMode::Free);
	assert(std::abs(t.length - 1500) <= 1);
	assert(std::fabs(t.seconds - 1.5) < 0.0011);

	Rig h;
	h.c.sync = false;
	h.c.buttons = RecordButtons::Hold;
	h.c.source = 1;
	h.c.rec = true;
	h.run(33600);
	h.c.rec = false;
	assert(h.step().status == Status::Idle);
	assert(std::abs(h.eng.take().length - 700) <= 1);
}

static void freeTakeLimit() {
	Rig r;
	r.c.sync = false;
	r.c.buttons = RecordButtons::Hold;
	r.c.source = 1;
	r.c.rec = true;
	int hits = 0;
	LyrebirdStep s{};
	for (int i = 0; i < 181 * 48000; i++) {
		s = r.step();
		if (s.hitLimit)
			hits++;
	}
	assert(hits == 1);
	assert(s.status == Status::Idle);
	assert(r.eng.take().length == kMaxTakeSamples);
	assert(std::fabs(r.eng.take().seconds - 180.0) < 1e-6);
}

static void movementStartWaitsForMotion() {
	Rig r;
	r.c.sync = false;
	r.c.startOn = StartOn::Movement;
	r.c.source = 1;
	assert(r.pressRec().status == Status::Armed);
	LyrebirdStep s = r.run(5000, [&](int i) { r.c.source = 1.0 + 0.04 * std::sin(i * 0.01); });
	assert(s.status == Status::Armed);
	assert(r.eng.take().empty());
	r.c.source = 1.06;
	assert(r.step().status == Status::Recording);
	r.run(4800 - 1);
	assert(r.pressRec().status == Status::Idle);
	assert(std::abs(r.eng.take().length - 100) <= 1);

	// A press while armed cancels
	Rig c;
	c.c.startOn = StartOn::Movement;
	assert(c.pressRec().status == Status::Armed);
	assert(c.step().status == Status::Armed);
	assert(c.pressRec().status == Status::Idle);
}

static void punchReplacesOnlyItsSpan() {
	Rig r;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.source = 5;
	r.runTo(0.25);
	assert(r.pressPunch().status == Status::Punching);
	r.runTo(0.5);
	r.pressPunch();
	r.runTo(1.2);
	const Take& t = r.eng.take();
	assert(t.length == 2000);
	for (int i = 0; i < 2000; i++) {
		double v = t.samples[i];
		if (i < 480 || i > 1020)
			assert(v == 0.0);
		if (i > 520 && i < 990)
			assert(std::fabs(v - 5.0) < 1e-6);
	}
	int rising = 0, falling = 0;
	for (int i = 480; i < 520; i++) {
		assert(t.samples[i + 1] >= t.samples[i]);
		if (t.samples[i] > 1e-6 && t.samples[i] < 5.0 - 1e-6)
			rising++;
	}
	for (int i = 980; i < 1020; i++) {
		assert(t.samples[i + 1] <= t.samples[i]);
		if (t.samples[i] > 1e-6 && t.samples[i] < 5.0 - 1e-6)
			falling++;
	}
	assert(rising >= 8 && rising <= 12);
	assert(falling >= 8 && falling <= 12);

	// Playback after the punch plays the new data, smoothed
	r.c.smoothMs = 50;
	LyrebirdStep s = r.runTo(1.375);
	assert(s.status == Status::Idle);
	assert(std::fabs(s.volts - 5.0) < 1e-3);
}

static void punchFastReverseWritesContiguously() {
	// At 1 kHz the playhead crosses four take samples per audio sample
	for (double sr : {kSr, 1000.0}) {
		Rig r(sr);
		r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
		r.c.speedOct = 2;
		r.c.reverse = true;
		r.c.source = 5;
		r.runTo(0.33);
		r.pressPunch();
		r.runTo(0.43);
		r.pressPunch();
		r.runTo(0.6);
		const Take& t = r.eng.take();
		int written = 0, edges = 0;
		for (int i = 0; i < 2000; i++) {
			bool a = t.samples[i] != 0.f;
			bool b = t.samples[(i + 1) % 2000] != 0.f;
			if (a)
				written++;
			if (a != b)
				edges++;
		}
		assert(edges == 2);
		// 0.4 of the take, plus the tail: at x4 the 10 ms fade spans about 40 samples
		assert(written > 790 && written < 860);
		for (int i = 2000; i < 2100; i++)
			assert(t.samples[i] == 0.f);
	}
}

static void punchWithoutTakeRecords() {
	Rig r;
	r.c.source = 3;
	assert(r.pressPunch().status == Status::Recording);
	r.runTo(0.5);
	assert(r.pressPunch().status == Status::Idle);
	const Take& t = r.eng.take();
	assert(t.length == 2000 && t.mode == TakeMode::Sync);
	assert(std::fabs(t.samples[1500] - 3.f) < 1e-6);
}

static void undoSwapsBack() {
	Rig r;
	r.recordFree(1, 200);
	r.recordFree(2, 300);
	assert(r.eng.take().samples[10] == 2.f);
	r.eng.requestUndo();
	r.step();
	assert(r.eng.take().samples[10] == 1.f);
	assert(std::abs(r.eng.take().length - 200) <= 1);
	r.eng.requestUndo();
	r.step();
	assert(r.eng.take().samples[10] == 2.f);
	r.load(7.f, 500, TakeMode::Free, 1.0, 0.5);
	assert(r.eng.take().samples[10] == 7.f);
	r.eng.requestUndo();
	r.step();
	assert(r.eng.take().samples[10] == 2.f);
	assert(std::abs(r.eng.take().length - 300) <= 1);
}

static void loadDuringRecordingAbandonsTake() {
	Rig r;
	r.recordFree(1, 200);
	r.c.sync = false;
	r.c.source = 2;
	r.pressRec();
	r.run(10000);
	r.fillSpare(3.f, 500, TakeMode::Free, 1.0, 0.5, "LOADED");
	r.eng.requestLoad();
	LyrebirdStep s = r.step();
	assert(s.status == Status::Idle);
	assert(r.eng.take().length == 500 && r.eng.take().samples[10] == 3.f);
	assert(r.run(1000).status == Status::Idle);
	r.eng.requestUndo();
	r.step();
	assert(r.eng.take().samples[10] == 1.f);
	assert(std::abs(r.eng.take().length - 200) <= 1);
}

static bool sameTake(const Take& a, const Take& b) {
	if (a.length != b.length || a.version != b.version || a.mode != b.mode)
		return false;
	return std::memcmp(a.samples.data(), b.samples.data(), sizeof(float) * a.length) == 0;
}

static void snapshotIsStable() {
	Rig r;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.source = 5;
	r.runTo(0.1);
	r.pressPunch();
	r.runTo(0.3);
	auto copy = std::make_unique<Take>();
	copy->copyFrom(r.eng.take());
	r.eng.requestSnapshot();
	r.step();
	assert(r.eng.snapshotReady());
	assert(sameTake(r.eng.snapshot(), *copy));
	r.runTo(0.4);
	assert(sameTake(r.eng.snapshot(), *copy));
	assert(!sameTake(r.eng.take(), *copy));
}

static void versionAndModified() {
	Rig r;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0, "SWOOP");
	assert(!r.eng.take().modified);
	uint32_t v0 = r.eng.take().version;
	r.c.source = 5;
	r.runTo(0.1);
	r.pressPunch();
	r.runTo(0.2);
	r.pressPunch();
	r.runTo(0.3);
	assert(r.eng.take().version != v0);
	assert(r.eng.take().modified);
	assert(std::strcmp(r.eng.take().name, "SWOOP") == 0);

	uint32_t v1 = r.eng.take().version;
	r.recordFree(1, 100);
	assert(r.eng.take().version != v1);
	assert(r.eng.take().modified);
	assert(r.eng.take().name[0] == 0);

	r.load(2.f, 100, TakeMode::Free, 1.0, 0.1, "AGAIN");
	assert(!r.eng.take().modified);
}

// Largest change in OUT from one sample to the next over [from, to) bars
static double maxStep(Rig& r, double to, LyrebirdStep prev) {
	double m = 0;
	while (r.bars() < to) {
		LyrebirdStep s = r.step();
		m = std::max(m, std::fabs(s.volts - prev.volts));
		prev = s;
	}
	return m;
}

static void punchEdgesAreSmooth() {
	// 5 V into 0 V, and a LOOP punch that runs past a whole loop
	for (double smooth : {0.0, 200.0, 1000.0}) {
		for (double end : {0.5, 1.5}) {
			Rig r;
			r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
			r.c.smoothMs = smooth;
			r.c.source = 5;
			LyrebirdStep prev = r.runTo(0.25);
			r.c.punch = true;
			double m = maxStep(r, 0.26, prev);
			r.c.punch = false;
			prev = r.runTo(end);
			r.c.punch = true;
			// On through the tail, the rebuild and the fade back to the take
			m = std::max(m, maxStep(r, end + 0.25, prev));
			r.c.punch = false;
			// Playback of the new take continues smoothly from there
			LyrebirdStep last = r.step();
			m = std::max(m, maxStep(r, end + 1.0, last));
			assert(m <= 0.05);
			// A punch over a whole loop leaves a take of nothing but the new value
			if (end > 1.0)
				assert(std::fabs(r.step().volts - 5.0) < 1e-3);
		}
	}
}

static void punchHoldMode() {
	Rig r;
	r.c.buttons = RecordButtons::Hold;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.source = 5;
	r.runTo(0.25);
	r.c.punch = true;
	LyrebirdStep s = r.runTo(0.5);
	assert(s.status == Status::Punching);
	r.c.punch = false;
	assert(r.step().status == Status::Idle);
	r.runTo(0.75);
	const Take& t = r.eng.take();
	assert(t.samples[400] == 0.f && t.samples[1200] == 0.f);
	assert(std::fabs(t.samples[750] - 5.f) < 1e-6);
}

static void punchMovementStart() {
	Rig r;
	r.c.startOn = StartOn::Movement;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.source = 0;
	r.runTo(0.25);
	assert(r.pressPunch().status == Status::PunchArmed);
	assert(r.runTo(0.3).status == Status::PunchArmed);
	r.c.source = 5;
	assert(r.step().status == Status::Punching);
	r.runTo(0.5);
	assert(r.pressPunch().status == Status::Idle);
	r.runTo(0.75);
	const Take& t = r.eng.take();
	assert(t.samples[560] == 0.f);
	assert(std::fabs(t.samples[800] - 5.f) < 1e-6);
}

static void undoDuringRecording() {
	Rig r;
	r.recordFree(1, 200);
	r.c.sync = false;
	r.c.source = 2;
	r.step();
	r.pressRec();
	r.run(4800);
	r.eng.requestUndo();
	assert(r.step().status == Status::Idle);
	assert(r.eng.take().samples[10] == 1.f);
	r.eng.requestUndo();
	r.step();
	// The interrupted take was kept, as far as it got
	assert(r.eng.take().samples[10] == 2.f);
	assert(std::abs(r.eng.take().length - 100) <= 1);
}

static void loadDuringPunchReverts() {
	Rig r;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.source = 5;
	r.runTo(0.25);
	r.pressPunch();
	r.runTo(0.4);
	r.fillSpare(3.f, 500, TakeMode::Free, 1.0, 0.5);
	r.eng.requestLoad();
	assert(r.step().status == Status::Idle);
	assert(r.eng.take().samples[10] == 3.f);
	r.eng.requestUndo();
	r.step();
	// Undo gives the take from before the punch, untouched
	const Take& t = r.eng.take();
	assert(t.length == 2000);
	for (int i = 0; i < 2000; i++)
		assert(t.samples[i] == 0.f);
}

static void snapshotDuringFreeRec() {
	Rig r;
	r.c.sync = false;
	r.c.source = 1;
	r.pressRec();
	r.run(24000);
	r.eng.requestSnapshot();
	assert(r.step().status == Status::Recording);
	const Take& s = r.eng.snapshot();
	assert(r.eng.snapshotReady());
	assert(std::abs(s.length - 500) <= 1);
	assert(std::fabs(s.seconds - s.length / 1000.0) < 1e-9);
}

static void traceWhileWriting() {
	Rig r;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.c.smoothMs = 200;
	r.c.scale = 0.5;
	r.c.offset = 1;
	r.c.source = 5;
	r.runTo(0.25);
	r.pressPunch();
	r.runTo(0.5);
	float raw[256], out[256];
	r.eng.renderTrace(r.c, raw, out, 256);
	for (int i = 0; i < 256; i++)
		assert(std::fabs(out[i] - (raw[i] * 0.5f + 1.f)) < 1e-5);
	// The written span already shows
	assert(std::fabs(raw[96] - 5.f) < 1e-5);
}

// Recording a Sync take into a buffer that held an older take: the trace shows
// only what has been written so far
static void syncRecordTraceSkipsUnwritten() {
	Rig r;
	r.load(9.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.load(8.f, 2000, TakeMode::Sync, 1.0, 2.0);
	// REC writes into the undo buffer, which now holds the 9 V take
	r.c.source = 1;
	r.step();
	r.pressRec();
	r.runTo(0.5);
	assert(r.eng.take().samples[1500] == 9.f);
	float raw[256], out[256];
	r.eng.renderTrace(r.c, raw, out, 256);
	int written = 0;
	for (int i = 0; i < 256; i++) {
		if (std::isfinite(raw[i])) {
			assert(std::fabs(raw[i] - 1.f) < 1e-5 && std::isfinite(out[i]));
			written++;
		}
		else
			assert(!std::isfinite(out[i]) && i > 120);
	}
	assert(written >= 125 && written <= 130);
	// Once it's finished, every point is real again
	r.runTo(1.1);
	assert(r.eng.take().samples[1500] == 1.f);
	r.eng.renderTrace(r.c, raw, out, 256);
	for (int i = 0; i < 256; i++)
		assert(std::isfinite(raw[i]) && std::isfinite(out[i]));
}

// Only takes REC and PUNCH finish themselves are counted, not writes a load or undo ends
static void countsFinishedTakes() {
	Rig r;
	r.recordFree(1, 200);
	assert(r.eng.recsFinished() == 1);
	r.c.lengthIndex = 0;  // 1/4 bar: stops by itself
	r.step();
	r.pressRec();
	r.runTo(r.bars() + 0.3);
	assert(r.eng.take().mode == TakeMode::Sync && r.eng.recsFinished() == 2);
	// Undo or load mid-take: no count
	r.c.sync = false;
	r.step();
	r.pressRec();
	r.run(4800);
	r.eng.requestUndo();
	r.step();
	r.pressRec();
	r.run(4800);
	r.load(3.f, 500, TakeMode::Free, 1.0, 0.5);
	assert(r.eng.recsFinished() == 2);
	// A punch counts when its tail has faded out
	r.c.source = 5;
	r.step();
	r.pressPunch();
	r.run(2400);
	r.pressPunch();
	assert(r.eng.punchesFinished() == 0);
	r.run(2400);
	assert(r.eng.punchesFinished() == 1);
	r.pressPunch();
	r.run(2400);
	r.load(2.f, 500, TakeMode::Free, 1.0, 0.5);
	assert(r.eng.punchesFinished() == 1 && r.eng.recsFinished() == 2);
}

// Two undo requests before the audio thread runs are two swaps, not one
static void undoRequestsAreCounted() {
	Rig r;
	r.recordFree(1, 200);
	r.recordFree(2, 300);
	r.eng.requestUndo();
	r.eng.requestUndo();
	r.step();
	assert(r.eng.take().samples[10] == 2.f);
	r.eng.requestUndo();
	r.eng.requestUndo();
	r.eng.requestUndo();
	r.step();
	assert(r.eng.take().samples[10] == 1.f);
}

// A load cancelling an armed PUNCH-as-REC hands PUNCH back to punching
static void loadClearsPunchOwnedRecord() {
	for (int viaUndo = 0; viaUndo < 2; viaUndo++) {
		Rig r;
		if (viaUndo)
			r.recordFree(1, 200);
		r.c.startOn = StartOn::Movement;
		if (viaUndo) {
			// Back to no take, then PUNCH arms a recording
			r.eng.requestUndo();
			r.step();
			assert(r.eng.take().empty());
		}
		assert(r.pressPunch().status == Status::Armed);
		if (viaUndo) {
			r.eng.requestUndo();
			r.step();
		}
		else
			r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
		assert(r.step().status == Status::Idle);
		r.c.startOn = StartOn::Press;
		r.run(100);
		assert(r.pressPunch().status == Status::Punching);
	}
}

// Changed take samples, counting from a take of all -1
static int changedSamples(const Take& t) {
	int n = 0;
	for (int i = 0; i < t.length; i++)
		if (std::fabs(t.samples[i] + 1.f) > 1e-3f)
			n++;
	return n;
}

// REVERSE, TRIG and a Sync SPEED change move the playhead in one jump: the
// punch writes where the playhead goes, not the samples it skipped
static void punchJumpsDontFill() {
	for (int kind = 0; kind < 4; kind++) {
		Rig r;
		r.load(-1.f, 2000, TakeMode::Sync, 1.0, 2.0);
		r.runTo(0.25);
		r.c.source = 5;
		r.pressPunch();
		r.run(48 * 20);
		if (kind == 0)
			r.c.reverse = true;
		else if (kind == 1)
			r.in.resetButton = true;
		else if (kind == 2)
			r.c.speedOct = 1;
		r.run(48 * 20);
		r.pressPunch();
		r.run(48 * 30);
		// About 20 + 20 (x2: 40) take samples, plus a 10 ms tail
		int n = changedSamples(r.eng.take());
		if (kind == 3)
			assert(n >= 45 && n <= 60);
		else
			assert(n >= 45 && n <= 90);
	}
}

// REC takes over from a punch, or from a punch waiting for movement
static void recDuringPunch() {
	for (int armed = 0; armed < 2; armed++) {
		for (int moveStart = 0; moveStart < 2; moveStart++) {
			Rig r;
			r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
			r.c.source = 1;
			r.runTo(0.25);
			r.c.startOn = armed ? StartOn::Movement : StartOn::Press;
			assert(r.pressPunch().status == (armed ? Status::PunchArmed : Status::Punching));
			r.run(480);
			r.c.startOn = moveStart ? StartOn::Movement : StartOn::Press;
			LyrebirdStep s = r.pressRec();
			assert(s.status == (moveStart ? Status::Armed : Status::Recording));
			if (moveStart) {
				r.c.source = 3;
				assert(r.step().status == Status::Recording);
			}
			// REC owns the stop
			r.runTo(0.5);
			assert(r.pressRec().status == Status::Idle);
			assert(r.eng.take().mode == TakeMode::Sync && r.eng.take().length == 2000);
		}
	}
}

// A ONCE pass ending mid-punch keeps the block it was averaging
static void punchEndKeepsLastBlock() {
	Rig r;
	r.c.loop = false;
	r.load(0.f, 2000, TakeMode::Sync, 1.0, 2.0);
	r.in.resetButton = true;
	r.step();
	r.c.source = 5;
	r.runTo(0.5);
	assert(r.pressPunch().status == Status::Punching);
	r.runTo(1.1);
	const Take& t = r.eng.take();
	assert(std::fabs(t.samples[1998] - 5.f) < 1e-6);
	assert(std::fabs(t.samples[1999] - 5.f) < 1e-6);
}

// No two takes the engine holds share a version, so a save can tell them apart
static void versionsUniqueAcrossBuffers() {
	Rig r;
	std::vector<uint32_t> seen;
	for (int i = 0; i < 6; i++) {
		r.recordFree(i + 1, 100);
		seen.push_back(r.eng.take().version);
		r.eng.requestUndo();
		r.step();
		seen.push_back(r.eng.take().version);
		r.eng.requestUndo();
		r.step();
	}
	// Each recorded take is new; the undo side is the take before it
	for (size_t i = 0; i < seen.size(); i += 2)
		for (size_t j = 0; j < i; j += 2)
			assert(seen[i] != seen[j]);
}

int main() {
	syncTakeStartsMidBarAndWraps();
	syncTakeStopsItself();
	syncTakeFullUnderTempoChange();
	syncEarlyStopHoldsLast();
	freeTakeToggleAndHold();
	freeTakeLimit();
	movementStartWaitsForMotion();
	punchReplacesOnlyItsSpan();
	punchFastReverseWritesContiguously();
	punchWithoutTakeRecords();
	undoSwapsBack();
	loadDuringRecordingAbandonsTake();
	snapshotIsStable();
	versionAndModified();
	punchEdgesAreSmooth();
	punchHoldMode();
	punchMovementStart();
	undoDuringRecording();
	loadDuringPunchReverts();
	snapshotDuringFreeRec();
	traceWhileWriting();
	syncRecordTraceSkipsUnwritten();
	countsFinishedTakes();
	undoRequestsAreCounted();
	loadClearsPunchOwnedRecord();
	punchJumpsDontFill();
	recDuringPunch();
	punchEndKeepsLastBlock();
	versionsUniqueAcrossBuffers();
	printf("lyrebird record tests passed\n");
	return 0;
}
