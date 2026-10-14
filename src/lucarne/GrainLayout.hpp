#pragma once
// Lucarne's grain view for Particules: turns a GrainFeed snapshot into
// rectangles. (Retours' tap frames go to ScopeLayout instead.)
// Pure C++ (no Rack, no nanovg) so it is unit-tested; Lucarne.cpp paints
// the result. Units are the display widget's box units, whatever pixel
// size the host renders them at.

#include <cstdint>
#include <vector>

#include "DrawList.hpp"
#include "host/grain_feed.h"

namespace lucarne {

struct VisualGrain {
	uint32_t serial;
	int slot;
	int32_t start_index;
	float speed;
	float duration;
	float pan;     // 0 = left, 0.5 = center, 1 = right; picks the color
	float x0;      // start x
	float span;    // signed length in x; negative = reversed
	int side;      // +1 above the band, -1 below
	int row;       // 0 = next to the band
	float cursor;  // 0..1 along span
	float level;   // envelope while live
	bool ended;
	float fade;    // seconds of fade left once ended
};

class GrainLayout {
public:
	// Rows per side on a 16:9 display. Band and row heights are sized from
	// the width as if the display were 16:9, so a taller display (VCV) keeps
	// a thin band and the same row height, and gains rows: finer pitch.
	static constexpr int kRowsPerSide = 12;
	static constexpr int kMaxGrains = 64;
	static constexpr float kFadeSeconds = 0.15f;
	static constexpr float kBandFraction = 0.2f;
	static constexpr float kMaxSemitones = 24.f;

	struct Input {
		uint32_t epoch = 0;
		const int8_t* peak_lo = nullptr;   // GrainFeed::kColumns each
		const int8_t* peak_hi = nullptr;
		const robotboy_beads::GrainLiveFrame* frame = nullptr;   // null: none yet
		const robotboy_beads::GrainStartEvent* events = nullptr;
		int num_events = 0;
		float dt = 0.f;       // seconds since the last Update
	};

	void SetSize(float w, float h);
	void Update(const Input& in);
	// Forget every grain and drawn mark (Lucarne shows a message instead).
	void Clear();
	const DrawList& draw_list() const { return list_; }
	const std::vector<VisualGrain>& grains() const { return grains_; }

	float XForIndex(int32_t index, int32_t max_delay) const;
	float RowY(int side, int row) const;   // center of the row
	static int PreferredRow(float speed, int rows_per_side = kRowsPerSide);
	int rows_per_side() const { return rows_; }
	static int SideFor(float speed);
	// Grain color for a stereo position: blue at hard left, warm white at
	// center, orange at hard right.
	static Rgb PanColor(float pan);

private:
	float BandHeight() const;
	float RowHeight() const;
	void Birth(const robotboy_beads::GrainStartEvent& e, const robotboy_beads::GrainLiveFrame& f);
	int ChooseRow(int side, int preferred, float x0, float span) const;
	void Build(const Input& in);
	void AddSpan(float x, float len, float y, float h, Rgb c);
	void MakeRoom();

	float w_ = 1.f, h_ = 1.f;
	int rows_ = kRowsPerSide;
	uint32_t epoch_ = 0;
	bool have_epoch_ = false;
	std::vector<VisualGrain> grains_;
	DrawList list_;
};

}  // namespace lucarne
