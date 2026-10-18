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
	float speed;
	float duration;
	float pan;     // 0 = left, 0.5 = center, 1 = right; picks the color
	float x0;      // start x
	float span;    // signed length in x; negative = reversed
	int side;      // +1 above the band, -1 below
	int row;       // 0 = next to the band
	float cursor;  // 0..1 along span
	float level;   // envelope while live
	bool ended;    // only born-ended grains are kept once ended
	float hold;    // seconds left on screen once ended
};

class GrainLayout {
public:
	// Rows per side on a 16:9 display. Band and row heights are sized from
	// the width as if the display were 16:9, so a taller display (VCV) keeps
	// a thin band and the same row height, and gains rows: finer pitch.
	static constexpr int kRowsPerSide = 12;
	static constexpr int kMaxGrains = 64;
	// A grain seen live vanishes the frame it ends, like its sound. One that
	// started and finished between two UI frames was never seen live, so it
	// shows for this long, steady at kBornEndedLevel, then vanishes.
	static constexpr float kBornEndedSeconds = 0.1f;
	static constexpr float kBornEndedLevel = 0.6f;
	static constexpr float kBandFraction = 0.2f;
	static constexpr float kMaxSemitones = 24.f;

	struct Input {
		uint32_t epoch = 0;
		const int8_t* peakLo = nullptr;   // GrainFeed::kColumns each
		const int8_t* peakHi = nullptr;
		const robotboy_beads::GrainLiveFrame* frame = nullptr;   // null: none yet
		const robotboy_beads::GrainStartEvent* events = nullptr;
		int numEvents = 0;
		float dt = 0.f;       // seconds since the last update()
	};

	void setSize(float w, float h);
	void update(const Input& in);
	// Forget every grain and drawn mark (Lucarne shows a message instead).
	void clear();
	const DrawList& getDrawList() const { return list; }
	const std::vector<VisualGrain>& getGrains() const { return grains; }

	float xForIndex(int32_t index, int32_t maxDelay) const;
	float rowY(int side, int row) const;   // center of the row
	static int preferredRow(float speed, int rowsPerSide = kRowsPerSide);
	int getRowsPerSide() const { return rows; }
	static int sideFor(float speed);
	// Grain color for a stereo position: blue at hard left, warm white at
	// center, orange at hard right.
	static Rgb panColor(float pan);

private:
	float bandHeight() const;
	float rowHeight() const;
	void birth(const robotboy_beads::GrainStartEvent& e, const robotboy_beads::GrainLiveFrame& f);
	void chooseRow(float speed, float x0, float span, int& side, int& row) const;
	void build(const Input& in);
	void addSpan(float x, float len, float y, float h, Rgb c);
	void makeRoom();

	float width = 1.f, height = 1.f;
	int rows = kRowsPerSide;
	uint32_t epoch = 0;
	bool haveEpoch = false;
	std::vector<VisualGrain> grains;
	DrawList list;
};

}  // namespace lucarne
