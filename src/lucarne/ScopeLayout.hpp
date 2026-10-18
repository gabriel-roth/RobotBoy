#pragma once
// Lucarne's echo scope for Retours: turns a ring of recent-audio min/max
// entries plus tap states into rectangles. Pure C++ (no Rack, no nanovg).

#include <algorithm>
#include <cstdint>

#include "ColorScheme.hpp"
#include "DrawList.hpp"
#include "host/grain_feed.h"

namespace lucarne {

// The zoom's tradeoffs: a wide view, a visible gap between the now line
// and the nearest tap, and a steady view while the taps are modulated.
// The zoom is the widest window that keeps the nearest tap minGap from the
// now line, but never narrower than the farthest tap needs (farMargin, and
// its flash bar kFarEdgePx clear of the edge): when the two conflict the
// farthest tap stays on screen. Values tuned by ear in VCV, 2026-10-16.
struct ZoomTuning {
	float minGap = 0.12f;         // now line to nearest tap, fraction of the width
	float farMargin = 1.f;        // window at least the farthest tap's delay x this
	float modHoldSeconds = 10.f;  // modulated: how long a delay extreme is remembered
	float modReleaseSeconds = 10.f;  // ... then how slowly it's let go (time constant)
	float knobGlide = 0.08f;      // seconds, time constant; ~0.25 s to settle
	float modGlide = 0.2f;        // seconds, time constant, while modulated
	// Widest window per sample of the nearest tap's delay.
	float reach() const;   // uses ScopeLayout::kNowInset
};

class ScopeLayout {
public:
	static constexpr int kColumns = 320;           // MetaModule fullscreen width
	static constexpr float kWaveFraction = 0.7f;   // waveform height / display height
	static constexpr float kMinWindowSeconds = 0.05f;

	using ZoomTuning = lucarne::ZoomTuning;
	// The dry playhead: "now" sits this fraction of the width in from the
	// right edge (the strip to its right stays empty, like the space past a
	// tape head), and flashes with the input of the last kDryWindowSeconds.
	static constexpr float kNowInset = 0.06f;
	static constexpr float kDryWindowSeconds = 0.005f;
	// Markers: a quiet line at kMarkerFloor of its color (scaled by the tap's
	// crossfade weight, so a tap fading out fades from view), rising to full
	// as it flashes; the flash bar its color, kBarLighten toward white.
	static constexpr float kMarkerFloor = 0.6f;
	static constexpr float kFarEdgePx = 1.f;   // the farthest tap's flash bar keeps this clear of the left edge
	static constexpr float kBarLighten = 0.1f;

	// A Slice head's band: this much of the tap color over the background.
	static constexpr float kSliceBandMix = 0.15f;
	static constexpr float kTapMinLevel = 0.01f;   // at or below: silent, not drawn
	// Zoom hold: a running tap that goes silent still counts toward the zoom
	// for this fraction of its delay plus kHoldFloor seconds. Shape's
	// envelope drops each tap to zero once per repeat for about 2% of its
	// delay; a real fade-out is only this much late.
	static constexpr float kHoldFraction = 0.05f;
	static constexpr float kHoldFloor = 0.05f;
	// Tap colors (the scheme's tapA for the primary ratio, tapB for the
	// other; Lucarne: amber and cyan, warm against cool, clear of the
	// cursor and the blue-gray waveform, and apart for red-green
	// colorblindness). The dry playhead is the scheme's dry color (Lucarne:
	// near white, apart from both tap hues). Tap slot i belongs to ratio
	// (i / 2) % 2. The primary ratio is the one with the larger crossfade
	// gain: always ratio 0 unclocked; clocked, the two ratios hold
	// neighboring divisions and trade places as Density sweeps.
	// The other ratio takes over as primary only once its gain leads by this
	// much, so a Density sitting at the crossover doesn't flicker the colors.
	static constexpr float kPrimaryHysteresis = 0.1f;
	static int ratioOf(int slot) { return (slot / 2) % 2; }
	static Rgb tapColor(int slot, int primary = 0, const ColorScheme& s = kSchemes[0]) {
		return ratioOf(slot) == primary ? s.tapA : s.tapB;
	}

	struct Tap {
		float delay;   // age of the read point, line samples
		float level;
		int slot;
		bool slice = false;          // a Slice play head
		float sliceOldest = 0.f;    // age of the slice's oldest end (slice only)
		float sliceLength = 0.f;    // line samples (slice only)
		float weight = 1.f;         // gain without the envelope: crossfades only
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
		int numTaps = 0;   // taps passed; silent ones (level <= kTapMinLevel) count toward the hold only
		bool frozen = false;   // Slice: the window is the whole buffer, fixed
		bool modulated = false;     // Retours: the delay moves on its own; zoom to its recent range
		bool tapKnobMoved = false;  // a knob that moves the taps turned: forget that range
		ZoomTuning tuning;
		float ratioGain[2] = {1.f, 0.f};   // each ratio's crossfade gain (no envelope); picks the primary
		float dt = 0.f;
	};

	void setSize(float w, float h);
	void update(const Input& in);
	// Colors come from this scheme; takes effect on the next update().
	void setScheme(const ColorScheme& s) {
		sch = &s;
		list.background = s.bg;
		list.waveColor = s.wave;
	}
	void clear();   // forget the zoom (the next update() snaps)
	const DrawList& getDrawList() const { return list; }
	float getWindow() const { return window; }   // current window, line samples
	int getPrimary() const { return primary; }   // the primary ratio, 0 or 1
	float nowX() const { return width * (1.f - kNowInset); }
	float markerWidth() const { return std::max(width * 0.004f, 1.f); }   // a marker line; its flash bar is 3x
	float xForAge(float age) const;            // age 0 (newest) -> nowX(), age window -> 0
	static float tapOutput(const Input& in, float delay, float level);   // 0..1
	static float dryOutput(const Input& in);   // 0..1: peak of the newest kDryWindowSeconds
	// Scope ring entries to read before update(): enough for the current
	// window or the widest zoom the longest tap allows, whichever is wider,
	// or all of them when the zoom follows more than this frame's taps
	// (modulated: it remembers wider ranges; it widens past what was read
	// on every frame of the glide).
	static int entriesToRead(float window, float longest, bool toWholeBuffer,
	                         int32_t samplesPerEntry, int maxEntries, const ZoomTuning& tuning = {});

private:
	void trackRange(float nearest, float farthest, bool remember, float dt, const ZoomTuning& z);

	float width = 1.f, height = 1.f;
	float window = 1.f;
	uint32_t epoch = 0;
	bool haveWindow = false;
	// Modulated: the nearest and farthest recent delays, and how long each
	// is still held before it releases toward the taps.
	bool haveRange = false;
	float rangeNear = 1.f, rangeFar = 1.f;
	float holdNear = 0.f, holdFar = 0.f;
	int primary = 0;
	float hold[robotboy_beads::GrainLiveFrame::kTaps] = {};   // seconds each slot still counts
	const ColorScheme* sch = &kSchemes[0];
	DrawList list;
};

}  // namespace lucarne
