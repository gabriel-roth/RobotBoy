#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "OutputStage.hpp"

namespace shapes {

struct TimebaseInput {
	double bpm = 120.0;
	bool clockConnected = false;
	float clockVoltage = 0.f;
	float resetVoltage = 0.f;
	bool resetButton = false;
	int pulsesPerCycle = 4;
};

// Phase ramp for the shape modulators. One cycle is four beats. Runs from a
// BPM setting, or from an external clock whose pulses measure the tempo and
// then gently pull the phase into line.
class Timebase {
public:
	struct Step {
		double pos;  // 0..1 within the cycle
		int64_t cycle;  // whole cycles since reset
		bool wrapped;  // crossed a cycle boundary this sample (never on reset)
		bool reset;
	};

	void setSampleRate(double sr) {
		sampleRate = sr > 1.0 ? sr : 1.0;
	}

	Step process(const TimebaseInput& in) {
		Step s{pos, cycle, false, false};
		int ppc = std::max(in.pulsesPerCycle, 1);

		// Both triggers must see every sample, so no short-circuit
		bool jackEdge = resetGate.process(in.resetVoltage);
		bool buttonEdge = resetBtn.process(in.resetButton ? 10.f : 0.f);
		bool resetEdge = jackEdge || buttonEdge;
		if (resetEdge) {
			pos = 0.0;
			cycle = 0;
			pulseIndex = 0;
			havePrev = false;  // next pulse aligns hard
			s = {0.0, 0, false, true};
		}

		if (!in.clockConnected && wasConnected) {
			locked = false;
			havePrev = false;
			measured = false;
		}
		// The BPM setting runs the ramp until a clock has given a tempo
		if (!measured) {
			// Clamped so pos can never advance by a whole cycle per sample
			tempo = std::clamp(shapes::sanitize(in.bpm, 120.0), 20.0, 300.0) / 240.0;
			rate = tempo;
		}
		wasConnected = in.clockConnected;

		sampleCount++;
		bool pulse = clockGate.process(in.clockConnected ? in.clockVoltage : 0.f);
		bool alignWrap = false;
		if (pulse && in.clockConnected)
			alignWrap = onPulse(ppc);

		if (resetEdge)
			return s;

		rate = shapes::sanitize(rate, 0.5);
		pos += rate / sampleRate;
		// Tolerance so a cycle of exactly N samples wraps on its last sample
		if (pos >= 1.0 - 1e-9) {
			pos -= std::floor(pos + 1e-9);
			pos = std::max(pos, 0.0);
			cycle++;
			s.wrapped = true;
		}
		// A hard align can cross the boundary too; still one wrap per boundary
		s.wrapped = s.wrapped || alignWrap;
		s.pos = pos;
		s.cycle = cycle;
		return s;
	}

	double tempoBpm() const {
		return tempo * 240.0;
	}

	// Cycles since reset
	double position() const {
		return (double) cycle + pos;
	}

	void reset() {
		pos = 0.0;
		cycle = 0;
		pulseIndex = 0;
		havePrev = false;
	}

private:
	struct Schmitt {
		bool high = false;
		bool process(float v) {
			if (high) {
				if (v <= 0.1f)
					high = false;
			}
			else if (v >= 1.f) {
				high = true;
				return true;
			}
			return false;
		}
	};

	// Hard align. A forward jump onto the cycle start counts as the wrap;
	// a backward jump doesn't, so one boundary never reports twice.
	bool alignTo(double target) {
		bool w = target == 0.0 && pos > 0.5;
		if (w)
			cycle++;
		pos = target;
		return w;
	}

	// Returns true when a hard align wrapped the cycle.
	bool onPulse(int ppc) {
		double interval = (double) (sampleCount - lastPulseSample) / sampleRate;
		lastPulseSample = sampleCount;
		// A gap much longer than the current pulse spacing means the clock
		// stopped; don't read a tempo from it
		bool gap = locked && interval * tempo * ppc > 2.0;
		if (!havePrev || gap) {
			pulseIndex = 0;
			havePrev = true;
			locked = false;
			return alignTo(0.0);
		}
		double cycleLen = std::clamp(interval * ppc, 0.05, 60.0);
		pulseIndex = (pulseIndex + 1) % ppc;
		double expected = (double) pulseIndex / ppc;
		if (!locked) {
			// Second pulse: take the tempo and align hard
			tempo = 1.0 / cycleLen;
			rate = tempo;
			locked = true;
			measured = true;
			return alignTo(expected);
		}
		double err = expected - pos;
		err -= std::round(err);  // wrapped to -0.5..0.5
		// Error is in cycles; ppc * err is in pulses
		double nudge = std::clamp(1.0 + kLockGain * err * ppc, 0.5, 1.5);
		tempo = 1.0 / cycleLen;
		rate = std::clamp(nudge * tempo, 1.0 / 60.0, 20.0);
		return false;
	}

	static constexpr double kLockGain = 0.1;

	double sampleRate = 48000.0;
	double pos = 0.0;
	double rate = 0.5;  // cycles per second, including the lock nudge
	double tempo = 0.5;  // measured cycles per second, for display
	int64_t cycle = 0;
	int64_t sampleCount = 0;
	int64_t lastPulseSample = 0;
	int pulseIndex = 0;
	bool havePrev = false;
	bool locked = false;
	bool measured = false;  // a clock tempo has been read since connecting
	bool wasConnected = false;
	Schmitt clockGate, resetGate, resetBtn;
};

}  // namespace shapes
