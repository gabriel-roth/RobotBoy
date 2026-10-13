#include "GrainLayout.hpp"
#include "particules/quality_colors.h"

#include <algorithm>
#include <cmath>

namespace lucarne {

using robotboy_beads::GrainFeed;
using robotboy_beads::GrainLiveFrame;
using robotboy_beads::GrainStartEvent;

namespace {

Rgb Scale(Rgb c, float k) {
	k = std::min(std::max(k, 0.f), 1.f);
	return {uint8_t(c.r * k + 0.5f), uint8_t(c.g * k + 0.5f), uint8_t(c.b * k + 0.5f)};
}
Rgb Lighten(Rgb c, float t) {   // t of the way to white
	return {uint8_t(c.r + (255 - c.r) * t + 0.5f), uint8_t(c.g + (255 - c.g) * t + 0.5f),
	        uint8_t(c.b + (255 - c.b) * t + 0.5f)};
}
Rgb QualityColor(int q) {
	q = std::min(std::max(q, 0), 3);
	return {uint8_t(kQualityColors[q][0] * 255.f), uint8_t(kQualityColors[q][1] * 255.f),
	        uint8_t(kQualityColors[q][2] * 255.f)};
}
float Semitones(float speed) {
	const float s = 12.f * std::log2(std::max(std::fabs(speed), 1e-6f));
	return std::min(std::max(s, -GrainLayout::kMaxSemitones), GrainLayout::kMaxSemitones);
}
// Do [a, a+la) and [b, b+lb) overlap on a ring of width w?
bool Overlaps(float a, float la, float b, float lb, float w) {
	for (float shift : {-w, 0.f, w})
		if (a < b + shift + lb && b + shift < a + la) return true;
	return false;
}

constexpr Rgb kWriteHead{0xC0, 0xC8, 0xD0};
constexpr Rgb kWriteHeadFrozen{0x50, 0x58, 0x60};

}  // namespace

void GrainLayout::Clear() {
	grains_.clear();
	have_epoch_ = false;
	list_.wave.clear();
	list_.marks.clear();
}

void GrainLayout::SetSize(float w, float h) {
	w_ = std::max(w, 1.f);
	h_ = std::max(h, 1.f);
}

float GrainLayout::BandHeight() const { return h_ * kBandFraction; }
float GrainLayout::RowHeight() const { return (h_ - BandHeight()) / (2.f * kRowsPerSide); }

float GrainLayout::XForIndex(int32_t index, int32_t max_delay) const {
	return float(max_delay - 1 - index) / float(max_delay) * w_;
}

float GrainLayout::RowY(int side, int row) const {
	const float band_top = (h_ - BandHeight()) * 0.5f;
	const float band_bottom = band_top + BandHeight();
	const float offset = (row + 0.5f) * RowHeight();
	return side > 0 ? band_top - offset : band_bottom + offset;
}

int GrainLayout::PreferredRow(float speed) {
	return int(std::lround(std::fabs(Semitones(speed)) / kMaxSemitones * (kRowsPerSide - 1)));
}

int GrainLayout::SideFor(float speed) { return Semitones(speed) >= 0.f ? +1 : -1; }

int GrainLayout::ChooseRow(int side, int preferred, float x0, float span) const {
	const float a = std::min(x0, x0 + span), la = std::max(std::fabs(span), 1e-3f);
	auto taken = [&](int row) {
		for (const VisualGrain& g : grains_) {
			if (g.side != side || g.row != row) continue;
			if (Overlaps(a, la, std::min(g.x0, g.x0 + g.span), std::max(std::fabs(g.span), 1e-3f), w_))
				return true;
		}
		return false;
	};
	for (int r = preferred; r < kRowsPerSide; ++r) if (!taken(r)) return r;
	for (int r = preferred - 1; r >= 0; --r) if (!taken(r)) return r;
	return preferred;
}

void GrainLayout::Birth(const GrainStartEvent& e, const GrainLiveFrame& f) {
	if (int(grains_.size()) >= kMaxGrains) {
		// Make room: the ended grain closest to vanishing, else the oldest.
		auto victim = grains_.end();
		for (auto it = grains_.begin(); it != grains_.end(); ++it)
			if (it->ended && (victim == grains_.end() || it->fade < victim->fade)) victim = it;
		grains_.erase(victim != grains_.end() ? victim : grains_.begin());
	}
	const int32_t m = std::max<int32_t>(f.max_delay, 1);
	const int32_t start = std::min(std::max<int32_t>(e.start_index, 0), m - 1);
	VisualGrain g{};
	g.serial = e.serial;
	g.slot = std::min(std::max<int>(e.slot, 0), GrainFeed::kSlots - 1);
	g.start_index = start;
	g.speed = e.speed;
	g.duration = e.duration;
	g.x0 = XForIndex(start, m);
	g.span = std::min(std::max(e.speed * e.duration / float(m) * w_, -w_), w_);
	g.side = SideFor(e.speed);
	g.row = ChooseRow(g.side, PreferredRow(e.speed), g.x0, g.span);
	const auto& slot = f.slots[g.slot];
	if (slot.serial == e.serial && !slot.done) {
		g.ended = false;
		g.cursor = 0.f;
		g.level = slot.level;
	} else {
		g.ended = true;   // started and finished between two UI frames
		g.cursor = 1.f;
		g.fade = kFadeSeconds;
	}
	grains_.push_back(g);
}

void GrainLayout::Update(const Input& in) {
	if (!have_epoch_ || in.epoch != epoch_) {
		grains_.clear();
		epoch_ = in.epoch;
		have_epoch_ = true;
	}
	for (VisualGrain& g : grains_)
		if (g.ended) g.fade -= in.dt;
	grains_.erase(std::remove_if(grains_.begin(), grains_.end(),
	                             [](const VisualGrain& g) { return g.ended && g.fade <= 0.f; }),
	              grains_.end());
	if (in.frame) {
		const GrainLiveFrame& f = *in.frame;
		for (int i = 0; i < in.num_events; ++i) Birth(in.events[i], f);
		for (size_t i = 0; i < grains_.size(); ++i) {
			VisualGrain& g = grains_[i];
			if (g.ended) continue;
			const auto& slot = f.slots[g.slot];
			if (slot.serial == g.serial && !slot.done) {
				g.cursor = g.duration > 0.f ? std::min(std::max(slot.elapsed / g.duration, 0.f), 1.f) : 1.f;
				g.level = slot.level;
			} else {
				g.ended = true;
				g.fade = kFadeSeconds;
			}
		}
	}
	Build(in);
}

void GrainLayout::AddSpan(float x, float len, float y, float h, Rgb c) {
	// [x, x+len) on a ring of width w_, len >= 0.
	len = std::min(len, w_);
	x = std::fmod(x, w_);
	if (x < 0.f) x += w_;
	if (x + len <= w_) {
		list_.marks.push_back({x, y, len, h, c});
	} else {
		list_.marks.push_back({x, y, w_ - x, h, c});
		list_.marks.push_back({0.f, y, x + len - w_, h, c});
	}
}

void GrainLayout::Build(const Input& in) {
	list_.wave.clear();
	list_.marks.clear();
	const float band = BandHeight();
	const float mid = h_ * 0.5f;
	const float col_w = w_ / GrainFeed::kColumns;
	if (in.peak_lo && in.peak_hi) {
		for (int c = 0; c < GrainFeed::kColumns; ++c) {
			const float top = mid - in.peak_hi[c] / 127.f * band * 0.5f;
			const float bottom = mid - in.peak_lo[c] / 127.f * band * 0.5f;
			const float min_h = band * 0.02f;   // silence still shows a hairline
			const float h = std::max(bottom - top, min_h);
			list_.wave.push_back({c * col_w, std::min(top, mid - min_h * 0.5f), col_w, h, list_.wave_color});
		}
	}
	if (in.frame) {
		const float x = XForIndex(in.frame->write_index, std::max<int32_t>(in.frame->max_delay, 1));
		list_.marks.push_back({x, mid - band * 0.5f, std::max(col_w * 0.75f, w_ * 0.003f), band,
		                       in.frame->frozen ? kWriteHeadFrozen : kWriteHead});
	}
	const Rgb base = QualityColor(in.quality);
	const float row_h = RowHeight();
	const float thick = row_h * 0.5f;
	const float min_len = w_ / 160.f;   // 2 px at MetaModule's 320-wide fullscreen
	for (const VisualGrain& g : grains_) {
		const float k = g.ended ? 0.6f * (g.fade / kFadeSeconds) : 0.35f + 0.65f * g.level;
		const float y = RowY(g.side, g.row);
		const float len = std::max(std::fabs(g.span), min_len);
		const float start = g.span >= 0.f ? g.x0 : g.x0 - len;
		AddSpan(start, len, y - thick * 0.5f, thick, Scale(base, k));
		const float cx = g.x0 + g.span * g.cursor;
		const float cw = std::max(w_ * 0.008f, thick);
		const float fade = g.ended ? g.fade / kFadeSeconds : 1.f;
		AddSpan(cx - cw * 0.5f, cw, y - row_h * 0.45f, row_h * 0.9f, Scale(Lighten(base, 0.5f), fade));
	}
}

}  // namespace lucarne
