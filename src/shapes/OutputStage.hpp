#pragma once
#include <algorithm>
#include <cmath>

namespace shapes {

enum class Range { Bipolar5, Unipolar10 };
enum class Treat { Fit, Clip, Soft, Fold, Wrap };

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
