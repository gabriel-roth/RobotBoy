#pragma once
// Per-voice state for Vespid polyphony: the 4x oversampling wrapper around
// WaspFilter, the voice smoothers, and the voice pool.
#include "WaspFilter.hpp"
#include "wasp_dsp_utils.hpp"
#include "../shared/dsp_utils.hpp"
#include <algorithm>

namespace wasp {

constexpr int kOversample = 4;
constexpr float kDcBlockHz = 8.f;

// Output-select mask for Channel::process, one bit per output jack. Only
// selected signals pay for decimation and DC blocking.
enum : int {
	kOutLp  = 1,
	kOutBp  = 2,
	kOutHp  = 4,
	kOutMix = 8,
};

// One audio channel: WaspFilter at 4x, through a two-stage halfband
// up/down cascade (host <-> 2x <-> 4x).
//
// Each output signal needs its own decimator delay lines (lp, bp, hp, and
// mix, which is blended at the oversampled rate, are distinct signals once
// they leave the nonlinear core); there is one input, so one upsampler.
//
// Output makeup and the DC blockers are linear and time-invariant, so they
// commute with the decimators and run at host rate, once per selected output.
struct Channel {
	WaspFilter filt;
	HalfbandUp up;                                          // host -> 2x
	HalfbandUpInner up4a;                                   // 2x -> 4x
	HalfbandDownInner downLp4a, downBp4a, downHp4a, downMix4a;  // 4x -> 2x
	HalfbandDown downLp, downBp, downHp, downMix;               // 2x -> host
	robotboy::DcBlocker dcLp, dcBp, dcHp, dcMix;            // host rate
	int activeMask = 0;   // chains whose histories are currently live

	struct Out { float lp, bp, hp, mix; };

	// Returns host-rate, makeup'd and DC-blocked outputs for every signal
	// selected in `mask` (the others read 0). g/h1/kC2 are already slewed by
	// the caller and held across the sub-samples of one host sample, as is
	// `m`, the Mix blend (0 = LP, 1 = HP).
	Out process(float in, float g, const H1Coeffs& h1, float kC2,
	            int mask, float m, float makeup) {
		// A signal whose jack was just connected starts with a clean chain:
		// its histories stopped updating while unpatched. (The ~24-sample
		// FIR warmup is a 0.5 ms fade-in, inaudible on a patching action.)
		if (int fresh = mask & ~activeMask) {
			if (fresh & kOutLp)  { downLp.reset();  downLp4a.reset();  dcLp.reset();  }
			if (fresh & kOutBp)  { downBp.reset();  downBp4a.reset();  dcBp.reset();  }
			if (fresh & kOutHp)  { downHp.reset();  downHp4a.reset();  dcHp.reset();  }
			if (fresh & kOutMix) { downMix.reset(); downMix4a.reset(); dcMix.reset(); }
		}
		activeMask = mask;

		float buf2[2], buf4[4];
		up.process(in, buf2);
		up4a.process(buf2[0], &buf4[0]);
		up4a.process(buf2[1], &buf4[2]);

		WaspFilter::Out o[4];
		for (int i = 0; i < 4; i++)
			o[i] = filt.process(buf4[i], g, h1, kC2);

		Out out{0.f, 0.f, 0.f, 0.f};
		if (mask & kOutLp)
			out.lp = makeup * dcLp.process(downLp.process(
				downLp4a.process(o[0].lp, o[1].lp), downLp4a.process(o[2].lp, o[3].lp)));
		if (mask & kOutBp)
			out.bp = makeup * dcBp.process(downBp.process(
				downBp4a.process(o[0].bp, o[1].bp), downBp4a.process(o[2].bp, o[3].bp)));
		if (mask & kOutHp)
			out.hp = makeup * dcHp.process(downHp.process(
				downHp4a.process(o[0].hp, o[1].hp), downHp4a.process(o[2].hp, o[3].hp)));
		if (mask & kOutMix) {
			float b[4];
			for (int i = 0; i < 4; i++)
				b[i] = (1.f - m) * o[i].lp + m * o[i].hp;
			out.mix = makeup * dcMix.process(downMix.process(
				downMix4a.process(b[0], b[1]), downMix4a.process(b[2], b[3])));
		}
		return out;
	}

	void setSampleRate(float fsHost) {
		filt.setSampleRate(fsHost * kOversample);
		dcLp.setCutoff(kDcBlockHz, fsHost);
		dcBp.setCutoff(kDcBlockHz, fsHost);
		dcHp.setCutoff(kDcBlockHz, fsHost);
		dcMix.setCutoff(kDcBlockHz, fsHost);
	}

	// Filter core state plus every resampler and output history.
	void reset() {
		filt.reset();
		resetResamplers();
		dcLp.reset(); dcBp.reset(); dcHp.reset(); dcMix.reset();
	}

	// The resampler histories only; filter core state carries across a
	// sample-rate change.
	void resetResamplers() {
		up.reset();
		up4a.reset();
		downLp4a.reset(); downBp4a.reset(); downHp4a.reset(); downMix4a.reset();
		downLp.reset(); downBp.reset(); downHp.reset(); downMix.reset();
	}
};

struct VoiceEngine {
	Channel l, r;

	robotboy::OnePoleSmoother gSlew{0.049f};
	robotboy::OnePoleSmoother kC2Slew{0.f};
	robotboy::OnePoleSmoother rhoSlew{0.f};
	// Drive at rest in the default mode (British) is 0.5x: the 2x knob-floor
	// pre-gain times British's 0.25 level staging (kBritish.inGain). Seeding
	// the smoother there keeps a fresh voice from sweeping in.
	robotboy::OnePoleSmoother driveSlew{0.5f};
	// computeH1 is division-heavy, so it runs at modulate rate and these
	// smoothers slew its coefficients per sample. They're seeded at the
	// rho=0 values for the current rate (seedH1) so a reset voice doesn't
	// fade the resonance network in from zero.
	robotboy::OnePoleSmoother beta0Slew{0.f};
	robotboy::OnePoleSmoother beta1Slew{0.f};
	robotboy::OnePoleSmoother alpha1Slew{0.f};

	float gTarget = 0.049f, kC2Target = 0.f, rhoTarget = 0.f, driveTarget = 0.5f;
	float beta0Target = 0.f, beta1Target = 0.f, alpha1Target = 0.f;

	// Internal (oversampled) rate; matches WaspFilter's default.
	float fsInt = 192000.f;

	VoiceEngine() { seedH1(); }

	void setSampleRate(float fsHost) {
		fsInt = fsHost * kOversample;
		l.setSampleRate(fsHost);
		r.setSampleRate(fsHost);
	}

	void setAlpha(float alpha) {
		for (robotboy::OnePoleSmoother* s : {&gSlew, &kC2Slew, &rhoSlew, &driveSlew,
		                                     &beta0Slew, &beta1Slew, &alpha1Slew})
			s->setAlpha(alpha);
	}

	// Seed the H1 smoothers (and targets) at the exact rho=0 coefficients
	// for the current internal rate.
	void seedH1() {
		H1Coeffs h1 = computeH1(0.f, fsInt);
		beta0Slew.reset(beta0Target = h1.beta0);
		beta1Slew.reset(beta1Target = h1.beta1);
		alpha1Slew.reset(alpha1Target = h1.alpha1);
	}

	void reset() {
		l.reset();
		r.reset();
		gSlew.reset(gTarget = 0.049f);
		kC2Slew.reset(kC2Target = 0.f);
		rhoSlew.reset(rhoTarget = 0.f);
		driveSlew.reset(driveTarget = 0.5f);
		seedH1();
	}

	// NaN/inf recovery, called once per modulate block. Only the offending
	// channel resets, core and resamplers both (a NaN in the core has reached
	// the FIR delay lines by the time it's detected). The smoothers' inputs
	// are clamped params, so they stay, avoiding a parameter sweep.
	void sanitize() {
		if (!l.filt.stateFinite()) l.reset();
		if (!r.filt.stateFinite()) r.reset();
	}
};

// All 16 voices live by value, so the audio thread never allocates;
// setVoices() only changes activeVoices.
struct EnginePool {
	VoiceEngine engines[16];
	int activeVoices = 1;

	void setVoices(int n) {
		n = std::clamp(n, 1, 16);
		// Voices (re)entering the active range start clean: a voice that rang
		// at high resonance and went inactive must not re-emit its old state.
		for (int i = activeVoices; i < n; i++)
			engines[i].reset();
		activeVoices = n;
	}

	// New host rate: resampler histories don't survive it, filter state does.
	void setSampleRate(float fsHost, float alpha) {
		for (auto& e : engines) {
			e.setSampleRate(fsHost);
			e.setAlpha(alpha);
			e.l.resetResamplers();
			e.r.resetResamplers();
		}
	}

	void resetAll() {
		for (auto& e : engines)
			e.reset();
	}
};

} // namespace wasp
