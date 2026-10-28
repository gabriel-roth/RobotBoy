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

int main() {
	bpm120CycleIsTwoSeconds();
	resetRestartsWithoutWrap();
	clockSetsTempo();
	clockStopKeepsLastTempo();
	clockTempoClamped();
	resetThenClockAlignsHard();
	printf("test_timebase OK\n");
	return 0;
}
