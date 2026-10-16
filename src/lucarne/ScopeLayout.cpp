#include "ScopeLayout.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace lucarne {

void ScopeLayout::setSize(float w, float h) {
	width = std::max(w, 1.f);
	height = std::max(h, 1.f);
}

float ZoomTuning::reach() const {
	const float kNowInset = ScopeLayout::kNowInset;
	// The nearest tap sits at nowX() x (1 - delay / window); its gap to the
	// now line, delay / window x nowX(), must be at least minGap x width.
	const float gap = std::min(std::max(minGap, 0.01f), 0.9f * (1.f - kNowInset));
	return (1.f - kNowInset) / gap;
}

void ScopeLayout::clear() {
	haveWindow = false;
	gliding = false;
	haveRange = false;
	std::fill(std::begin(hold), std::end(hold), 0.f);
	list.clear();
}

void ScopeLayout::trackRange(float nearest, float farthest, bool modulated, float dt, const ZoomTuning& z) {
	// Unmodulated: just this frame's taps (the hold covers Shape's dips).
	// Modulated: a new extreme is taken at once and held, then released
	// toward the taps, so a delay that wanders settles on one zoom.
	const float hold = std::max(z.modHoldSeconds, 0.f);
	if (!modulated || !haveRange) {
		rangeNear = nearest;
		rangeFar = farthest;
		holdNear = holdFar = hold;
		haveRange = true;
		return;
	}
	const float k = z.modReleaseSeconds > 0.f ? 1.f - std::exp(-dt / z.modReleaseSeconds) : 1.f;
	auto follow = [&](float& range, float& left, float now, bool wider) {
		if (wider) {
			range = now;
			left = hold;
		} else if ((left -= dt) <= 0.f) {
			left = 0.f;
			range = std::exp(std::log(range) + (std::log(now) - std::log(range)) * k);
		}
	};
	follow(rangeNear, holdNear, nearest, nearest <= rangeNear);
	follow(rangeFar, holdFar, farthest, farthest >= rangeFar);
}

float ScopeLayout::xForAge(float age) const {
	return (1.f - age / std::max(window, 1e-3f)) * nowX();
}

float ScopeLayout::tapOutput(const Input& in, float delay, float level) {
	if (!in.lo || !in.hi || in.entries <= 0 || delay < 0.f) return 0.f;
	const int spe = std::max<int32_t>(in.samplesPerEntry, 1);
	const float idx = delay / float(spe);
	if (!(idx < float(in.entries))) return 0.f;
	const int i = std::min(std::max(int(idx), 0), in.entries - 1);
	const int peak = std::max(std::abs(int(in.lo[i])), std::abs(int(in.hi[i])));
	return std::min(float(peak) / 127.f, 1.f) * std::min(std::max(level, 0.f), 1.f);
}

float ScopeLayout::dryOutput(const Input& in) {
	if (!in.lo || !in.hi || in.entries <= 0) return 0.f;
	const float spe = float(std::max<int32_t>(in.samplesPerEntry, 1));
	const int n = std::min(std::max(int(std::ceil(kDryWindowSeconds * in.lineRate / spe)), 1), in.entries);
	int peak = 0;
	for (int i = 0; i < n; ++i)
		peak = std::max({peak, std::abs(int(in.lo[i])), std::abs(int(in.hi[i]))});
	return std::min(float(peak) / 127.f, 1.f);
}

int ScopeLayout::entriesToRead(float window, float longest, bool toWholeBuffer,
                               int32_t samplesPerEntry, int maxEntries, const ZoomTuning& tuning) {
	if (toWholeBuffer) return maxEntries;
	// The widest target: the nearest tap is no farther than the longest.
	const double span = std::max(window, std::max(tuning.farMargin, tuning.reach()) * longest);
	const double spe = double(std::max<int32_t>(samplesPerEntry, 1));
	return int(std::min<double>(maxEntries, std::ceil(span / spe) + 2.0));
}

void ScopeLayout::update(const Input& in) {
	const float maxDelay = float(std::max<int32_t>(in.maxDelay, 1));
	const bool snap = !haveWindow || in.epoch != epoch;
	epoch = in.epoch;
	if (snap) {
		std::fill(std::begin(hold), std::end(hold), 0.f);
		gliding = false;
		haveRange = false;
	}
	// Primary ratio: the louder one, with hysteresis once running.
	if (snap)
		primary = in.ratioGain[1] > in.ratioGain[0] ? 1 : 0;
	else if (in.ratioGain[1 - primary] > in.ratioGain[primary] + kPrimaryHysteresis)
		primary = 1 - primary;

	// Every slot's hold runs down, passed this frame or not; an audible
	// tap refills its own. A Slice head's view must hold its whole slice,
	// not just its read point (Slice shows the whole buffer anyway).
	const float dt = std::max(in.dt, 0.f);
	const float rate = in.lineRate > 0.f ? in.lineRate : 48000.f;   // as Lucarne's default
	for (float& h : hold) h = std::max(h - dt, 0.f);
	const int numTaps = std::min(in.numTaps, int(robotboy_beads::GrainLiveFrame::kTaps));
	float longest = 0.f, nearest = maxDelay;
	bool present = false;
	for (int i = 0; i < numTaps; ++i) {
		const Tap& t = in.taps[i];
		const bool audible = t.level > kTapMinLevel;
		if (t.slice) {
			if (audible) {
				longest = std::max(longest, t.sliceOldest);
				nearest = std::min(nearest, t.delay);
				present = true;
			}
			continue;
		}
		float* h = t.slot >= 0 && t.slot < int(robotboy_beads::GrainLiveFrame::kTaps) ? &hold[t.slot] : nullptr;
		if (audible && h) *h = kHoldFraction * t.delay / rate + kHoldFloor;
		if (audible || (h && *h > 0.f)) {
			longest = std::max(longest, t.delay);
			nearest = std::min(nearest, t.delay);
			present = true;
		}
	}
	if (snap) window = maxDelay;   // initial: whole buffer
	if (present) {
		const ZoomTuning& z = in.tuning;
		trackRange(std::max(nearest, 1.f), std::max(longest, 1.f), in.modulated, dt, z);
		// Narrowest: the farthest tap and its margin on screen. Widest: the
		// nearest tap's gap to the now line kept. A conflict goes to the far tap.
		const float floor = std::min(kMinWindowSeconds * in.lineRate, maxDelay);
		const float lo = std::max(std::min(std::max(z.farMargin * rangeFar, floor), maxDelay), 1.f);
		const float hi = std::max(std::min(z.reach() * rangeNear, maxDelay), 1.f);
		float target = lo;
		if (hi > lo) {
			const float b = std::min(std::max(z.bandPosition, 0.f), 1.f);
			target = std::exp(std::log(lo) + b * (std::log(hi) - std::log(lo)));
		}
		if (snap) {
			window = target;
		} else {
			// Deadband: while the window keeps both rules and the target is
			// near, stay put. Once out, glide all the way to the target.
			const float d = std::log(target) - std::log(window);
			const bool inBand = window >= lo * (1.f - 1e-4f) && window <= std::max(hi, lo) * (1.f + 1e-4f);
			const bool stay = !gliding && inBand && std::fabs(d) <= std::log1p(std::max(z.deadband, 0.f));
			if (stay) {
			} else if (std::fabs(d) < 1e-4f) {   // land exactly: float steps stall a few samples short
				window = target;
				gliding = false;
			} else {
				gliding = true;
				const float tau = in.modulated ? z.modGlide : z.knobGlide;
				const float k = tau > 0.f ? 1.f - std::exp(-dt / tau) : 1.f;
				window = std::exp(std::log(window) + d * k);
			}
		}
	}
	// Slice: show the whole buffer, fixed, until Slice turns off. The slices
	// can be anywhere in it, and a zoom that followed the heads (which fade
	// in and out and restart) would keep changing the frozen waveform.
	if (in.frozen) window = maxDelay;
	haveWindow = true;

	list.clear();
	const float mid = height * 0.5f;
	const float full = kWaveFraction * height;
	const float hair = full * 0.02f;
	const float colW = nowX() / kColumns;
	const float spe = float(std::max<int32_t>(in.samplesPerEntry, 1));
	const float colAge = window / kColumns;
	for (int c = 0; c < kColumns; ++c) {
		int lo = 0, hi = 0;
		bool have = false;
		if (in.lo && in.hi && in.entries > 0) {
			const float a0 = float(kColumns - 1 - c) * colAge;
			const int e0 = int(a0 / spe);
			if (e0 < in.entries) {
				const int e1 = std::min(std::max(e0, int(std::ceil((a0 + colAge) / spe)) - 1), in.entries - 1);
				lo = 127;
				hi = -127;
				for (int e = e0; e <= e1; ++e) {
					lo = std::min<int>(lo, in.lo[e]);
					hi = std::max<int>(hi, in.hi[e]);
				}
				have = true;
			}
		}
		float top = mid - hair * 0.5f, h = hair;
		if (have) {
			const float t = mid - hi / 127.f * full * 0.5f;
			const float b = mid - lo / 127.f * full * 0.5f;
			h = std::max(b - t, hair);
			top = std::min(t, mid - hair * 0.5f);
		}
		list.wave.push_back({c * colW, top, colW, h, list.waveColor});
	}

	const float lineW = std::max(width * 0.004f, 1.f);
	auto marker = [&](float x, Rgb base, float o) {
		list.marks.push_back({x - lineW * 0.5f, 0.f, lineW, height, mix(list.background, base, 0.3f + 0.7f * o)});
		if (!(o > 0.f)) return;
		const float barH = o * full;
		list.marks.push_back({x - lineW * 1.5f, mid - barH * 0.5f, lineW * 3.f, barH, lighten(base, 0.4f)});
	};
	marker(nowX(), kDryColor, dryOutput(in));
	for (int i = 0; i < numTaps; ++i) {
		const Tap& t = in.taps[i];
		if (!(t.level > kTapMinLevel)) continue;   // held or silent: no band, no marker
		if (t.slice) {
			// The stretch of audio the slice loops, behind the waveform.
			const float x0 = std::max(xForAge(t.sliceOldest), 0.f);
			const float x1 = std::min(xForAge(std::max(t.sliceOldest - t.sliceLength, 0.f)), nowX());
			if (x1 > x0)
				list.under.push_back({x0, 0.f, x1 - x0, height,
				                       mix(list.background, tapColor(t.slot, primary), kSliceBandMix)});
		}
		if (t.delay > window || t.delay < 0.f) continue;
		marker(xForAge(t.delay), tapColor(t.slot, primary), tapOutput(in, t.delay, t.level));
	}
}

}  // namespace lucarne
