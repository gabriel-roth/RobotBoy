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

		bool resetEdge = resetGate.process(in.resetVoltage) | resetBtn.process(in.resetButton ? 10.f : 0.f);
		if (resetEdge) {
			pos = 0.0;
			cycle = 0;
			pulseIndex = 0;
			havePrev = false;  // next pulse aligns hard
			s = {0.0, 0, false, true};
		}

		if (!in.clockConnected) {
			if (wasConnected) {
				locked = false;
				havePrev = false;
			}
			rate = in.bpm / 240.0;
		}
		wasConnected = in.clockConnected;

		sampleCount++;
		bool pulse = clockGate.process(in.clockConnected ? in.clockVoltage : 0.f);
		if (pulse && in.clockConnected)
			onPulse(ppc);

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
		s.pos = pos;
		s.cycle = cycle;
		return s;
	}

	double tempoBpm() const {
		return rate * 240.0;
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

	void onPulse(int ppc) {
		if (!havePrev) {
			// First pulse after connecting or resetting: align hard
			pos = 0.0;
			pulseIndex = 0;
			havePrev = true;
			lastPulseSample = sampleCount;
			return;
		}
		double interval = (double) (sampleCount - lastPulseSample) / sampleRate;
		lastPulseSample = sampleCount;
		double cycleLen = std::clamp(interval * ppc, 0.05, 60.0);
		pulseIndex = (pulseIndex + 1) % ppc;
		double expected = (double) pulseIndex / ppc;
		if (!locked) {
			// Second pulse: take the tempo and align hard
			rate = 1.0 / cycleLen;
			pos = expected;
			locked = true;
			return;
		}
		double err = expected - pos;
		err -= std::round(err);  // wrapped to -0.5..0.5
		// Error is in cycles; ppc * err is in pulses
		double nudge = std::clamp(1.0 + kLockGain * err * ppc, 0.5, 1.5);
		rate = std::clamp(nudge / cycleLen, 1.0 / 60.0, 20.0);
	}

	static constexpr double kLockGain = 0.1;

	double sampleRate = 48000.0;
	double pos = 0.0;
	double rate = 0.5;  // cycles per second
	int64_t cycle = 0;
	int64_t sampleCount = 0;
	int64_t lastPulseSample = 0;
	int pulseIndex = 0;
	bool havePrev = false;
	bool locked = false;
	bool wasConnected = false;
	Schmitt clockGate, resetGate, resetBtn;
};

}  // namespace shapes
