#pragma once
#include "plugin.hpp"
#include "shapes/OutputStage.hpp"

// Rack-side pieces Skylark and Weaver share: the output and clock settings
// with their menu items and JSON, and the drawing helpers for their displays.
namespace shapeui {

// Context menu settings, saved with the patch.
struct Settings {
	shapes::Range range = shapes::Range::Bipolar5;
	shapes::Treat treatMode = shapes::Treat::Fit;
	float slewMs = 0.f;
	int pulsesPerCycle = 4;

	void reset() {
		*this = Settings();
	}
	void toJson(json_t* rootJ) const;
	// Missing or unknown values fall back to the defaults.
	void fromJson(json_t* rootJ);
};

// Output range, When out of range, Slew, Clock pulses per cycle.
void appendSettingsMenu(Menu* menu, Module* module, Settings* settings);

constexpr float kMinBpm = 20.f, kMaxBpm = 300.f;
// The BPM knob: unsnapped, shown and used to 0.01 BPM, not randomized.
ParamQuantity* configBpm(Module* module, int paramId);
// The tempo a BPM knob value means.
double bpmOf(float value);

// Display colours
inline NVGcolor backgroundColor() {
	return nvgRGB(0x0c, 0x0e, 0x10);
}
inline NVGcolor gridColor() {
	return nvgRGB(0x1d, 0x22, 0x27);
}
inline NVGcolor dividerColor() {
	return nvgRGB(0x3a, 0x41, 0x48);
}
inline NVGcolor mutedColor() {
	return nvgRGB(0x98, 0xa1, 0xab);
}
inline NVGcolor inkColor() {
	return nvgRGB(0xe6, 0xe8, 0xeb);
}
inline NVGcolor signalColor() {
	return nvgRGB(0xff, 0xff, 0xff);
}
// Weaver's operators A, B, C
inline NVGcolor operatorColor(int i) {
	static const NVGcolor colors[3] = {nvgRGB(0xe0, 0xb0, 0x4a), nvgRGB(0x3f, 0xc6, 0xbf), nvgRGB(0xe5, 0x7d, 0xb8)};
	return colors[clamp(i, 0, 2)];
}

// Share Tech Mono from Rack's own assets; may be null.
std::shared_ptr<window::Font> loadFont();

// A display: background in draw(), contents on the light layer so they stay
// lit when the room is dimmed.
struct Screen : LedDisplay {
	float radiusMm = 0.6f;
	void draw(const DrawArgs& args) override;
	void drawLayer(const DrawArgs& args, int layer) override;
	virtual void drawScreen(NVGcontext* vg) = 0;
};

// Five horizontal lines, the centre one heavier.
void drawGrid(NVGcontext* vg, float x, float y, float w, float h);

struct TraceStyle {
	NVGcolor color;
	float widthMm = 0.45f;
	// Values beyond +-limit are clamped (1 is the edge of the box)
	float limit = 1.f;
	// Dash and gap lengths in mm; 0 draws a solid line
	float dashMm = 0.f;
	float gapMm = 0.f;
	// Closed: one more point repeats the first, so the trace spans the box
	// and loops back to its start. Open: the n points span the box.
	bool closed = true;
	// Open traces only: start from the last value at the left edge, so the
	// jump from one cycle into the next is drawn where it happens
	bool leadIn = false;
};

// n values (-1..1 fills the box height) drawn across the box.
void drawTrace(NVGcontext* vg, const float* values, int n, float x, float y, float w, float h, const TraceStyle& style);

// Linear interpolation into a trace at position pos (0..1); closed and open
// as in TraceStyle.
float traceAt(const float* values, int n, float pos, bool closed = true);

// Vertical playhead at pos (0..1) and a dot on the trace at value.
void drawPlayhead(NVGcontext* vg, float x, float y, float w, float h, float pos, float value);

}  // namespace shapeui
