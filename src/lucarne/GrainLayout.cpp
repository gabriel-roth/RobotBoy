#include "GrainLayout.hpp"
#include "LayoutColor.hpp"

#include <algorithm>
#include <cmath>

namespace lucarne {

using robotboy_beads::GrainFeed;
using robotboy_beads::GrainLiveFrame;
using robotboy_beads::GrainStartEvent;

namespace {

float Semitones(float speed) {
	const float s = 12.f * std::log2(std::max(std::fabs(speed), 1e-6f));
	return std::min(std::max(s, -GrainLayout::kMaxSemitones), GrainLayout::kMaxSemitones);
}
int32_t Wrap(int32_t x, int32_t m) { return ((x % m) + m) % m; }
int32_t ClampIndex(int32_t i, int32_t m) { return std::min(std::max<int32_t>(i, 0), m - 1); }
// Do [a, a+la) and [b, b+lb) overlap on a ring of width w?
bool Overlaps(float a, float la, float b, float lb, float w) {
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

void GrainLayout::Clear() {
	grains_.clear();
	have_epoch_ = false;
	list_.wave.clear();
	list_.marks.clear();
}

void GrainLayout::SetSize(float w, float h) {
	w_ = std::max(w, 1.f);
	h_ = std::max(h, 1.f);
	rows_ = std::max(1, int((h_ - BandHeight()) / (2.f * RowHeight()) + 1e-3f));
}

// The display's height if it were 16:9, capped at its real height.
static float NominalHeight(float w, float h) { return std::min(h, w * 9.f / 16.f); }
float GrainLayout::BandHeight() const { return NominalHeight(w_, h_) * kBandFraction; }
float GrainLayout::RowHeight() const {
	return (NominalHeight(w_, h_) - BandHeight()) / (2.f * kRowsPerSide);
}

float GrainLayout::XForIndex(int32_t index, int32_t max_delay) const {
	return float(max_delay - 1 - index) / float(max_delay) * w_;
}

float GrainLayout::RowY(int side, int row) const {
	const float band_top = (h_ - BandHeight()) * 0.5f;
	const float band_bottom = band_top + BandHeight();
	const float offset = (row + 0.5f) * RowHeight();
	return side > 0 ? band_top - offset : band_bottom + offset;
}

int GrainLayout::PreferredRow(float speed, int rows_per_side) {
	return int(std::lround(std::fabs(Semitones(speed)) / kMaxSemitones * (rows_per_side - 1)));
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
	for (int r = preferred; r < rows_; ++r) if (!taken(r)) return r;
	for (int r = preferred - 1; r >= 0; --r) if (!taken(r)) return r;
	return preferred;
}

Rgb GrainLayout::PanColor(float pan) {
	// Beads bunches pans around the center (BoundedRandomFloat), so the
	// square root stretches small offsets into visible color.
	const float t = std::min(std::max((pan - 0.5f) * 2.f, -1.f), 1.f);
	const float shaped = std::sqrt(std::fabs(t));
	return t < 0.f ? Mix(kPanCenter, kPanLeft, shaped) : Mix(kPanCenter, kPanRight, shaped);
}

Rgb GrainLayout::TapColor(int slot) { return (slot / 2) % 2 == 0 ? PanColor(0.5f) : kRatioGreen; }
int GrainLayout::TapSide(int slot) { return (slot / 2) % 2 == 0 ? +1 : -1; }
int GrainLayout::TapLane(int slot) { return slot & 1; }

float GrainLayout::LaneY(int side, int lane) const {
	const float band_top = (h_ - BandHeight()) * 0.5f;
	const float band_bottom = band_top + BandHeight();
	const float lane_h = band_top * 0.5f;   // each side's height split in two
	const float offset = (lane + 0.5f) * lane_h;
	return side > 0 ? band_top - offset : band_bottom + offset;
}

void GrainLayout::MakeRoom() {
	if (int(grains_.size()) >= kMaxGrains) {
		// Make room: the ended grain closest to vanishing, else the oldest.
		auto victim = grains_.end();
		for (auto it = grains_.begin(); it != grains_.end(); ++it)
			if (it->ended && (victim == grains_.end() || it->fade < victim->fade)) victim = it;
		grains_.erase(victim != grains_.end() ? victim : grains_.begin());
	}
}

void GrainLayout::Birth(const GrainStartEvent& e, const GrainLiveFrame& f) {
	MakeRoom();
	const int32_t m = std::max<int32_t>(f.max_delay, 1);
	const int32_t start = std::min(std::max<int32_t>(e.start_index, 0), m - 1);
	VisualGrain g{};
	g.serial = e.serial;
	g.slot = std::min(std::max<int>(e.slot, 0), GrainFeed::kSlots - 1);
	g.start_index = start;
	g.speed = e.speed;
	g.duration = e.duration;
	g.pan = e.pan;
	g.x0 = XForIndex(start, m);
	g.span = std::min(std::max(e.speed * e.duration / float(m) * w_, -w_), w_);
	g.side = SideFor(e.speed);
	g.row = ChooseRow(g.side, PreferredRow(e.speed, rows_), g.x0, g.span);
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
	if (in.frame) {
		const GrainLiveFrame& f = *in.frame;
		View v = View::Grains;
		if (f.tap_mode) {
			v = View::Running;
			for (const auto& t : f.taps)
				if (t.slice) v = View::Slice;
		}
		if (v != view_) {
			grains_.clear();   // the old view's visuals go at once, no fade
			view_ = v;
		}
	}
	for (VisualGrain& g : grains_)
		if (g.ended) g.fade -= in.dt;
	grains_.erase(std::remove_if(grains_.begin(), grains_.end(),
	                             [](const VisualGrain& g) { return g.ended && g.fade <= 0.f; }),
	              grains_.end());
	if (in.frame && view_ == View::Slice) {
		TapBirthsAndEnds(*in.frame);
	} else if (in.frame && view_ == View::Grains) {
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

void GrainLayout::TapBirthsAndEnds(const GrainLiveFrame& f) {
	const int32_t m = std::max<int32_t>(f.max_delay, 1);
	// Progress of a head through its slice. The read index can sit a sample
	// past the slice's newer end, and head and tap fields can disagree for a
	// frame, so clamp rather than trust the bounds.
	auto cursor = [m](const robotboy_beads::TapState& t) {
		if (t.slice_length <= 0) return 1.f;
		const int32_t done = Wrap(ClampIndex(t.slice_start, m) - ClampIndex(t.read_index, m), m);
		return std::min(std::max(float(done) / float(t.slice_length), 0.f), 1.f);
	};
	for (VisualGrain& g : grains_) {
		if (g.ended) continue;
		const auto& t = f.taps[g.slot];
		if (t.slice && t.serial == g.serial && t.level > kTapMinLevel) {
			g.cursor = cursor(t);
			g.level = t.level;
		} else {
			g.ended = true;   // restarted, or silent after having been audible
			g.fade = kFadeSeconds;
		}
	}
	for (int i = 0; i < GrainLiveFrame::kTaps; ++i) {
		const auto& t = f.taps[i];
		if (!t.slice || t.level <= kTapMinLevel) continue;
		bool known = false;
		for (const VisualGrain& g : grains_)
			if (g.slot == i && g.serial == t.serial) known = true;
		if (known) continue;
		MakeRoom();
		const int32_t start = ClampIndex(t.slice_start, m);
		const float length = float(std::max<int32_t>(t.slice_length, 0));
		VisualGrain g{};
		g.serial = t.serial;
		g.slot = i;
		g.start_index = start;
		g.speed = 1.f;
		g.duration = length;
		g.pan = 0.5f;
		g.x0 = XForIndex(start, m);
		g.span = std::min(length / float(m) * w_, w_);
		g.side = TapSide(i);
		g.row = TapLane(i);
		g.cursor = cursor(t);
		g.level = t.level;
		g.ended = false;
		grains_.push_back(g);
	}
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
	if (view_ != View::Grains) {
		BuildTaps(in);
		return;
	}
	const float row_h = RowHeight();
	const float thick = row_h * 0.5f;
	const float min_len = w_ / 160.f;   // 2 px at MetaModule's 320-wide fullscreen
	for (const VisualGrain& g : grains_) {
		const Rgb base = PanColor(g.pan);
		const float k = g.ended ? 0.6f * (g.fade / kFadeSeconds) : 0.35f + 0.65f * g.level;
		const float y = RowY(g.side, g.row);
		const float len = std::max(std::fabs(g.span), min_len);
		const float start = g.span >= 0.f ? g.x0 : g.x0 - len;
		// Dim and fade toward the background, not black, so a fading grain
		// never shows darker than the display behind it.
		const Rgb bg = list_.background;
		AddSpan(start, len, y - thick * 0.5f, thick, Mix(bg, base, std::min(std::max(k, 0.f), 1.f)));
		const float cx = g.x0 + g.span * g.cursor;
		const float cw = std::max(w_ * 0.008f, thick);
		const float fade = g.ended ? g.fade / kFadeSeconds : 1.f;
		AddSpan(cx - cw * 0.5f, cw, y - row_h * 0.45f, row_h * 0.9f, Mix(bg, Lighten(base, 0.5f), std::min(std::max(fade, 0.f), 1.f)));
	}
}

void GrainLayout::BuildTaps(const Input& in) {
	// Lines twice the grain line's thickness; cursors as wide as grain
	// cursors and half a lane tall.
	const float thick = RowHeight();
	const float lane_h = (h_ - BandHeight()) * 0.25f;
	const float cursor_h = lane_h * 0.5f;
	const float cw = std::max(w_ * 0.008f, RowHeight() * 0.5f);
	const float min_len = w_ / 160.f;
	const Rgb bg = list_.background;
	if (view_ == View::Running && in.frame) {
		// Drawn straight from the live frame: no state, no fade.
		const GrainLiveFrame& f = *in.frame;
		const int32_t m = std::max<int32_t>(f.max_delay, 1);
		const int32_t write = ClampIndex(f.write_index, m);
		for (int i = 0; i < GrainLiveFrame::kTaps; ++i) {
			const auto& t = f.taps[i];
			if (t.level <= kTapMinLevel) continue;
			const int32_t read = ClampIndex(t.read_index, m);
			const float x = XForIndex(read, m);
			const float len = std::max(float(Wrap(read - write, m)) / float(m) * w_, min_len);
			const float y = LaneY(TapSide(i), TapLane(i));
			const Rgb base = TapColor(i);
			AddSpan(x, len, y - thick * 0.5f, thick, Mix(bg, base, 0.15f + 0.85f * std::min(t.level, 1.f)));
			AddSpan(x - cw * 0.5f, cw, y - cursor_h * 0.5f, cursor_h, Lighten(base, 0.5f));
		}
	} else if (view_ == View::Slice) {
		for (const VisualGrain& g : grains_) {
			const Rgb base = TapColor(g.slot);
			const float k = g.ended ? 0.6f * (g.fade / kFadeSeconds) : 0.35f + 0.65f * g.level;
			const float y = LaneY(g.side, g.row);
			AddSpan(g.x0, std::max(g.span, min_len), y - thick * 0.5f, thick,
			        Mix(bg, base, std::min(std::max(k, 0.f), 1.f)));
			const float fade = g.ended ? g.fade / kFadeSeconds : 1.f;
			AddSpan(g.x0 + g.span * g.cursor - cw * 0.5f, cw, y - cursor_h * 0.5f, cursor_h,
			        Mix(bg, Lighten(base, 0.5f), std::min(std::max(fade, 0.f), 1.f)));
		}
	}
}

}  // namespace lucarne
