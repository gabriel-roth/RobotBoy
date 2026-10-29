// Timebase tests: BPM running, reset, external clock lock, tempo clamping.
#include "shapes/Timebase.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace shapes;

static constexpr double kSr = 48000.0;

static TimebaseInput bpmInput(double bpm) {
	TimebaseInput in{};
	in.bpm = bpm;
	in.pulsesPerCycle = 4;
	return in;
}

static void bpm120CycleIsTwoSeconds() {
	Timebase tb;
	tb.setSampleRate(kSr);
	TimebaseInput in = bpmInput(120.0);
	int wraps = 0;
	Timebase::Step s{};
	for (int i = 0; i < 96000; i++) {
		s = tb.process(in);
		if (s.wrapped)
			wraps++;
	}
	assert(wraps == 1);
	assert(s.cycle == 1);
	assert(std::fabs(tb.tempoBpm() - 120.0) < 1e-9);
}

static void resetRestartsWithoutWrap() {
	Timebase tb;
	tb.setSampleRate(kSr);
	TimebaseInput in = bpmInput(120.0);
	for (int i = 0; i < 72000; i++)
		tb.process(in);
	assert(std::fabs(tb.position() - 0.75) < 1e-3);
	in.resetButton = true;
	Timebase::Step s = tb.process(in);
	assert(s.reset && !s.wrapped);
	assert(s.pos == 0.0 && s.cycle == 0);
	assert(tb.position() == 0.0);
	// Held button is one edge only
	s = tb.process(in);
	assert(!s.reset);
	// Voltage reset uses a Schmitt trigger
	in.resetButton = false;
	in.resetVoltage = 5.f;
	s = tb.process(in);
	assert(s.reset);
	in.resetVoltage = 0.5f;  // between thresholds: still armed-off
	s = tb.process(in);
	assert(!s.reset);
}

// Feeds 1 ms-wide pulses every `periodSamples` for `n` samples.
struct ClockRun {
	std::vector<int> pulseAt, wrapAt;
};

static ClockRun runClock(Timebase& tb, int ppc, int periodSamples, int totalSamples, int startSample = 0) {
	ClockRun r;
	TimebaseInput in = bpmInput(120.0);
	in.clockConnected = true;
	in.pulsesPerCycle = ppc;
	for (int i = 0; i < totalSamples; i++) {
		int k = i - startSample;
		bool high = k >= 0 && (k % periodSamples) < 48;
		if (k >= 0 && (k % periodSamples) == 0)
			r.pulseAt.push_back(i);
		in.clockVoltage = high ? 10.f : 0.f;
		Timebase::Step s = tb.process(in);
		if (s.wrapped)
			r.wrapAt.push_back(i);
	}
	return r;
}

static void clockSetsTempo() {
	Timebase tb;
	tb.setSampleRate(kSr);
	ClockRun r = runClock(tb, 4, 12000, 4 * 48000, 100);
	assert(std::fabs(tb.tempoBpm() - 240.0) < 0.5);
	// After lock, wraps land on every 4th pulse
	int checked = 0;
	for (size_t p = 8; p < r.pulseAt.size(); p++) {
		if (p % 4 != 0)
			continue;
		bool found = false;
		for (int w : r.wrapAt)
			if (std::abs(w - r.pulseAt[p]) <= 1)
				found = true;
		assert(found);
		checked++;
	}
	assert(checked >= 1);
}

static void clockStopKeepsLastTempo() {
	Timebase tb;
	tb.setSampleRate(kSr);
	runClock(tb, 4, 12000, 2 * 48000);
	double tempo = tb.tempoBpm();
	double p0 = tb.position();
	// clock stays connected but silent
	TimebaseInput in = bpmInput(60.0);
	in.clockConnected = true;
	for (int i = 0; i < 3 * 48000; i++)
		tb.process(in);
	assert(std::fabs(tb.tempoBpm() - tempo) < 1e-9);
	assert(tb.position() > p0 + 1.0);
}

static void clockTempoClamped() {
	Timebase tb;
	tb.setSampleRate(kSr);
	// 1 ppc, pulses 10 s apart: cycle length would be 10 s (fine) -- go slower than 60 s via 100 s
	runClock(tb, 1, 100 * 48000, 250 * 48000);
	double cycleLen = 240.0 / tb.tempoBpm();
	assert(cycleLen <= 60.0 + 1e-9);
	assert(std::isfinite(tb.tempoBpm()) && std::isfinite(tb.position()));

	Timebase fast;
	fast.setSampleRate(kSr);
	// 1 ms apart (48 samples), pulse width 24 samples
	TimebaseInput in = bpmInput(120.0);
	in.clockConnected = true;
	in.pulsesPerCycle = 1;
	for (int i = 0; i < 4800; i++) {
		in.clockVoltage = (i % 48) < 24 ? 10.f : 0.f;
		Timebase::Step s = fast.process(in);
		assert(std::isfinite(s.pos) && s.pos >= 0.0 && s.pos < 1.0);
	}
	assert(240.0 / fast.tempoBpm() >= 0.05 - 1e-9);
	assert(std::isfinite(fast.tempoBpm()));
}

static void resetThenClockAlignsHard() {
	Timebase tb;
	tb.setSampleRate(kSr);
	runClock(tb, 4, 12000, 3 * 48000);
	TimebaseInput in = bpmInput(120.0);
	in.clockConnected = true;
	in.resetButton = true;
	tb.process(in);
	in.resetButton = false;
	for (int i = 0; i < 5000; i++)
		tb.process(in);
	in.clockVoltage = 10.f;
	Timebase::Step s = tb.process(in);
	assert(s.pos < 1e-3 && !s.wrapped);  // aligned to 0, then one sample of advance
}

// Pulse k arrives at times[k] samples; returns per-pulse phase error and max per-sample jump.
struct LockRun {
	std::vector<double> err;  // phase minus expected, in cycles, at each pulse
	double maxJump = 0.0;  // largest per-sample pos delta (mod 1)
	double tempo = 0.0;
};

static LockRun runPulses(const std::vector<int>& times, int ppc, int total) {
	Timebase tb;
	tb.setSampleRate(kSr);
	TimebaseInput in = bpmInput(120.0);
	in.clockConnected = true;
	in.pulsesPerCycle = ppc;
	LockRun r;
	size_t next = 0;
	double prev = 0.0;
	for (int i = 0; i < total; i++) {
		bool pulseNow = next < times.size() && i >= times[next] && i < times[next] + 48;
		in.clockVoltage = pulseNow ? 10.f : 0.f;
		Timebase::Step s = tb.process(in);
		if (next < times.size() && i == times[next]) {
			if (next >= 2) {
				double e = s.pos - std::fmod((double) next, ppc) / ppc;
				e -= std::round(e);
				r.err.push_back(e);
			}
			next++;
		}
		if (next > 3) {
			double d = s.pos - prev;
			d -= std::floor(d + 0.5);
			r.maxJump = std::max(r.maxJump, std::fabs(d));
		}
		prev = s.pos;
	}
	r.tempo = tb.tempoBpm();
	return r;
}

static void softLockConvergesWithoutJumps() {
	const double nominal = 1.0 / 48000.0;  // 1 cycle/s at 48 kHz
	// A constant offset: pulses 600 samples late from pulse 10 on
	std::vector<int> t;
	for (int k = 0; k < 60; k++)
		t.push_back(1000 + k * 12000 + (k >= 10 ? 600 : 0));
	LockRun r = runPulses(t, 4, 1000 + 60 * 12000);
	assert(r.maxJump < 2.0 * nominal);
	// Late pulses find the phase ahead of expectation (positive), then it shrinks
	double first = r.err[10 - 2];
	assert(first > 0.005);
	double last = r.err.back();
	assert(std::fabs(last) < 0.1 * std::fabs(first));
	assert(std::fabs(r.tempo - 240.0) < 1.0);

	// Alternating early/late pulses (3%): no jumps, tempo stays near
	std::vector<int> j;
	int at = 1000;
	for (int k = 0; k < 60; k++) {
		j.push_back(at);
		at += 12000 + ((k % 2) ? 360 : -360);
	}
	LockRun q = runPulses(j, 4, at);
	assert(q.maxJump < 2.0 * 1.04 * nominal);
	for (size_t i = 10; i < q.err.size(); i++)
		assert(std::fabs(q.err[i]) < 0.02);
}

static void hardAlignWrapsOnce() {
	// 200 BPM: pos is 0.83 at the second pulse (ppc 1), which aligns forward onto the boundary
	{
		Timebase tb;
		tb.setSampleRate(kSr);
		TimebaseInput in = bpmInput(200.0);
		in.clockConnected = true;
		in.pulsesPerCycle = 1;
		int wraps = 0;
		for (int i = 0; i < 72000; i++) {
			in.clockVoltage = ((i % 48000) < 48) ? 10.f : 0.f;
			Timebase::Step s = tb.process(in);
			if (s.wrapped)
				wraps++;
		}
		assert(wraps == 1);
		assert(std::fabs(tb.position() - 1.5) < 0.02);
	}
	// 60 BPM: second pulse aligns backward (pos 0.25 -> 0), no wrap
	{
		Timebase tb;
		tb.setSampleRate(kSr);
		TimebaseInput in = bpmInput(60.0);
		in.clockConnected = true;
		in.pulsesPerCycle = 1;
		int wraps = 0;
		for (int i = 0; i < 72000; i++) {
			in.clockVoltage = ((i % 48000) < 48) ? 10.f : 0.f;
			if (tb.process(in).wrapped)
				wraps++;
		}
		assert(wraps == 0);
	}
}

static void clockGapReacquires() {
	Timebase tb;
	tb.setSampleRate(kSr);
	runClock(tb, 4, 12000, 2 * 48000);
	TimebaseInput in = bpmInput(120.0);
	in.clockConnected = true;
	for (int i = 0; i < 5 * 48000; i++)
		tb.process(in);
	in.clockVoltage = 10.f;
	Timebase::Step s = tb.process(in);
	assert(s.pos < 1e-3 || s.pos > 0.999);  // aligned, not stepped forward
	assert(std::fabs(tb.tempoBpm() - 240.0) < 0.5);  // gap didn't set tempo
	in.clockVoltage = 0.f;
	for (int i = 0; i < 12000 - 1; i++)
		tb.process(in);
	in.clockVoltage = 10.f;
	tb.process(in);
	assert(std::fabs(tb.tempoBpm() - 240.0) < 0.5);
}

// A swung clock (alternating 60% / 40% of the beat): the tempo is the
// average over a cycle of pulses, so it holds steady instead of jumping
// between the long and short intervals.
static void swungClockTempoIsSteady() {
	Timebase tb;
	tb.setSampleRate(kSr);
	TimebaseInput in = bpmInput(120.0);
	in.clockConnected = true;
	in.pulsesPerCycle = 4;
	std::vector<int> times;
	int at = 1000;
	for (int k = 0; k < 40; k++) {
		times.push_back(at);
		at += (k % 2) ? 9600 : 14400;  // average 12000: 240 BPM at 4 ppc
	}
	size_t next = 0;
	int measuredPulses = 0;
	double lo = 1e9, hi = 0.0;
	for (int i = 0; i < at; i++) {
		bool high = next < times.size() && i >= times[next] && i < times[next] + 48;
		in.clockVoltage = high ? 10.f : 0.f;
		tb.process(in);
		if (next < times.size() && i == times[next] + 1) {
			// Once a full cycle of intervals has been heard
			if (next >= 5) {
				lo = std::min(lo, tb.tempoBpm());
				hi = std::max(hi, tb.tempoBpm());
				measuredPulses++;
			}
			next++;
		}
	}
	assert(measuredPulses > 20);
	assert(lo >= 240.0 * 0.99 && hi <= 240.0 * 1.01);
}

static void bpmInputIsClamped() {
	Timebase tb;
	tb.setSampleRate(kSr);
	TimebaseInput in = bpmInput(1e9);
	for (int i = 0; i < 1000; i++) {
		Timebase::Step s = tb.process(in);
		assert(s.pos >= 0.0 && s.pos < 1.0);
	}
	in.bpm = std::nan("");
	Timebase::Step s = tb.process(in);
	assert(std::isfinite(s.pos) && std::fabs(tb.tempoBpm() - 120.0) < 1e-9);
}

int main() {
	bpm120CycleIsTwoSeconds();
	resetRestartsWithoutWrap();
	clockSetsTempo();
	clockStopKeepsLastTempo();
	clockTempoClamped();
	resetThenClockAlignsHard();
	softLockConvergesWithoutJumps();
	hardAlignWrapsOnce();
	clockGapReacquires();
	swungClockTempoIsSteady();
	bpmInputIsClamped();
	printf("test_timebase OK\n");
	return 0;
}
