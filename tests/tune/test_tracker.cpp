// Tune's pitch tracker (src/tune/FrequencyTracker.hpp) against signals of
// known pitch: sines, band-limited and naive saws, squares, and an LFO.
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

// Stand-in for rack::simd::float_4: the tracker only loads, multiplies,
// accumulates and reads lanes.
namespace rack {
namespace simd {
struct float_4 {
	float v[4];
	float_4() : v{0.f, 0.f, 0.f, 0.f} {}
	explicit float_4(float x) : v{x, x, x, x} {}
	static float_4 load(const float* p) {
		float_4 r;
		for (int i = 0; i < 4; i++)
			r.v[i] = p[i];
		return r;
	}
	float operator[](int i) const { return v[i]; }
	float_4 operator*(const float_4& o) const {
		float_4 r;
		for (int i = 0; i < 4; i++)
			r.v[i] = v[i] * o.v[i];
		return r;
	}
	float_4& operator+=(const float_4& o) {
		for (int i = 0; i < 4; i++)
			v[i] += o.v[i];
		return *this;
	}
};
}  // namespace simd
}  // namespace rack

#define TUNE_TRACKER_NO_RACK
#include "tune/FrequencyTracker.hpp"

enum Shape { Sine, Saw, BandLimitedSaw, Square };
static const char* const kShapeNames[] = {"sine", "saw", "band-limited saw", "square"};
static const double kRate = 48000.0;

// White noise, +-1, the same every run
static float noise() {
	static unsigned state = 12345u;
	state = state * 1664525u + 1013904223u;
	return (float) (state >> 8) / 8388608.f - 1.f;
}

static float sample(Shape shape, double hz, long n) {
	double t = n / kRate;
	double phase = hz * t - std::floor(hz * t);
	switch (shape) {
		case Sine: return 5.f * (float) std::sin(2.0 * M_PI * phase);
		case Saw: return 5.f * (float) (2.0 * phase - 1.0);
		case Square: return phase < 0.5 ? 5.f : -5.f;
		case BandLimitedSaw: {
			double sum = 0.0;
			for (int k = 1; k * hz < kRate / 2.0; k++)
				sum += std::sin(2.0 * M_PI * k * phase) / k;
			return 5.f * (float) (sum * 2.0 / M_PI);
		}
	}
	return 0.f;
}

// The frequency the tracker settles on, as the module runs it (the -0.05 V
// offset, 8 lags per call, Balanced speed), averaged over the last half second.
static double track(Shape shape, double hz, double seconds, float noiseVolts = 0.f) {
	DualRangeTracker tracker;
	tracker.setSampleRate((float) kRate);
	tracker.setLagsPerCall(8);
	tracker.setAnalysisSkip(1);
	long total = (long) (seconds * kRate), tail = (long) (0.5 * kRate);
	double sum = 0.0;
	long count = 0;
	for (long n = 0; n < total; n++) {
		float out = tracker.process(sample(shape, hz, n) + noiseVolts * noise() - 0.05f);
		if (n >= total - tail) {
			if (out <= 0.f)
				return -1.0;
			sum += out;
			count++;
		}
	}
	return sum / count;
}

static int failures = 0;

static void expectWithin(Shape shape, double hz, double maxCents, double seconds = 2.0, float noiseVolts = 0.f) {
	double got = track(shape, hz, seconds, noiseVolts);
	double cents = got > 0.0 ? 1200.0 * std::log2(got / hz) : 1e9;
	bool ok = std::fabs(cents) <= maxCents;
	std::printf("%s %-16s %8.2f Hz -> %9.2f Hz  %+8.2f cents (limit %.1f)\n",
		ok ? "ok  " : "FAIL", kShapeNames[shape], hz, got, cents, maxCents);
	if (!ok)
		failures++;
}

int main() {
	// Sines were always close below 1 kHz
	expectWithin(Sine, 110.0, 0.5);
	expectWithin(Sine, 440.0, 0.5);
	// Short periods and bright waveforms: one autocorrelation peak isn't
	// precise enough, the later peaks are
	expectWithin(Sine, 1760.0, 1.0);
	expectWithin(Sine, 3520.0, 1.0);
	expectWithin(Sine, 4186.01, 1.0);
	expectWithin(Saw, 55.0, 1.0);
	expectWithin(Saw, 220.0, 1.0);
	expectWithin(Saw, 440.0, 1.0);
	expectWithin(Saw, 880.0, 1.0);
	expectWithin(Saw, 1760.0, 1.0);
	expectWithin(Saw, 3520.0, 1.0);
	expectWithin(BandLimitedSaw, 466.16, 1.0);
	expectWithin(BandLimitedSaw, 2093.0, 1.0);
	expectWithin(Square, 440.0, 1.0);
	expectWithin(Square, 1244.51, 1.0);
	// The top octave, which needs the high tracker: up to C8 and a little over
	expectWithin(Saw, 3445.57, 1.0);
	expectWithin(BandLimitedSaw, 3951.07, 1.0);
	expectWithin(Saw, 4186.01, 1.0);
	expectWithin(Square, 4186.01, 1.0);
	expectWithin(Sine, 4358.73, 1.0);
	expectWithin(Saw, 4500.0, 1.0);
	expectWithin(Sine, 4500.0, 1.0, 2.0, 0.02f);
	// An LFO: the audio tracker's alias lock mustn't win over the LFO tracker
	expectWithin(Sine, 2.0, 5.0, 12.0);
	expectWithin(Saw, 5.0, 5.0, 12.0);
	// Between the two trackers' ranges
	expectWithin(Saw, 12.0, 5.0, 12.0);
	// With a little noise, as from real hardware
	expectWithin(Sine, 2.0, 5.0, 12.0, 0.02f);
	expectWithin(Saw, 5.0, 5.0, 12.0, 0.02f);
	expectWithin(Saw, 440.0, 1.0, 2.0, 0.02f);
	expectWithin(Sine, 3520.0, 1.0, 2.0, 0.02f);

	if (failures) {
		std::printf("%d failed\n", failures);
		return 1;
	}
	std::printf("all passed\n");
	return 0;
}
