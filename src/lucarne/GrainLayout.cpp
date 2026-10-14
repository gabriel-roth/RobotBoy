#include "GrainLayout.hpp"

#include <algorithm>
#include <cmath>

namespace lucarne {

using robotboy_beads::GrainFeed;
using robotboy_beads::GrainLiveFrame;
using robotboy_beads::GrainStartEvent;

namespace {

float semitones(float speed) {
	const float s = 12.f * std::log2(std::max(std::fabs(speed), 1e-6f));
	return std::min(std::max(s, -GrainLayout::kMaxSemitones), GrainLayout::kMaxSemitones);
}
// Do [a, a+la) and [b, b+lb) overlap on a ring of width w?
bool overlaps(float a, float la, float b, float lb, float w) {
	for (float shift : {-w, 0.f, w})
		if (a < b + shift + lb && b + shift < a + la) return true;
	return false;
}

constexpr Rgb kWriteHead{0xC0, 0xC8, 0xD0};
constexpr Rgb kWriteHeadFrozen{0x50, 0x58, 0x60};
constexpr Rgb kPanLeft{0x3F, 0xA9, 0xF5};
constexpr Rgb kPanCenter{0xE8, 0xE4, 0xDA};
constexpr Rgb kPanRight{0xFF, 0x8A, 0x3D};

}  // namespace

void GrainLayout::clear() {
	grains.clear();
	haveEpoch = false;
	list.clear();
}

void GrainLayout::setSize(float w, float h) {
	width = std::max(w, 1.f);
	height = std::max(h, 1.f);
	rows = std::max(1, int((height - bandHeight()) / (2.f * rowHeight()) + 1e-3f));
}

// The display's height if it were 16:9, capped at its real height.
static float nominalHeight(float w, float h) { return std::min(h, w * 9.f / 16.f); }
float GrainLayout::bandHeight() const { return nominalHeight(width, height) * kBandFraction; }
float GrainLayout::rowHeight() const {
	return (nominalHeight(width, height) - bandHeight()) / (2.f * kRowsPerSide);
}

float GrainLayout::xForIndex(int32_t index, int32_t maxDelay) const {
	return float(maxDelay - 1 - index) / float(maxDelay) * width;
}

float GrainLayout::rowY(int side, int row) const {
	const float bandTop = (height - bandHeight()) * 0.5f;
	const float bandBottom = bandTop + bandHeight();
	const float offset = (row + 0.5f) * rowHeight();
	return side > 0 ? bandTop - offset : bandBottom + offset;
}

int GrainLayout::preferredRow(float speed, int rowsPerSide) {
	return int(std::lround(std::fabs(semitones(speed)) / kMaxSemitones * (rowsPerSide - 1)));
}

int GrainLayout::sideFor(float speed) { return semitones(speed) >= 0.f ? +1 : -1; }

int GrainLayout::chooseRow(int side, int preferred, float x0, float span) const {
	const float a = std::min(x0, x0 + span), la = std::max(std::fabs(span), 1e-3f);
	auto taken = [&](int row) {
		for (const VisualGrain& g : grains) {
			if (g.side != side || g.row != row) continue;
			if (overlaps(a, la, std::min(g.x0, g.x0 + g.span), std::max(std::fabs(g.span), 1e-3f), width))
				return true;
		}
		return false;
	};
	for (int r = preferred; r < rows; ++r) if (!taken(r)) return r;
	for (int r = preferred - 1; r >= 0; --r) if (!taken(r)) return r;
	return preferred;
}

Rgb GrainLayout::panColor(float pan) {
	// Beads bunches pans around the center (BoundedRandomFloat), so the
	// square root stretches small offsets into visible color.
	const float t = std::min(std::max((pan - 0.5f) * 2.f, -1.f), 1.f);
	const float shaped = std::sqrt(std::fabs(t));
	return t < 0.f ? mix(kPanCenter, kPanLeft, shaped) : mix(kPanCenter, kPanRight, shaped);
}

void GrainLayout::makeRoom() {
	if (int(grains.size()) >= kMaxGrains) {
		// Make room: the ended grain closest to vanishing, else the oldest.
		auto victim = grains.end();
		for (auto it = grains.begin(); it != grains.end(); ++it)
			if (it->ended && (victim == grains.end() || it->fade < victim->fade)) victim = it;
		grains.erase(victim != grains.end() ? victim : grains.begin());
	}
}

void GrainLayout::birth(const GrainStartEvent& e, const GrainLiveFrame& f) {
	makeRoom();
	const int32_t m = std::max<int32_t>(f.max_delay, 1);
	const int32_t start = std::min(std::max<int32_t>(e.start_index, 0), m - 1);
	VisualGrain g{};
	g.serial = e.serial;
	g.slot = std::min(std::max<int>(e.slot, 0), GrainFeed::kSlots - 1);
	g.speed = e.speed;
	g.duration = e.duration;
	g.pan = e.pan;
	g.x0 = xForIndex(start, m);
	g.span = std::min(std::max(e.speed * e.duration / float(m) * width, -width), width);
	g.side = sideFor(e.speed);
	g.row = chooseRow(g.side, preferredRow(e.speed, rows), g.x0, g.span);
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
	grains.push_back(g);
}

void GrainLayout::update(const Input& in) {
	if (!haveEpoch || in.epoch != epoch) {
		grains.clear();
		epoch = in.epoch;
		haveEpoch = true;
	}
	for (VisualGrain& g : grains)
		if (g.ended) g.fade -= in.dt;
	grains.erase(std::remove_if(grains.begin(), grains.end(),
	                             [](const VisualGrain& g) { return g.ended && g.fade <= 0.f; }),
	              grains.end());
	if (in.frame) {
		const GrainLiveFrame& f = *in.frame;
		for (int i = 0; i < in.numEvents; ++i) birth(in.events[i], f);
		for (size_t i = 0; i < grains.size(); ++i) {
			VisualGrain& g = grains[i];
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
	build(in);
}

void GrainLayout::addSpan(float x, float len, float y, float h, Rgb c) {
	// [x, x+len) on a ring of width width, len >= 0.
	len = std::min(len, width);
	x = std::fmod(x, width);
	if (x < 0.f) x += width;
	if (x + len <= width) {
		list.marks.push_back({x, y, len, h, c});
	} else {
		list.marks.push_back({x, y, width - x, h, c});
		list.marks.push_back({0.f, y, x + len - width, h, c});
	}
}

void GrainLayout::build(const Input& in) {
	list.clear();
	const float band = bandHeight();
	const float mid = height * 0.5f;
	const float colW = width / GrainFeed::kColumns;
	if (in.peakLo && in.peakHi) {
		for (int c = 0; c < GrainFeed::kColumns; ++c) {
			const float top = mid - in.peakHi[c] / 127.f * band * 0.5f;
			const float bottom = mid - in.peakLo[c] / 127.f * band * 0.5f;
			const float minH = band * 0.02f;   // silence still shows a hairline
			const float h = std::max(bottom - top, minH);
			list.wave.push_back({c * colW, std::min(top, mid - minH * 0.5f), colW, h, list.waveColor});
		}
	}
	if (in.frame) {
		const float x = xForIndex(in.frame->write_index, std::max<int32_t>(in.frame->max_delay, 1));
		list.marks.push_back({x, mid - band * 0.5f, std::max(colW * 0.75f, width * 0.003f), band,
		                       in.frame->frozen ? kWriteHeadFrozen : kWriteHead});
	}
	const float rowH = rowHeight();
	const float thick = rowH * 0.5f;
	const float minLen = width / 160.f;   // 2 px at MetaModule's 320-wide fullscreen
	for (const VisualGrain& g : grains) {
		const Rgb base = panColor(g.pan);
		const float k = g.ended ? 0.6f * (g.fade / kFadeSeconds) : 0.35f + 0.65f * g.level;
		const float y = rowY(g.side, g.row);
		const float len = std::max(std::fabs(g.span), minLen);
		const float start = g.span >= 0.f ? g.x0 : g.x0 - len;
		// Dim and fade toward the background, not black, so a fading grain
		// never shows darker than the display behind it.
		const Rgb bg = list.background;
		addSpan(start, len, y - thick * 0.5f, thick, mix(bg, base, std::min(std::max(k, 0.f), 1.f)));
		const float cx = g.x0 + g.span * g.cursor;
		const float cw = std::max(width * 0.008f, thick);
		const float fade = g.ended ? g.fade / kFadeSeconds : 1.f;
		addSpan(cx - cw * 0.5f, cw, y - rowH * 0.45f, rowH * 0.9f, mix(bg, lighten(base, 0.5f), std::min(std::max(fade, 0.f), 1.f)));
	}
}

}  // namespace lucarne
