#pragma once
#include <cmath>
#include <numbers>

// Small Rack-free DSP pieces shared by the filters and the loopers (and by
// the MetaModule Loooop/Löp cores, which don't link Rack).
namespace robotboy {

// One-pole smoother (IIR lowpass for zipper-noise suppression).
struct OnePoleSmoother {
	float value;
	float alpha;
	OnePoleSmoother(float init = 0.f, float a = 0.0045f) : value(init), alpha(a) {}
	void setAlpha(float a) { alpha = a; }
	float process(float target) { value += alpha * (target - value); return value; }
	void reset(float v) { value = v; }
};

// One-pole alpha for a time constant (seconds); saturates to 1 at low rates.
inline float smootherAlpha(float sampleRate, float tauSec) {
	return 1.f - std::exp(-1.f / (tauSec * sampleRate));
}

// One-pole DC blocker: subtracts a lowpassed copy of the signal.
struct DcBlocker {
	float state = 0.f;
	float a = 0.001f;
	void setCutoff(float hz, float sampleRate) { a = 1.f - std::exp(-2.f * std::numbers::pi_v<float> * hz / sampleRate); }
	void reset() { state = 0.f; }
	float process(float x) { float y = x - state; state += a * y; return y; }
};

// Output saturation toward the Eurorack ceiling (+/-12 V rails less the
// protection-diode drop, per VCV's voltage standards): identity up to
// +/-8 V, a tanh knee approaching +/-11.7 V beyond. Normal levels pass
// untouched; only hot, high-drive or high-resonance peaks are rounded off.
inline float outputSaturate(float v) {
	constexpr float knee = 8.f, w = 11.7f - knee, invW = 1.f / w;
	// Rational tanh, exact +/-1 beyond |x| = 3 (monotone, so the knee is too).
	auto tanhKnee = [](float x) {
		if (x > 3.f) return 1.f;
		float x2 = x * x;
		return x * (27.f + x2) / (27.f + 9.f * x2);
	};
	if (v >  knee) return  knee + w * tanhKnee((v - knee) * invW);
	if (v < -knee) return -knee - w * tanhKnee((-knee - v) * invW);
	return v;
}

} // namespace robotboy
