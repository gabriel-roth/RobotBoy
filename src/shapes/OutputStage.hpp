#pragma once
#include <algorithm>
#include <cmath>

namespace shapes {

enum class Range { Bipolar5, Unipolar10 };
enum class Treat { Fit, Clip, Soft, Fold, Wrap };

// A coefficient that follows a moving control (a slew time) is recomputed at
// most this often, in calls, so a jittering knob doesn't cost a libm call
// every time
constexpr int kCoefInterval = 32;

// The modules work out their outputs every this many samples and ramp in
// between (Ramp): a modulator doesn't need more, and it costs a fraction.
// An instant jump becomes a ramp this long, and outputs lag by one less.
constexpr int kControlInterval = 8;

inline double sanitize(double x, double fallback) {
	return std::isfinite(x) ? x : fallback;
}

inline double frac(double x) {
	return x - std::floor(x);
}

// Maps an out-of-range value (nominally -1..1) back into range.
// Fit scales by the cycle's peak, but only when that peak exceeds 1.
inline double treat(double x, Treat mode, double peak) {
	switch (mode) {
		case Treat::Fit:
			return peak > 1.0 ? x / peak : x;
		case Treat::Clip:
			return std::clamp(x, -1.0, 1.0);
		case Treat::Soft:
			return std::clamp(std::tanh(1.5 * x) / std::tanh(1.5), -1.0, 1.0);
		case Treat::Fold: {
			double u = frac((x + 1.0) / 4.0);
			return u < 0.5 ? u * 4.0 - 1.0 : 3.0 - u * 4.0;
		}
		case Treat::Wrap:
			return frac((x + 1.0) / 2.0) * 2.0 - 1.0;
	}
	return x;
}

inline double toVolts(double x, Range range) {
	return range == Range::Bipolar5 ? 5.0 * x : 5.0 * (x + 1.0);
}

// Mirrors about the centre of the range.
inline double invertVolts(double v, Range range) {
	return range == Range::Bipolar5 ? -v : 10.0 - v;
}

// One-pole lowpass; 0 ms passes the input through.
class Slew {
public:
	void set(double ms, double sampleRate) {
		double tau = ms * 0.001 * sampleRate;  // in samples
		coef = tau > 0.0 ? 1.0 - std::exp(-1.0 / tau) : 1.0;
	}

	// Callers must pass finite values; a NaN would stick in the filter state.
	double process(double v) {
		if (coef >= 1.0) {
			y = v;
			return v;
		}
		y += (v - y) * coef;
		return y;
	}

	void reset(double v) {
		y = v;
	}

private:
	double coef = 1.0;
	double y = 0.0;
};

// Steps from one control-rate value to the next: target() starts a ramp
// that reaches v after n calls to next().
class Ramp {
public:
	void target(double v, int n) {
		to = v;
		left = n > 0 ? n : 1;
		step = (v - value) / left;
	}

	// Lands on v at once
	void jump(double v) {
		value = to = v;
		left = 0;
	}

	double next() {
		if (left > 0)
			value = --left == 0 ? to : value + step;
		return value;
	}

private:
	double value = 0.0;
	double to = 0.0;
	double step = 0.0;
	int left = 0;
};

// 10 V pulse lasting 1 ms; re-firing restarts it.
class Trigger {
public:
	void fire() {
		remaining = 0.001;
	}

	float process(double sampleTime) {
		if (remaining <= 0.0)
			return 0.f;
		remaining -= sampleTime;
		return 10.f;
	}

private:
	double remaining = 0.0;
};

}  // namespace shapes
