#include "ScopeLayout.hpp"

#include <algorithm>
#include <cmath>

namespace lucarne {

void ScopeLayout::SetSize(float w, float h) {
	w_ = std::max(w, 1.f);
	h_ = std::max(h, 1.f);
}

void ScopeLayout::Clear() {
	have_window_ = false;
	list_.Clear();
}

float ScopeLayout::XForAge(float age) const {
	return (1.f - age / std::max(window_, 1e-3f)) * NowX();
}

float ScopeLayout::TapOutput(const Input& in, float delay, float level) {
	if (!in.lo || !in.hi || in.entries <= 0 || delay < 0.f) return 0.f;
	const int spe = std::max<int32_t>(in.samples_per_entry, 1);
	const float idx = delay / float(spe);
	if (!(idx < float(in.entries))) return 0.f;
	const int i = std::min(std::max(int(idx), 0), in.entries - 1);
	const int peak = std::max(std::abs(int(in.lo[i])), std::abs(int(in.hi[i])));
	return std::min(float(peak) / 127.f, 1.f) * std::min(std::max(level, 0.f), 1.f);
}

float ScopeLayout::DryOutput(const Input& in) {
	if (!in.lo || !in.hi || in.entries <= 0) return 0.f;
	const float spe = float(std::max<int32_t>(in.samples_per_entry, 1));
	const int n = std::min(std::max(int(std::ceil(kDryWindowSeconds * in.line_rate / spe)), 1), in.entries);
	int peak = 0;
	for (int i = 0; i < n; ++i)
		peak = std::max({peak, std::abs(int(in.lo[i])), std::abs(int(in.hi[i]))});
	return std::min(float(peak) / 127.f, 1.f);
}

void ScopeLayout::Update(const Input& in) {
	const float max_delay = float(std::max<int32_t>(in.max_delay, 1));
	const bool snap = !have_window_ || in.epoch != epoch_;
	epoch_ = in.epoch;

	float longest = 0.f;
	bool audible = false;
	for (int i = 0; i < std::min(in.num_taps, int(robotboy_beads::GrainLiveFrame::kTaps)); ++i) {
		// A Slice head's view must hold its whole slice, not just its read point.
		const Tap& t = in.taps[i];
		longest = std::max(longest, t.slice ? t.slice_oldest : t.delay);
		audible = true;
	}
	if (snap) window_ = max_delay;   // initial: whole buffer
	if (audible) {
		const float lo = std::min(kMinWindowSeconds * in.line_rate, max_delay);
		const float target = std::max(std::min(std::max(kZoomMargin * longest, lo), max_delay), 1.f);
		if (snap) {
			window_ = target;
		} else {
			const float k = 1.f - std::exp(-std::max(in.dt, 0.f) / kZoomTau);
			window_ = std::exp(std::log(window_) + (std::log(target) - std::log(window_)) * k);
		}
	}
	// Slice: show the whole buffer, fixed, until Slice turns off. The slices
	// can be anywhere in it, and a zoom that followed the heads (which fade
	// in and out and restart) would keep changing the frozen waveform.
	if (in.frozen) window_ = max_delay;
	have_window_ = true;

	list_.Clear();
	const float mid = h_ * 0.5f;
	const float full = kWaveFraction * h_;
	const float hair = full * 0.02f;
	const float col_w = NowX() / kColumns;
	const float spe = float(std::max<int32_t>(in.samples_per_entry, 1));
	const float col_age = window_ / kColumns;
	for (int c = 0; c < kColumns; ++c) {
		int lo = 0, hi = 0;
		bool have = false;
		if (in.lo && in.hi && in.entries > 0) {
			const float a0 = float(kColumns - 1 - c) * col_age;
			const int e0 = int(a0 / spe);
			if (e0 < in.entries) {
				const int e1 = std::min(std::max(e0, int(std::ceil((a0 + col_age) / spe)) - 1), in.entries - 1);
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
		list_.wave.push_back({c * col_w, top, col_w, h, list_.wave_color});
	}

	const float line_w = std::max(w_ * 0.004f, 1.f);
	auto marker = [&](float x, Rgb base, float o) {
		list_.marks.push_back({x - line_w * 0.5f, 0.f, line_w, h_, Mix(list_.background, base, 0.3f + 0.7f * o)});
		if (!(o > 0.f)) return;
		const float bar_h = o * full;
		list_.marks.push_back({x - line_w * 1.5f, mid - bar_h * 0.5f, line_w * 3.f, bar_h, Lighten(base, 0.4f)});
	};
	marker(NowX(), kDryColor, DryOutput(in));
	for (int i = 0; i < std::min(in.num_taps, int(robotboy_beads::GrainLiveFrame::kTaps)); ++i) {
		const Tap& t = in.taps[i];
		if (t.slice) {
			// The stretch of audio the slice loops, behind the waveform.
			const float x0 = std::max(XForAge(t.slice_oldest), 0.f);
			const float x1 = std::min(XForAge(std::max(t.slice_oldest - t.slice_length, 0.f)), NowX());
			if (x1 > x0)
				list_.under.push_back({x0, 0.f, x1 - x0, h_,
				                       Mix(list_.background, TapColor(t.slot), kSliceBandMix)});
		}
		if (t.delay > window_ || t.delay < 0.f) continue;
		marker(XForAge(t.delay), TapColor(t.slot), TapOutput(in, t.delay, t.level));
	}
}

}  // namespace lucarne
