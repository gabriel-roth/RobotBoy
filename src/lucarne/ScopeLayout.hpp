#pragma once
// Lucarne's echo scope for Retours: turns a ring of recent-audio min/max
// entries plus tap states into rectangles. Pure C++ (no Rack, no nanovg).

#include <cstdint>

#include "DrawList.hpp"
#include "host/grain_feed.h"

namespace lucarne {

class ScopeLayout {
public:
	static constexpr int kColumns = 320;           // MetaModule fullscreen width
	static constexpr float kWaveFraction = 0.7f;   // waveform height / display height
	static constexpr float kZoomMargin = 1.15f;    // window = longest audible delay x this
	static constexpr float kMinWindowSeconds = 0.05f;
	static constexpr float kZoomTau = 0.08f;       // seconds; ~0.25 s to settle
	// The dry playhead: "now" sits this fraction of the width in from the
	// right edge (the strip to its right stays empty, like the space past a
	// tape head), and flashes with the input of the last kDryWindowSeconds.
	static constexpr float kNowInset = 0.06f;
	static constexpr float kDryWindowSeconds = 0.005f;
	static constexpr Rgb kDryColor{0xC0, 0xC8, 0xD0};   // the ring view's write head

	// A Slice head's band: this much of the tap color over the background.
	static constexpr float kSliceBandMix = 0.15f;
	static constexpr float kTapMinLevel = 0.01f;   // at or below: silent, not drawn
	// Tap colors: tap slot i's ratio, (i / 2) % 2, picks warm white (the
	// grain view's center color) or green.
	static constexpr Rgb kTapWhite{0xE8, 0xE4, 0xDA};
	static constexpr Rgb kTapGreen{0x8F, 0xD3, 0xA0};
	static Rgb tapColor(int slot) { return (slot / 2) % 2 == 0 ? kTapWhite : kTapGreen; }

	struct Tap {
		float delay;   // age of the read point, line samples
		float level;
		int slot;
		bool slice = false;          // a Slice play head
		float sliceOldest = 0.f;    // age of the slice's oldest end (slice only)
		float sliceLength = 0.f;    // line samples (slice only)
	};
	struct Input {
		uint32_t epoch = 0;
		const int8_t* lo = nullptr;   // newest first
		const int8_t* hi = nullptr;
		int entries = 0;
		int32_t samplesPerEntry = 1;
		float lineRate = 48000.f;
		int32_t maxDelay = 1;
		Tap taps[robotboy_beads::GrainLiveFrame::kTaps];
		int numTaps = 0;   // audible taps only (level > kTapMinLevel)
		bool frozen = false;   // Slice: the window is the whole buffer, fixed
		bool wholeBuffer = false;   // menu: always the whole buffer, never follow the taps
		float dt = 0.f;
	};

	void setSize(float w, float h);
	void update(const Input& in);
	void clear();   // forget the zoom (the next update() snaps)
	const DrawList& getDrawList() const { return list; }
	float getWindow() const { return window; }   // current window, line samples
	float nowX() const { return width * (1.f - kNowInset); }
	float xForAge(float age) const;            // age 0 (newest) -> nowX(), age window -> 0
	static float tapOutput(const Input& in, float delay, float level);   // 0..1
	static float dryOutput(const Input& in);   // 0..1: peak of the newest kDryWindowSeconds

private:
	float width = 1.f, height = 1.f;
	float window = 1.f;
	uint32_t epoch = 0;
	bool haveWindow = false;
	DrawList list;
};

}  // namespace lucarne
