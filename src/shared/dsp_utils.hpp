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

} // namespace robotboy
