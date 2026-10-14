#pragma once
#include "MF20Filter.hpp"
#include "../shared/dsp_utils.hpp"
#include <algorithm>

namespace mf20 {

// One polyphonic voice: the LP and HP filters for each stereo side, plus the
// smoothers that slew their cutoff and resonance.
struct VoiceEngine {
	// Default g (prewarp-gain) values ≈ the 750 Hz / 120 Hz cutoff defaults
	// at 48 kHz; the first modulate() call corrects them within 2.5 ms.
	static constexpr float kDefaultLpG = 0.0491f;
	static constexpr float kDefaultHpG = 0.0079f;
	static constexpr float kDefaultRes = 0.25f;

	MF20Filter lpFilter, hpFilter;
	MF20Filter lpFilterR, hpFilterR;

	// Cutoff smoothing happens in the g (prewarp-gain) domain: modulate()
	// computes tan/exp2 at ~2.5 ms intervals and the audio path only slews g.
	robotboy::OnePoleSmoother lpGSlew{kDefaultLpG};
	robotboy::OnePoleSmoother hpGSlew{kDefaultHpG};
	robotboy::OnePoleSmoother lpResSlew{kDefaultRes};
	robotboy::OnePoleSmoother hpResSlew{kDefaultRes};

	float lpGTarget = kDefaultLpG;
	float hpGTarget = kDefaultHpG;
	float lpResTarget = kDefaultRes;
	float hpResTarget = kDefaultRes;

	void setAlpha(float alpha) {
		lpGSlew.setAlpha(alpha);
		hpGSlew.setAlpha(alpha);
		lpResSlew.setAlpha(alpha);
		hpResSlew.setAlpha(alpha);
	}

	// Filters, smoothers and targets back to the defaults, so a voice that
	// rejoins doesn't slew toward a stale target.
	void reset() {
		resetFilters();
		lpGSlew.reset(lpGTarget = kDefaultLpG);
		hpGSlew.reset(hpGTarget = kDefaultHpG);
		lpResSlew.reset(lpResTarget = kDefaultRes);
		hpResSlew.reset(hpResTarget = kDefaultRes);
	}

	void resetFilters() {
		lpFilter.reset();
		hpFilter.reset();
		lpFilterR.reset();
		hpFilterR.reset();
	}

	// NaN/inf recovery: one bad upstream sample would otherwise poison the
	// filter state permanently. Called once per modulate block. Resets only
	// the filter states; the smoothers' inputs are clamped params, so they
	// are finite, and keeping them avoids a parameter sweep after recovery.
	void sanitize() {
		if (!lpFilter.stateFinite() || !hpFilter.stateFinite() ||
		    !lpFilterR.stateFinite() || !hpFilterR.stateFinite())
			resetFilters();
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

	void setAlpha(float alpha) {
		for (auto& e : engines)
			e.setAlpha(alpha);
	}

	void resetAll() {
		for (auto& e : engines)
			e.reset();
	}
};

} // namespace mf20
