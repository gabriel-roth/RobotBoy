#pragma once
// Per-voice DSP state for Onbetap polyphony, the decimation FIRs, and the
// Vintage drift walker.
#include "OnbetapFilter.hpp"
#include "../shared/dsp_utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace onbetap {

// Decimation lowpass FIR, run at the oversampled rate ahead of
// downsampling. push() is called once per oversampled substep; only the
// value returned on a decimation instant is kept, and pushHistory() advances
// the history on substeps whose output would be discarded. The history is a
// ring buffer, walked newest to oldest.
template <int N, const float (&TAPS)[N]>
struct DecimFir {
	static constexpr const float (&h)[N] = TAPS;
	float z[N] = {};
	int head = 0;

	void reset() {
		for (float& v : z)
			v = 0.f;
		head = 0;
	}
	void pushHistory(float x) {
		head = (head == 0) ? N - 1 : head - 1;
		z[head] = x;
	}
	float push(float x) {
		pushHistory(x);
		float y = 0.f;
		int i = 0;
		for (int idx = head; idx < N; ++idx, ++i)
			y += h[i] * z[idx];
		for (int idx = 0; i < N; ++idx, ++i)
			y += h[i] * z[idx];
		return y;
	}
};

// 13 taps at 96 kHz (2x oversampling of 48 kHz): Kaiser-windowed (scipy
// firwin, cutoff 23.5 kHz, beta 1.0), tuned for passband flatness rather
// than maximum stopband, since the linear-interpolation upsampler ahead of
// it already droops ~1.4 dB near 18-20 kHz. Combined droop is ~0.7 dB at
// 18 kHz. A 7-tap halfband is pinned to -6 dB at fs/4 and droops more.
inline constexpr float kDecim13Taps[13] = {
	0.0078065f,  0.0510650f, -0.0089609f, -0.0952920f,  0.0096953f,
	0.3019243f,  0.4675237f,  0.3019243f,  0.0096953f, -0.0952920f,
	-0.0089609f,  0.0510650f,  0.0078065f
};
using DecimFir13 = DecimFir<13, kDecim13Taps>;

// 9-tap stage A for the 4x path, at 192 kHz (48 kHz host), decimating to
// 96 kHz ahead of the same DecimFir13 stage the 2x path uses, so the 4x
// passband matches 2x by construction. After both decimations only content
// at 72-96 kHz folds into the audible band (48-72 kHz folds to 24-48 kHz,
// which stage B removes), so stage A needs strong attenuation only above
// 72 kHz. scipy.signal.firwin(9, 45500, window=("kaiser", 3.25),
// fs=192000): passband deviation <= 0.096 dB to 20 kHz, stopband <= -41 dB
// over 72-96 kHz. Group delay 4 samples at 192 kHz = 1 host sample.
inline constexpr float kDecim9Taps[9] = {
	-0.0042816f, -0.0436724f, 0.0182510f, 0.2921273f, 0.4751513f,
	0.2921273f,  0.0182510f, -0.0436724f, -0.0042816f
};
using DecimFir9 = DecimFir<9, kDecim9Taps>;

// One stereo side's per-voice state beyond the filter core.
struct Side {
	OnbetapFilter filter;
	float xPrev = 0.f;   // previous host-rate input, for the interpolating upsampler
	robotboy::DcBlocker dc;
	DecimFir13 firLp, firBp, firHp;      // 2x decimators; stage B on the 4x path
	DecimFir9 fir4Lp, fir4Bp, fir4Hp;    // stage A, 4x path only

	void resetSignal() {
		filter.reset();
		dc.reset();
		firLp.reset(); firBp.reset(); firHp.reset();
		fir4Lp.reset(); fir4Bp.reset(); fir4Hp.reset();
	}
};

// One polyphonic voice: both stereo sides plus the smoothers. Cutoff
// smoothing happens in the g (prewarp-gain) domain: modulate() computes
// tan/exp2/pow at ~2.5 ms intervals; the audio path only slews.
struct OnbetapVoice {
	// Defaults ≈ 750 Hz at 96 kHz (2x oversampling of 48 kHz); corrected by
	// the first modulate() within 2.5 ms.
	static constexpr float kDefaultG = 0.0245f;
	static constexpr float kDefaultK = 1.02f;
	static constexpr float kDefaultDrive = 0.25f;

	Side l, r;
	robotboy::OnePoleSmoother gSlew{kDefaultG};
	robotboy::OnePoleSmoother kSlew{kDefaultK};
	robotboy::OnePoleSmoother driveSlew{kDefaultDrive};
	robotboy::OnePoleSmoother makeupSlew{1.f};
	robotboy::OnePoleSmoother pushSlew{1.f};
	float gTarget = kDefaultG, kTarget = kDefaultK;
	float driveTarget = kDefaultDrive, makeupTarget = 1.f;
	float pushTarget = 1.f;
	float rGRatio = 1.f;   // Vintage: R side's cutoff relative to L

	void setRates(float alpha, float dcHz, float sampleRate) {
		gSlew.setAlpha(alpha);
		kSlew.setAlpha(alpha);
		driveSlew.setAlpha(alpha);
		makeupSlew.setAlpha(alpha);
		pushSlew.setAlpha(alpha);
		l.dc.setCutoff(dcHz, sampleRate);
		r.dc.setCutoff(dcHz, sampleRate);
	}
	void reset() {
		l.resetSignal();
		r.resetSignal();
		l.xPrev = r.xPrev = 0.f;
		gSlew.reset(gTarget = kDefaultG);
		kSlew.reset(kTarget = kDefaultK);
		driveSlew.reset(driveTarget = kDefaultDrive);
		makeupSlew.reset(makeupTarget = 1.f);
		pushSlew.reset(pushTarget = 1.f);
		rGRatio = 1.f;
	}
	// NaN recovery per modulate block. Smoother inputs are clamped params,
	// always finite, so only the signal path resets (no parameter sweep on
	// recovery). The DC blocker and FIRs only ever see the filter's output,
	// so a non-finite filter state is the one trigger needed for them too.
	void sanitize() {
		if (!l.filter.stateFinite() || !r.filter.stateFinite()) {
			l.resetSignal();
			r.resetSignal();
		}
	}
};

struct OnbetapPool {
	OnbetapVoice voices[16];
	int activeVoices = 1;

	void setVoices(int n) {
		n = std::clamp(n, 1, 16);
		for (int i = activeVoices; i < n; i++)
			voices[i].reset();
		activeVoices = n;
	}
	void resetAll() {
		for (auto& v : voices)
			v.reset();
	}
};

// Fixed per-side integrator mismatch (Vintage). Deterministic by design.
constexpr float kMismatchL1 = 0.06f,  kMismatchL2 = -0.045f;
constexpr float kMismatchR1 = -0.05f, kMismatchR2 = 0.055f;

// Phase-lag damping-correction factor, a pure function of cutoff. Evaluated
// at kCLagRefFsOs, the oversampled rate the module's kCLag constant was
// calibrated at (2x of 48 kHz), so the correction (and with it the
// self-oscillation onset and top-octave damping) doesn't depend on the
// oversampling setting or the host rate. cutoffToG's own clamp
// (0.245·fsOs = 23.52 kHz here) saturates it above the calibrated range.
inline constexpr float kCLagRefFsOs = 96000.f;
inline float cutoffLagCorr(float fcHz) {
	float gr = OnbetapFilter::cutoffToG(fcHz, kCLagRefFsOs);
	return gr * gr / (1.f + gr * gr);
}

// Deterministic Ornstein-Uhlenbeck random walk (xorshift32), for Vintage's
// cutoff drift; bit-identical across runs and between the VCV and
// MetaModule builds. step() advances one modulate block and returns the
// current offset in octaves. With uniform noise the stationary standard
// deviation is about scaleOct / sqrt(3); tau fixes the wander timescale.
struct DriftWalker {
	uint32_t rng;
	float value = 0.f;
	explicit DriftWalker(uint32_t seed) : rng(seed ? seed : 1u) {}

	float step(float dtSec, float scaleOct) {
		// xorshift32 → uniform in [-1, 1]
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		float u = (int32_t)rng * (1.f / 2147483648.f);
		constexpr float tau = 45.f;   // seconds
		float a = dtSec / tau;
		float sigma = scaleOct * std::sqrt(2.f * a);
		value += a * (0.f - value) + sigma * u;
		value = std::clamp(value, -3.f * scaleOct, 3.f * scaleOct);
		return value;
	}
	void reset() { value = 0.f; }
};

} // namespace onbetap
