#pragma once
// Lucarne's display layout: turns a GrainFeed snapshot into rectangles.
// Pure C++ (no Rack, no nanovg) so it is unit-tested; Lucarne.cpp paints
// the result. Units are the display widget's box units, whatever pixel
// size the host renders them at.

#include <cstdint>
#include <vector>

#include "host/grain_feed.h"

namespace lucarne {

struct Rgb { uint8_t r, g, b; };
struct Rect { float x, y, w, h; Rgb color; };

struct DrawList {
	Rgb background{0x10, 0x14, 0x18};   // Loooop's display background
	Rgb wave_color{0x51, 0x66, 0x7A};   // Loooop's waveform blue-gray
	std::vector<Rect> wave;             // one per peak column, all wave_color
	std::vector<Rect> marks;            // write head, grain lines, cursors (in paint order)
};

struct VisualGrain {
	uint32_t serial;
	int slot;
	int32_t start_index;
	float speed;
	float duration;
	float pan;     // 0 = left, 0.5 = centre, 1 = right; picks the colour
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
	float RowY(int side, int row) const;   // centre of the row
	static int PreferredRow(float speed);
	static int SideFor(float speed);
	// Grain colour for a stereo position: blue at hard left, warm white at
	// centre, orange at hard right.
	static Rgb PanColor(float pan);

private:
	float BandHeight() const;
	float RowHeight() const;
	void Birth(const robotboy_beads::GrainStartEvent& e, const robotboy_beads::GrainLiveFrame& f);
	int ChooseRow(int side, int preferred, float x0, float span) const;
	void Build(const Input& in);
	void AddSpan(float x, float len, float y, float h, Rgb c);

	float w_ = 1.f, h_ = 1.f;
	uint32_t epoch_ = 0;
	bool have_epoch_ = false;
	std::vector<VisualGrain> grains_;
	DrawList list_;
};

}  // namespace lucarne
