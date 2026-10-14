#pragma once
// Lucarne's echo scope for Retours: turns a ring of recent-audio min/max
// entries plus tap states into rectangles. Pure C++ (no Rack, no nanovg).

#include <cstdint>

#include "GrainLayout.hpp"

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

	struct Tap {
		float delay;   // age of the read point, line samples
		float level;
		int slot;
		bool slice = false;          // a Slice play head
		float slice_oldest = 0.f;    // age of the slice's oldest end (slice only)
		float slice_length = 0.f;    // line samples (slice only)
	};
	struct Input {
		uint32_t epoch = 0;
		const int8_t* lo = nullptr;   // newest first
		const int8_t* hi = nullptr;
		int entries = 0;
		int32_t samples_per_entry = 1;
		float line_rate = 48000.f;
		int32_t max_delay = 1;
		Tap taps[robotboy_beads::GrainLiveFrame::kTaps];
		int num_taps = 0;   // audible taps only (level > GrainLayout::kTapMinLevel)
		float dt = 0.f;
	};

	void SetSize(float w, float h);
	void Update(const Input& in);
	void Clear();   // forget the zoom (next Update snaps)
	const DrawList& draw_list() const { return list_; }
	float window() const { return window_; }   // current window, line samples
	float NowX() const { return w_ * (1.f - kNowInset); }
	float XForAge(float age) const;            // age 0 (newest) -> NowX(), age window -> 0
	static float TapOutput(const Input& in, float delay, float level);   // 0..1
	static float DryOutput(const Input& in);   // 0..1: peak of the newest kDryWindowSeconds

private:
	float w_ = 1.f, h_ = 1.f;
	float window_ = 1.f;
	uint32_t epoch_ = 0;
	bool have_window_ = false;
	DrawList list_;
};

}  // namespace lucarne
