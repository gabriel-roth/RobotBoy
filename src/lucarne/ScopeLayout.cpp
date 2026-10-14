#include "ScopeLayout.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace lucarne {

void ScopeLayout::setSize(float w, float h) {
	width = std::max(w, 1.f);
	height = std::max(h, 1.f);
}

void ScopeLayout::clear() {
	haveWindow = false;
	std::fill(std::begin(hold), std::end(hold), 0.f);
	list.clear();
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
                               int32_t samplesPerEntry, int maxEntries) {
	if (toWholeBuffer) return maxEntries;
	const double span = std::max(window, kZoomMargin * longest);
	const double spe = double(std::max<int32_t>(samplesPerEntry, 1));
	return int(std::min<double>(maxEntries, std::ceil(span / spe) + 2.0));
}

void ScopeLayout::update(const Input& in) {
	const float maxDelay = float(std::max<int32_t>(in.maxDelay, 1));
	const bool snap = !haveWindow || in.epoch != epoch;
	epoch = in.epoch;
	if (snap) std::fill(std::begin(hold), std::end(hold), 0.f);

	// Every slot's hold runs down, passed this frame or not; an audible
	// tap refills its own. A Slice head's view must hold its whole slice,
	// not just its read point (Slice shows the whole buffer anyway).
	const float dt = std::max(in.dt, 0.f);
	const float rate = in.lineRate > 0.f ? in.lineRate : 48000.f;   // as Lucarne's default
	for (float& h : hold) h = std::max(h - dt, 0.f);
	const int numTaps = std::min(in.numTaps, int(robotboy_beads::GrainLiveFrame::kTaps));
	float longest = 0.f;
	bool present = false;
	for (int i = 0; i < numTaps; ++i) {
		const Tap& t = in.taps[i];
		const bool audible = t.level > kTapMinLevel;
		if (t.slice) {
			if (audible) {
				longest = std::max(longest, t.sliceOldest);
				present = true;
			}
			continue;
		}
		float* h = t.slot >= 0 && t.slot < int(robotboy_beads::GrainLiveFrame::kTaps) ? &hold[t.slot] : nullptr;
		if (audible && h) *h = kHoldFraction * t.delay / rate + kHoldFloor;
		if (audible || (h && *h > 0.f)) {
			longest = std::max(longest, t.delay);
			present = true;
		}
	}
	if (snap) window = maxDelay;   // initial: whole buffer
	// Modulated: the delay moves on its own, so aim at the whole buffer
	// instead of chasing it, easing there as after a knob turn.
	if (present || in.modulated) {
		const float lo = std::min(kMinWindowSeconds * in.lineRate, maxDelay);
		const float target = in.modulated ? maxDelay
			: std::max(std::min(std::max(kZoomMargin * longest, lo), maxDelay), 1.f);
		if (snap) {
			window = target;
		} else {
			const float k = 1.f - std::exp(-dt / (in.modulated ? kModulatedZoomTau : kZoomTau));
			const float d = std::log(target) - std::log(window);
			// Land exactly once close: float steps stall a few samples short.
			window = std::fabs(d) < 1e-4f ? target : std::exp(std::log(window) + d * k);
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
				                       mix(list.background, tapColor(t.slot), kSliceBandMix)});
		}
		if (t.delay > window || t.delay < 0.f) continue;
		marker(xForAge(t.delay), tapColor(t.slot), tapOutput(in, t.delay, t.level));
	}
}

}  // namespace lucarne
